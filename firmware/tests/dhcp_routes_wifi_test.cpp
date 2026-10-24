// Production DHCP routing -> additive netif observers -> SDK-style GOT_IP
// boundary -> real Wi-Fi manager -> actual main-loop consumer -> real TCP server.
// SDK networking is recorded, not running on hardware or a full lwIP stack.
#include "sdk.h"
#include "dhcp_routes.h"
#include "network_wifi.h"
#include <cassert>

extern "C" void __wrap_udp_recv(udp_pcb*,udp_recv_fn,void*);
extern "C" void __wrap_netif_set_addr(netif*,const ip4_addr_t*,const ip4_addr_t*,const ip4_addr_t*);
extern "C" void __wrap_dhcp_parse_extra_opts(dhcp*,uint8_t,uint8_t,uint8_t,pbuf*,uint16_t);
extern "C" netif* __wrap_ip4_route_src_hook(const ip4_addr_t*,const ip4_addr_t*);
extern "C" void __real_dhcp_append_extra_opts(netif*,uint8_t,dhcp_msg*,uint16_t*) {}
extern "C" void __real_dhcp_parse_extra_opts(dhcp*,uint8_t,uint8_t,uint8_t,pbuf*,uint16_t) {}
extern "C" netif* __real_ip4_route_src_hook(const ip4_addr_t*,const ip4_addr_t*) { return netif_default; }
extern "C" void __real_icmp_input(pbuf*,netif*) {}
extern "C" void __real_udp_recv(udp_pcb* pcb,udp_recv_fn callback,void* arg) { pcb->recv=callback; pcb->recv_arg=arg; }
extern "C" void __real_udp_remove(udp_pcb*) {}
extern "C" void __real_netif_set_addr(netif* n,const ip4_addr_t* ip,const ip4_addr_t* mask,const ip4_addr_t* gw) {
    n->ip_addr=*ip; n->netmask=*mask; n->gw=*gw;
    notifyNetif(n,LWIP_NSC_IPV4_ADDRESS_CHANGED|LWIP_NSC_IPV4_GATEWAY_CHANGED);
}
unsigned gatewayEvents=0;
void sdkGatewayObserver(netif* n,netif_nsc_reason_t reason,const netif_ext_callback_args_t*) {
    if (!(reason & LWIP_NSC_IPV4_GATEWAY_CHANGED)) return;
    ++gatewayEvents;
    WiFi.ip=IPAddress(n->ip_addr.addr);
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP,0,true);
}
void acceptedDhcp(void*,udp_pcb*,pbuf* packet,const ip_addr_t*,uint16_t) {
    auto* n=currentInput;
    __wrap_dhcp_parse_extra_opts(&n->lease,n->lease.state,121,packet->tot_len,packet,0);
    n->lease.state=DHCP_STATE_BOUND;
    __wrap_netif_set_addr(n,&n->lease.offered_ip_addr,&n->lease.offered_sn_mask,&n->lease.offered_gw_addr);
}
int main() {
    WifiManager::begin(); connected(); consumeFromMain();
    TCPServer::begin(5055,"secret");
    incomingClient.s=std::make_shared<SocketStub>(); auto socket=incomingClient.s;
    TCPServer::loop(); TCPServer::authenticated=true;
    TCPServer::parser.state=FrameParser::READ_PAYLOAD;
    netif n;
    n.lease.state=DHCP_STATE_REQUESTING;
    n.lease.offered_ip_addr.addr=uint32_t(WiFi.localIP());
    n.lease.offered_sn_mask.addr=htonl(0xffffff00);
    n.lease.offered_gw_addr.addr=htonl(0xc0a80101);
    netif_default=netif_list=currentInput=&n;
    netif_ext_callback_t observer;
    netif_add_ext_callback(&observer,sdkGatewayObserver); // must survive module registration
    udp_pcb pcb;
    __wrap_udp_recv(&pcb,acceptedDhcp,nullptr);
    const uint8_t bytes[]={16,10,20,192,168,1,10};
    pbuf packet{bytes,sizeof(bytes),sizeof(bytes)};
    pcb.recv(pcb.recv_arg,&pcb,&packet,nullptr,67);
    CHECK(DhcpRoutes::active());
    const ip4_addr_t routed{htonl(0x0a140001)},unrelated{htonl(0x08080808)};
    for(unsigned i=0;i<32;++i) {
        __wrap_ip4_route_src_hook(nullptr,&routed);
        WifiManager::loop(); consumeFromMain();
        CHECK(socket->open && TCPServer::authenticated);
        __wrap_ip4_route_src_hook(nullptr,&unrelated);
        WifiManager::loop(); consumeFromMain();
        CHECK(socket->open && TCPServer::authenticated);
    }
    CHECK(gatewayEvents>=64 && TCPServer::parser.state==FrameParser::READ_PAYLOAD);
    WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP,0,true);
    WifiManager::loop(); consumeFromMain();
    CHECK(!socket->open && !TCPServer::authenticated);
    CHECK(TCPServer::parser.state==FrameParser::WAIT_SYNC);
    std::cout << "gateway callbacks preserve TCP; real same-IP outage retires TCP: PASS\n";
}
