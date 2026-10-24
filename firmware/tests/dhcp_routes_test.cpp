// Production DHCP wrapper tests at a recording lwIP callback boundary.
// The fixture models accepted/rejected/binding callbacks; it is not a DHCP
// protocol implementation or a physical network test.
#include "sdk.h"
#include "dhcp_routes.h"
#include "network_display.h"
#include <cassert>
#include <iostream>
#include <string>
#include <random>

extern "C" void __wrap_dhcp_parse_extra_opts(dhcp*, uint8_t, uint8_t, uint8_t, pbuf*, uint16_t);
extern "C" void __wrap_dhcp_append_extra_opts(netif*,uint8_t,dhcp_msg*,uint16_t*);
extern "C" netif* __wrap_ip4_route_src_hook(const ip4_addr_t*, const ip4_addr_t*);
extern "C" void __real_dhcp_append_extra_opts(netif*, uint8_t, dhcp_msg*, uint16_t*) {}
extern "C" void __real_dhcp_parse_extra_opts(dhcp*, uint8_t, uint8_t, uint8_t, pbuf*, uint16_t) {}
extern "C" netif* __real_ip4_route_src_hook(const ip4_addr_t*, const ip4_addr_t*) { return netif_default; }
extern "C" void __real_icmp_input(pbuf*, netif*) {}
extern "C" void __real_udp_recv(udp_pcb* pcb, udp_recv_fn recv, void* arg) { pcb->recv=recv; pcb->recv_arg=arg; }
extern "C" void __real_udp_remove(udp_pcb*) {}
extern "C" void __real_netif_set_addr(netif* n, const ip4_addr_t* ip, const ip4_addr_t* mask, const ip4_addr_t* gw) {
    n->ip_addr=*ip; n->netmask=*mask; n->gw=*gw;
    notifyNetif(n,LWIP_NSC_IPV4_ADDRESS_CHANGED);
}
// The original PR has neither lifecycle wrapper. These fallbacks keep it
// executable so RED is a behavioral assertion rather than a missing symbol.
extern "C" __attribute__((weak)) void __wrap_udp_recv(udp_pcb* pcb, udp_recv_fn recv, void* arg) { __real_udp_recv(pcb,recv,arg); }
extern "C" __attribute__((weak)) void __wrap_udp_remove(udp_pcb* pcb) { __real_udp_remove(pcb); }
extern "C" __attribute__((weak)) void __wrap_netif_set_addr(netif* n,const ip4_addr_t* ip,const ip4_addr_t* mask,const ip4_addr_t* gw) { __real_netif_set_addr(n,ip,mask,gw); }

enum class Verdict { ACK, OFFER, REJECT, CHECKING, REMOVED };
Verdict verdict=Verdict::ACK;
int callbackCalls=0;
int alternateCalls=0, alternateCookie=0;
void alternateReceive(void* arg,udp_pcb*,pbuf*,const ip_addr_t*,uint16_t port) {
    assert(arg==&alternateCookie && port==67);
    ++alternateCalls;
}
std::vector<uint8_t> routes{16,10,20,192,168,1,10};
size_t chunkSize=255;
uint8_t optionCode=121;
std::vector<uint8_t> alternateRoutes;
bool alternateFirst=false;
bool includeRoutes=true;
void* expectedArg=reinterpret_cast<void*>(0x1234);
void sdkReceive(void* arg,udp_pcb*,pbuf* packet,const ip_addr_t*,uint16_t port) {
    assert(arg==expectedArg && port==67);
    ++callbackCalls;
    auto* n=currentInput;
    auto parse=[&](uint8_t code,size_t base,size_t size){
        if (!size) __wrap_dhcp_parse_extra_opts(&n->lease,n->lease.state,code,0,packet,base);
        for (size_t offset=0; offset<size; offset+=chunkSize) {
            const auto length=std::min(chunkSize,size-offset);
            __wrap_dhcp_parse_extra_opts(&n->lease,n->lease.state,code,length,packet,base+offset);
        }
    };
    if (includeRoutes) {
        if (alternateFirst && !alternateRoutes.empty()) parse(249,routes.size(),alternateRoutes.size());
        parse(optionCode,0,routes.size());
        if (!alternateFirst && !alternateRoutes.empty()) parse(249,routes.size(),alternateRoutes.size());
    }
    if (verdict==Verdict::REJECT) return;
    if (verdict==Verdict::REMOVED) { notifyNetif(n,LWIP_NSC_NETIF_REMOVED); delete n; currentInput=nullptr; return; }
    if (verdict==Verdict::OFFER) { n->lease.state=DHCP_STATE_REQUESTING; return; }
    if (verdict==Verdict::CHECKING) { n->lease.state=DHCP_STATE_CHECKING; return; }
    n->lease.state=DHCP_STATE_BOUND;
    __wrap_netif_set_addr(n,&n->lease.offered_ip_addr,&n->lease.offered_sn_mask,&n->lease.offered_gw_addr);
}
void deliver(netif& n,udp_pcb& pcb,Verdict result,bool withRoutes) {
    currentInput=&n; verdict=result; includeRoutes=withRoutes;
    std::vector<uint8_t> payload=routes;
    payload.insert(payload.end(),alternateRoutes.begin(),alternateRoutes.end());
    const size_t split=payload.size()/2;
    pbuf tail{payload.data()+split,uint16_t(payload.size()-split),uint16_t(payload.size()-split)};
    pbuf packet{payload.data(),uint16_t(split),uint16_t(payload.size()),&tail};
    pcb.recv(pcb.recv_arg,&pcb,&packet,nullptr,67);
}
#define CHECK(x) do { if(!(x)) { std::cerr<<__LINE__<<": " #x "\n"; return 1; } } while(0)
int main(int argc,char** argv) {
    assert(argc==2); const std::string scenario=argv[1];
    netif station;
    station.gw.addr=htonl(0xc0a80101);
    station.lease.offered_gw_addr=station.gw;
    station.lease.offered_ip_addr.addr=htonl(0xc0a80132);
    station.lease.offered_sn_mask.addr=htonl(0xffffff00);
    station.lease.state=DHCP_STATE_REQUESTING;
    netif_default=netif_list=&station;
    DhcpRoutes::begin();
    udp_pcb pcb;
    if (scenario=="ipv6_not_intercepted") {
        udp_pcb ipv6; ipv6.local_ip.is_v4=false;
        __wrap_udp_recv(&ipv6,alternateReceive,&alternateCookie);
        CHECK(ipv6.recv==alternateReceive && ipv6.recv_arg==&alternateCookie);
        return 0;
    }
    __wrap_udp_recv(&pcb,sdkReceive,expectedArg);
    if (scenario=="prl_order_and_bounds") {
        dhcp_msg message;
        const uint8_t initial[]={55,3,3,1,6};
        std::memcpy(message.options,initial,sizeof(initial)); uint16_t length=sizeof(initial);
        __wrap_dhcp_append_extra_opts(&station,0,&message,&length);
        CHECK(length==6 && message.options[1]==4 && message.options[2]==121 && message.options[3]==3);
        __wrap_dhcp_append_extra_opts(&station,0,&message,&length); CHECK(length==6);
        const uint8_t alreadyPresent[]={55,3,3,121,6};
        std::memcpy(message.options,alreadyPresent,sizeof(alreadyPresent)); length=sizeof(alreadyPresent);
        __wrap_dhcp_append_extra_opts(&station,0,&message,&length);
        CHECK(length==5 && message.options[2]==121 && message.options[3]==3);
        std::memset(message.options,0,sizeof(message.options));
        std::memcpy(message.options,initial,sizeof(initial)); length=DHCP_OPTIONS_LEN;
        auto before=message;
        __wrap_dhcp_append_extra_opts(&station,0,&message,&length);
        CHECK(length==DHCP_OPTIONS_LEN && std::memcmp(&before,&message,sizeof(message))==0);
        length=2; before=message;
        __wrap_dhcp_append_extra_opts(&station,0,&message,&length);
        CHECK(length==2 && std::memcmp(&before,&message,sizeof(message))==0);
        return 0;
    }
    if (scenario=="route_payload_fuzz") {
        std::mt19937 rng(3442);
        for (size_t iteration=0; iteration<1000; ++iteration) {
            routes.resize(rng()%600); for(auto& byte:routes) byte=uint8_t(rng());
            chunkSize=1+rng()%255; optionCode=iteration%2 ? 121 : 249;
            station.lease.state=DHCP_STATE_RENEWING;
            deliver(station,pcb,Verdict::ACK,true);
            CHECK(DhcpRoutes::count()<=8);
            DhcpRoutes::Route entry;
            for(size_t i=0;DhcpRoutes::get(i,entry);++i) CHECK(i<8 && entry.prefix>0 && entry.prefix<=32);
        }
        return 0;
    }
    if (scenario=="callback_lifecycle") {
        udp_pcb other;
        __wrap_udp_recv(&other,alternateReceive,&alternateCookie);
        deliver(station,pcb,Verdict::ACK,true);
        CHECK(callbackCalls==1 && alternateCalls==0);
        deliver(station,other,Verdict::ACK,false);
        CHECK(callbackCalls==1 && alternateCalls==1);
        __wrap_udp_remove(&other);
        station.lease.state=DHCP_STATE_RENEWING;
        deliver(station,pcb,Verdict::ACK,false);
        CHECK(callbackCalls==2 && alternateCalls==1);
        CHECK(!DhcpRoutes::active());
        // Replacing the intercepted callback/argument preserves both; removing
        // or unregistering it permits another PCB to become the tracked client.
        expectedArg=reinterpret_cast<void*>(0x5678);
        __wrap_udp_recv(&pcb,sdkReceive,expectedArg);
        station.lease.state=DHCP_STATE_RENEWING;
        deliver(station,pcb,Verdict::ACK,true);
        CHECK(callbackCalls==3 && DhcpRoutes::active());
        __wrap_udp_recv(&pcb,nullptr,nullptr);
        CHECK(pcb.recv==nullptr && pcb.recv_arg==nullptr);
        __wrap_udp_recv(&other,sdkReceive,expectedArg);
        station.lease.state=DHCP_STATE_RENEWING;
        deliver(station,other,Verdict::ACK,false);
        CHECK(callbackCalls==4 && !DhcpRoutes::active());
        __wrap_udp_remove(&other);
        __wrap_udp_recv(&pcb,sdkReceive,expectedArg);
        station.lease.state=DHCP_STATE_RENEWING;
        deliver(station,pcb,Verdict::ACK,true);
        CHECK(callbackCalls==5 && DhcpRoutes::active());
        udp_pcb unrelated; unrelated.local_port=9000; unrelated.remote_port=9001;
        __wrap_udp_recv(&unrelated,alternateReceive,&alternateCookie);
        CHECK(unrelated.recv==alternateReceive && unrelated.recv_arg==&alternateCookie);
        return 0;
    }
    if (scenario=="removed_during_receive") {
        auto* heapInterface=new netif(station);
        netif_default=heapInterface;
        deliver(*heapInterface,pcb,Verdict::REMOVED,true);
        netif_default=&station;
        CHECK(!DhcpRoutes::active());
        return 0;
    }
    if (scenario=="offer_not_published" || scenario=="rejected_not_published") {
        station.lease.state=scenario=="offer_not_published" ? DHCP_STATE_SELECTING : DHCP_STATE_REQUESTING;
        deliver(station,pcb,scenario=="offer_not_published" ? Verdict::OFFER : Verdict::REJECT,true);
        CHECK(!DhcpRoutes::active() && DhcpRoutes::count()==0);
    } else if (scenario=="delayed_bind" || scenario=="delayed_link_loss" || scenario=="delayed_xid_change") {
        deliver(station,pcb,Verdict::CHECKING,true);
        CHECK(!DhcpRoutes::active());
        if (scenario=="delayed_link_loss") {
            station.link=false; notifyNetif(&station,LWIP_NSC_LINK_CHANGED); station.link=true;
        }
        if (scenario=="delayed_xid_change") ++station.lease.xid;
        station.lease.state=DHCP_STATE_BOUND;
        __wrap_netif_set_addr(&station,&station.lease.offered_ip_addr,&station.lease.offered_sn_mask,&station.lease.offered_gw_addr);
        CHECK(DhcpRoutes::active()==(scenario=="delayed_bind"));
    } else {
        if (scenario=="concatenated_options") chunkSize=3;
        if (scenario=="249_only") optionCode=249;
        if (scenario=="121_wins_first" || scenario=="121_wins_last") {
            alternateRoutes={0,192,168,1,99}; alternateFirst=scenario=="121_wins_last";
        }
        deliver(station,pcb,Verdict::ACK,true);
        CHECK(DhcpRoutes::active() && DhcpRoutes::count()==1);
        const ip4_addr_t remote{htonl(0x0a140001)}, unrelated{htonl(0x08080808)};
        __wrap_ip4_route_src_hook(nullptr,&remote);
        CHECK(station.gw.addr==htonl(0xc0a8010a));
        if (scenario=="no_default" || scenario=="concatenated_options" || scenario=="249_only" ||
            scenario=="121_wins_first" || scenario=="121_wins_last") {
            __wrap_ip4_route_src_hook(nullptr,&unrelated);
            CHECK(station.gw.addr==0);
            CHECK(displayGateway(IPAddress(station.gw.addr))==IPAddress(0u));
        } else if (scenario=="renew_without_routes") {
            station.lease.state=DHCP_STATE_RENEWING;
            deliver(station,pcb,Verdict::ACK,false);
            CHECK(!DhcpRoutes::active() && DhcpRoutes::count()==0);
            CHECK(station.gw.addr==station.lease.offered_gw_addr.addr);
        } else if (scenario=="link_down" || scenario=="admin_down" || scenario=="address_loss" || scenario=="netif_removed") {
            if (scenario=="link_down") { station.link=false; notifyNetif(&station,LWIP_NSC_LINK_CHANGED); }
            if (scenario=="admin_down") { station.up=false; notifyNetif(&station,LWIP_NSC_STATUS_CHANGED); }
            if (scenario=="address_loss") { station.ip_addr.addr=0; notifyNetif(&station,LWIP_NSC_IPV4_ADDRESS_CHANGED); }
            if (scenario=="netif_removed") notifyNetif(&station,LWIP_NSC_NETIF_REMOVED);
            CHECK(!DhcpRoutes::active() && DhcpRoutes::count()==0);
            if (scenario!="netif_removed") CHECK(station.gw.addr==station.lease.offered_gw_addr.addr);
        } else if (scenario=="malformed_replacement" || scenario=="malformed121_blocks249" || scenario=="oversize_replacement") {
            routes={33};
            if (scenario=="malformed121_blocks249") alternateRoutes={0,192,168,1,99};
            if (scenario=="oversize_replacement") {
                routes.clear();
                for(size_t i=0;i<74;++i) routes.insert(routes.end(),{16,10,20,192,168,1,10});
            }
            station.lease.state=DHCP_STATE_RENEWING;
            deliver(station,pcb,Verdict::ACK,true);
            CHECK(!DhcpRoutes::active() && DhcpRoutes::count()==0);
            CHECK(station.gw.addr==station.lease.offered_gw_addr.addr);
        } else if (scenario=="rejected_replacement_then_acceptance") {
            routes={16,172,20,192,168,1,20}; station.lease.state=DHCP_STATE_RENEWING;
            deliver(station,pcb,Verdict::REJECT,true);
            DhcpRoutes::Route entry;
            CHECK(DhcpRoutes::get(0,entry) && entry.destination==IPAddress(10,20,0,0));
            deliver(station,pcb,Verdict::ACK,false);
            CHECK(!DhcpRoutes::active() && station.gw.addr==station.lease.offered_gw_addr.addr);
        } else if (scenario=="begin_does_not_reset") {
            DhcpRoutes::begin(); CHECK(DhcpRoutes::active() && DhcpRoutes::count()==1);
        } else if (scenario=="zero_router_ignored") {
            routes={16,10,20,0,0,0,0}; station.lease.state=DHCP_STATE_RENEWING;
            deliver(station,pcb,Verdict::ACK,true);
            CHECK(DhcpRoutes::active() && DhcpRoutes::count()==0 && !DhcpRoutes::hasDefaultRoute());
            CHECK(station.gw.addr==0);
        } else if (scenario=="static_address_replaces_lease") {
            station.lease.state=DHCP_STATE_BACKING_OFF;
            const ip4_addr_t address{htonl(0xc0a80164)},gateway{htonl(0xc0a801fe)};
            __wrap_netif_set_addr(&station,&address,&station.netmask,&gateway);
            CHECK(!DhcpRoutes::active() && !DhcpRoutes::baseGatewayValid());
            CHECK(station.gw.addr==gateway.addr);
        } else if (scenario=="longest_prefix_and_default") {
            routes={8,10,192,168,1,11,16,10,20,192,168,1,12,24,10,20,30,192,168,1,13,
                    32,10,20,30,1,192,168,1,14,0,192,168,1,2};
            station.lease.state=DHCP_STATE_RENEWING; deliver(station,pcb,Verdict::ACK,true);
            CHECK(DhcpRoutes::count()==4 && DhcpRoutes::hasDefaultRoute());
            CHECK(displayGateway(IPAddress(station.gw.addr))==IPAddress(192,168,1,2));
            const uint32_t destinations[]={0x0a141e01,0x0a141e02,0x0a141f02,0x0a150102,0x08080808};
            for(size_t i=0;i<5;++i) {
                const ip4_addr_t target{htonl(destinations[i])};
                __wrap_ip4_route_src_hook(nullptr,&target);
                CHECK(station.gw.addr==htonl(i<4 ? 0xc0a8010e - i : 0xc0a80102));
            }
        } else if (scenario=="route_capacity") {
            routes.clear();
            for(uint8_t i=0;i<12;++i) routes.insert(routes.end(),{16,10,i,192,168,1,uint8_t(i+10)});
            routes.insert(routes.end(),{0,192,168,1,2});
            station.lease.state=DHCP_STATE_RENEWING; deliver(station,pcb,Verdict::ACK,true);
            CHECK(DhcpRoutes::count()==8 && DhcpRoutes::hasDefaultRoute());
            DhcpRoutes::Route entry; CHECK(!DhcpRoutes::get(8,entry));
            CHECK(DhcpRoutes::defaultGateway()==IPAddress(192,168,1,2));
        } else if (scenario=="owner_isolation") {
            netif ethernet=station; ethernet.lease.state=DHCP_STATE_REQUESTING;
            ethernet.lease.offered_ip_addr.addr=htonl(0xc0a8013c);
            ethernet.next=&station; netif_list=&ethernet;
            routes={16,172,20,192,168,1,20};
            deliver(ethernet,pcb,Verdict::ACK,true);
            CHECK(DhcpRoutes::count()==1);
            DhcpRoutes::Route entry;
            CHECK(DhcpRoutes::get(0,entry));
            CHECK(entry.destination==IPAddress(10,20,0,0));
            ethernet.link=false; notifyNetif(&ethernet,LWIP_NSC_LINK_CHANGED);
            CHECK(DhcpRoutes::active()); // another interface's loss must not erase STA
            netif_default=&ethernet;
            CHECK(!DhcpRoutes::active());
        } else assert(false);
    }
    CHECK(callbackCalls>=1);
    std::cout<<scenario<<": PASS\n";
}
