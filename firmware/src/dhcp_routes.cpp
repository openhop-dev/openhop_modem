// DHCP option 121 routing for the precompiled ESP-IDF lwIP stack.
// Unknown-option hooks only stage bytes. The original DHCP receive callback
// validates the packet; netif_set_addr at DHCP_STATE_BOUND is the commit point,
// including unchanged renewals and delayed ARP/ACD checking. All lifecycle
// mutation runs on the lwIP core thread; management readers take s_mux.
#include "dhcp_routes.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <cstring>
extern "C" {
#include <lwip/def.h>
#include <lwip/dhcp.h>
#include <lwip/icmp.h>
#include <lwip/ip.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/prot/dhcp.h>
#include <lwip/udp.h>
}

extern "C" void __real_dhcp_append_extra_opts(netif*, uint8_t, dhcp_msg*, uint16_t*);
extern "C" void __real_dhcp_parse_extra_opts(dhcp*, uint8_t, uint8_t, uint8_t, pbuf*, uint16_t);
extern "C" netif* __real_ip4_route_src_hook(const ip4_addr_t*, const ip4_addr_t*);
extern "C" void __real_icmp_input(pbuf*, netif*);
extern "C" void __real_udp_recv(udp_pcb*, udp_recv_fn, void*);
extern "C" void __real_udp_remove(udp_pcb*);
extern "C" void __real_netif_set_addr(netif*, const ip4_addr_t*, const ip4_addr_t*, const ip4_addr_t*);

namespace DhcpRoutes {
namespace {
constexpr size_t MAX_ROUTES = 8, MAX_OWNERS = 4, MAX_OPTION_BYTES = 512;
constexpr uint8_t OPTION_ROUTES = 121, OPTION_MS_ROUTES = 249;
struct Entry { uint32_t dest = 0; uint8_t prefix = 0; uint32_t gateway = 0; };
struct Table {
    Entry routes[MAX_ROUTES];
    size_t count = 0;
    bool active = false, hasDefault = false;
    uint32_t defaultGw = 0;
};
struct Owner {
    netif* interface = nullptr;
    dhcp* client = nullptr;
    uint32_t generation = 0, baseGw = 0;
    bool leaseValid = false;
    Table table;
    bool pending = false;
    uint32_t pendingXid = 0, pendingIp = 0, pendingGeneration = 0;
    Table pendingTable;
};
struct OptionBytes {
    uint8_t bytes[MAX_OPTION_BYTES] = {};
    size_t length = 0;
    bool seen = false, invalid = false;
};
// One serialized DHCP UDP receive frame. Keep bounded option buffers out of
// the lwIP task stack. Reentrant callbacks bypass staging rather than alias it.
struct ReceiveFrame {
    bool active = false, bound = false;
    netif* interface = nullptr;
    dhcp* client = nullptr;
    uint32_t xid = 0, generation = 0;
    OptionBytes primary, microsoft;
};
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Owner owners[MAX_OWNERS];
ReceiveFrame frame;
udp_pcb* dhcpPcb = nullptr;
udp_recv_fn originalReceive = nullptr;
void* originalReceiveArg = nullptr;
netif_ext_callback_t netifCallback;
bool callbackRegistered = false;

uint32_t host(uint32_t raw) { return lwip_ntohl(raw); }
uint32_t raw(uint32_t value) { return lwip_htonl(value); }
bool acceptsAck(uint8_t state) {
    return state == DHCP_STATE_REQUESTING || state == DHCP_STATE_RENEWING ||
           state == DHCP_STATE_REBINDING || state == DHCP_STATE_REBOOTING;
}
Owner* findOwner(netif* n) {
    if (!n) return nullptr;
    for (auto& owner : owners) if (owner.interface == n) return &owner;
    return nullptr;
}
Owner* acquireOwner(netif* n, dhcp* client) {
    portENTER_CRITICAL(&s_mux);
    Owner* owner = findOwner(n);
    if (!owner) {
        for (auto& slot : owners) if (!slot.interface) { owner = &slot; break; }
    }
    if (owner && (owner->interface != n || owner->client != client)) {
        const uint32_t generation = owner->generation + 1;
        *owner = Owner{};
        owner->generation = generation;
        owner->interface = n;
        owner->client = client;
    }
    portEXIT_CRITICAL(&s_mux);
    return owner;
}

Table decode(const OptionBytes& option) {
    Table table;
    if (!option.seen || option.invalid || !option.length) return table;
    size_t i = 0;
    while (i < option.length) {
        const uint8_t width = option.bytes[i++];
        const size_t octets = (width + 7u) / 8u;
        if (width > 32 || i + octets + 4 > option.length) return Table{};
        uint32_t destination = 0;
        for (size_t b = 0; b < octets; ++b) destination |= uint32_t(option.bytes[i + b]) << (24 - 8 * b);
        i += octets;
        uint32_t gateway = 0;
        for (size_t b = 0; b < 4; ++b) gateway = (gateway << 8) | option.bytes[i++];
        // This lwIP port cannot express on-link routes outside the connected
        // subnet through a zero router: RFC3442 requires ignoring those entries.
        if (!gateway) continue;
        if (width == 0) { table.hasDefault = true; table.defaultGw = gateway; }
        else if (table.count < MAX_ROUTES) {
            const uint32_t mask = 0xffffffffu << (32 - width);
            table.routes[table.count++] = Entry{destination & mask, width, gateway};
        }
    }
    table.active = true;
    return table;
}
Table stagedTable() {
    // 121 takes precedence even if malformed; never combine two servers' or
    // two leases' data. Malformed/oversize options install no classless table.
    return decode(frame.primary.seen ? frame.primary : frame.microsoft);
}
void stage(dhcp* client, uint8_t option, uint8_t length, pbuf* p, uint16_t offset) {
    if (!frame.active || frame.client != client) return;
    OptionBytes* bytes = option == OPTION_ROUTES ? &frame.primary :
                         option == OPTION_MS_ROUTES ? &frame.microsoft : nullptr;
    if (!bytes) return;
    bytes->seen = true;
    if (!length || bytes->invalid || bytes->length + length > MAX_OPTION_BYTES || !p ||
        pbuf_copy_partial(p, bytes->bytes + bytes->length, length, offset) != length) {
        bytes->invalid = true;
        return;
    }
    bytes->length += length; // RFC3396 concatenation, including split route entries
}
void invalidate(netif* n, bool removed) {
    Owner* owner = findOwner(n);
    if (!owner) return;
    const uint32_t base = owner->baseGw;
    const bool hadLease = owner->leaseValid;
    portENTER_CRITICAL(&s_mux);
    const uint32_t generation = owner->generation + 1;
    dhcp* client = owner->client;
    *owner = Owner{};
    owner->generation = generation;
    if (!removed) { owner->interface = n; owner->client = client; }
    portEXIT_CRITICAL(&s_mux);
    if (!removed && hadLease) {
        ip4_addr_t gateway;
        ip4_addr_set_u32(&gateway, raw(base));
        netif_set_gw(n, &gateway); // retire a previously selected per-packet next hop
    }
}
void onNetifEvent(netif* n, netif_nsc_reason_t reason, const netif_ext_callback_args_t* args) {
    const bool removed = reason & LWIP_NSC_NETIF_REMOVED;
    const bool down = args && (((reason & LWIP_NSC_LINK_CHANGED) && !args->link_changed.state) ||
                              ((reason & LWIP_NSC_STATUS_CHANGED) && !args->status_changed.state));
    const bool lostAddress = (reason & LWIP_NSC_IPV4_ADDRESS_CHANGED) &&
                            ip4_addr_get_u32(netif_ip4_addr(n)) == 0;
    if (removed || down || lostAddress) invalidate(n, removed);
}
void receive(void* arg, udp_pcb* pcb, pbuf* p, const ip_addr_t* address, u16_t port) {
    (void)arg;
    const auto original = originalReceive;
    void* originalArg = originalReceiveArg;
    if (!original) return; // never registered in this state
    netif* n = ip_current_input_netif();
    dhcp* client = n ? netif_dhcp_data(n) : nullptr;
    if (frame.active || pcb != dhcpPcb || !client || !acceptsAck(client->state)) {
        original(originalArg, pcb, p, address, port);
        return;
    }
    Owner* owner = acquireOwner(n, client);
    if (!owner) { original(originalArg, pcb, p, address, port); return; }
    frame = ReceiveFrame{};
    frame.active = true;
    frame.interface = n;
    frame.client = client;
    frame.xid = client->xid;
    frame.generation = owner->generation;
    original(originalArg, pcb, p, address, port); // original owns/frees p; never read p afterward
    // The original callback can synchronously retire/remove its interface.
    // Prove owner lifetime/generation before dereferencing its DHCP instance.
    if (!frame.bound && owner->interface == n && owner->client == client &&
        owner->generation == frame.generation && client->state == DHCP_STATE_CHECKING &&
        client->xid == frame.xid) {
        owner->pending = true;
        owner->pendingTable = stagedTable();
        owner->pendingXid = client->xid;
        owner->pendingIp = ip4_addr_get_u32(&client->offered_ip_addr);
        owner->pendingGeneration = owner->generation;
    }
    frame.active = false;
}

// Keep the original bounded PRL insertion; called before the SDK appends any
// other extra options. Native lwIP currently requests neither121 nor249.
void requestOption121(dhcp_msg* msg, uint16_t* length) {
    if (!msg || !length || *length > DHCP_OPTIONS_LEN) return;
    auto* bytes = msg->options;
    uint16_t i = 0;
    while (i + 2u <= *length) {
        const uint8_t code = bytes[i];
        if (code == DHCP_OPTION_PAD) { ++i; continue; }
        if (code == DHCP_OPTION_END) return;
        const uint8_t size = bytes[i + 1];
        if (uint32_t(i) + 2 + size > *length) return;
        if (code == DHCP_OPTION_PARAMETER_REQUEST_LIST) {
            for (uint8_t k = 0; k < size; ++k) {
                if (bytes[i + 2 + k] != OPTION_ROUTES) continue;
                // RFC3442 orders121 before router/subnet options even when a
                // future SDK already includes it in its native request list.
                memmove(bytes + i + 3, bytes + i + 2, k);
                bytes[i + 2] = OPTION_ROUTES;
                return;
            }
            if (size == 255 || *length == DHCP_OPTIONS_LEN) return;
            memmove(bytes + i + 3, bytes + i + 2, *length - i - 2);
            bytes[i + 2] = OPTION_ROUTES;
            bytes[i + 1] = size + 1;
            ++*length;
            return;
        }
        i += 2 + size;
    }
}
} // namespace

void begin() {
    // Registration itself happens at the first DHCP PCB registration, already
    // on the initialized lwIP core. Never install callbacks from Arduino's event task.
    Serial.println("[DHCP] classless routes enabled; accepted-lease lifecycle tracking");
}
void registerReceive(udp_pcb* pcb, udp_recv_fn callback, void* arg) {
    if (pcb == dhcpPcb && callback == receive) { __real_udp_recv(pcb, callback, arg); return; }
    // Pinned lwIP DHCP owns one shared PCB. An independent socket using the
    // same ports must keep its own callback/argument, never replace this one.
    if (pcb && callback && IP_IS_V4(&pcb->local_ip) && (!dhcpPcb || dhcpPcb == pcb) &&
        pcb->local_port == 68 && pcb->remote_port == 67) {
        if (!callbackRegistered) {
            netif_add_ext_callback(&netifCallback, onNetifEvent);
            callbackRegistered = true;
        }
        dhcpPcb = pcb;
        originalReceive = callback;
        originalReceiveArg = arg;
        __real_udp_recv(pcb, receive, nullptr);
        return;
    }
    if (pcb == dhcpPcb) { dhcpPcb = nullptr; originalReceive = nullptr; originalReceiveArg = nullptr; }
    __real_udp_recv(pcb, callback, arg);
}
void removePcb(udp_pcb* pcb) {
    if (pcb == dhcpPcb) { dhcpPcb = nullptr; originalReceive = nullptr; originalReceiveArg = nullptr; }
    __real_udp_remove(pcb);
}
void bindAddress(netif* n, const ip4_addr_t* ip, const ip4_addr_t* mask, const ip4_addr_t* gateway) {
    Owner* owner = findOwner(n);
    dhcp* client = n ? netif_dhcp_data(n) : nullptr;
    const bool bound = owner && client && owner->client == client &&
        client->state == DHCP_STATE_BOUND && ip && ip4_addr_get_u32(ip) != 0 &&
        ip4_addr_get_u32(ip) == ip4_addr_get_u32(&client->offered_ip_addr) &&
        netif_is_up(n) && netif_is_link_up(n);
    const bool immediate = bound && frame.active && frame.interface == n && frame.client == client &&
        frame.xid == client->xid && frame.generation == owner->generation;
    const bool delayed = bound && owner->pending && owner->pendingXid == client->xid &&
        owner->pendingIp == ip4_addr_get_u32(ip) && owner->pendingGeneration == owner->generation;
    ip4_addr_t effectiveGateway;
    const ip4_addr_t* selected = gateway;
    if (immediate || delayed) {
        const Table table = immediate ? stagedTable() : owner->pendingTable;
        const uint32_t base = host(ip4_addr_get_u32(&client->offered_gw_addr));
        portENTER_CRITICAL(&s_mux);
        owner->table = table;
        owner->baseGw = base;
        owner->leaseValid = true;
        owner->pending = false;
        portEXIT_CRITICAL(&s_mux);
        if (immediate) frame.bound = true;
        ip4_addr_set_u32(&effectiveGateway, raw(table.active ? (table.hasDefault ? table.defaultGw : 0) : base));
        selected = &effectiveGateway;
    } else if (owner && (!ip || ip4_addr_get_u32(ip) == 0 || !bound)) {
        invalidate(n, false);
    }
    __real_netif_set_addr(n, ip, mask, selected);
}
void applyGateway(netif* n, const ip4_addr_t* destination) {
    Owner* owner = findOwner(n);
    if (!owner || !owner->leaseValid || owner->client != netif_dhcp_data(n) ||
        !netif_is_up(n) || !netif_is_link_up(n) || !dhcp_supplied_address(n) || !destination) return;
    const Table& table = owner->table;
    if (!table.active) return;
    const uint32_t dest = host(ip4_addr_get_u32(destination));
    int best = -1;
    for (size_t i = 0; i < table.count; ++i) {
        const auto& route = table.routes[i];
        const uint32_t mask = 0xffffffffu << (32 - route.prefix);
        if ((dest & mask) == route.dest && (best < 0 || route.prefix > table.routes[best].prefix)) best = int(i);
    }
    const uint32_t nextHop = best >= 0 ? table.routes[best].gateway : table.hasDefault ? table.defaultGw : 0;
    ip4_addr_t gw;
    ip4_addr_set_u32(&gw, raw(nextHop));
    netif_set_gw(n, &gw);
}
void prepareIcmpReply(netif* n) {
    const ip_addr_t* source = ip_current_src_addr();
    if (source && IP_IS_V4(source)) applyGateway(n, ip_2_ip4(source));
}
// Management accessors report only the current default interface, not a
// global table borrowed from whichever interface obtained DHCP most recently.
size_t count() {
    portENTER_CRITICAL(&s_mux);
    const Owner* owner = findOwner(netif_default);
    const size_t value = owner && owner->leaseValid ? owner->table.count : 0;
    portEXIT_CRITICAL(&s_mux);
    return value;
}
bool get(size_t index, Route& out) {
    portENTER_CRITICAL(&s_mux);
    const Owner* owner = findOwner(netif_default);
    const bool valid = owner && owner->leaseValid && index < owner->table.count;
    if (valid) {
        const Entry& entry = owner->table.routes[index];
        out = Route{IPAddress(raw(entry.dest)), entry.prefix, IPAddress(raw(entry.gateway))};
    }
    portEXIT_CRITICAL(&s_mux);
    return valid;
}
bool active() {
    portENTER_CRITICAL(&s_mux);
    const Owner* owner = findOwner(netif_default);
    const bool value = owner && owner->leaseValid && owner->table.active;
    portEXIT_CRITICAL(&s_mux);
    return value;
}
bool hasDefaultRoute() {
    portENTER_CRITICAL(&s_mux);
    const Owner* owner = findOwner(netif_default);
    const bool value = owner && owner->leaseValid && owner->table.hasDefault;
    portEXIT_CRITICAL(&s_mux);
    return value;
}
IPAddress defaultGateway() {
    portENTER_CRITICAL(&s_mux);
    const Owner* owner = findOwner(netif_default);
    const uint32_t value = owner && owner->leaseValid && owner->table.hasDefault ? owner->table.defaultGw : 0;
    portEXIT_CRITICAL(&s_mux);
    return IPAddress(raw(value));
}
IPAddress baseGateway() {
    portENTER_CRITICAL(&s_mux);
    const Owner* owner = findOwner(netif_default);
    const uint32_t value = owner && owner->leaseValid ? owner->baseGw : 0;
    portEXIT_CRITICAL(&s_mux);
    return IPAddress(raw(value));
}
bool baseGatewayValid() {
    portENTER_CRITICAL(&s_mux);
    const Owner* owner = findOwner(netif_default);
    const bool value = owner && owner->leaseValid;
    portEXIT_CRITICAL(&s_mux);
    return value;
}
} // namespace DhcpRoutes

extern "C" void __wrap_dhcp_append_extra_opts(netif* n, uint8_t state, dhcp_msg* msg, uint16_t* length) {
    DhcpRoutes::requestOption121(msg, length);
    __real_dhcp_append_extra_opts(n, state, msg, length);
}
extern "C" void __wrap_dhcp_parse_extra_opts(dhcp* client, uint8_t state, uint8_t option, uint8_t length, pbuf* p, uint16_t offset) {
    __real_dhcp_parse_extra_opts(client, state, option, length, p, offset);
    DhcpRoutes::stage(client, option, length, p, offset);
}
extern "C" netif* __wrap_ip4_route_src_hook(const ip4_addr_t* source, const ip4_addr_t* destination) {
    netif* n = __real_ip4_route_src_hook(source, destination);
    if (!n) n = netif_default;
    DhcpRoutes::applyGateway(n, destination);
    return n;
}
extern "C" void __wrap_icmp_input(pbuf* p, netif* n) {
    DhcpRoutes::prepareIcmpReply(n);
    __real_icmp_input(p, n);
}
extern "C" void __wrap_udp_recv(udp_pcb* pcb, udp_recv_fn callback, void* arg) { DhcpRoutes::registerReceive(pcb, callback, arg); }
extern "C" void __wrap_udp_remove(udp_pcb* pcb) { DhcpRoutes::removePcb(pcb); }
extern "C" void __wrap_netif_set_addr(netif* n, const ip4_addr_t* ip, const ip4_addr_t* mask, const ip4_addr_t* gw) {
    DhcpRoutes::bindAddress(n, ip, mask, gw);
}
#endif // ARDUINO_ARCH_ESP32
