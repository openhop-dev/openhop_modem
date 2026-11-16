#pragma once
extern "C++" {
#include <Arduino.h>
#include <IPAddress.h>
#include <arpa/inet.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>

using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define lwip_ntohl ntohl
#define lwip_htonl htonl

struct ip4_addr_t { uint32_t addr = 0; };
struct ip_addr_t { ip4_addr_t ip4; bool is_v4 = true; };
struct dhcp { uint32_t xid = 1; uint8_t state = 0; ip4_addr_t offered_ip_addr, offered_sn_mask, offered_gw_addr; };
struct netif {
    netif* next = nullptr;
    ip4_addr_t gw;
    ip4_addr_t ip_addr, netmask;
    bool up = true, link = true;
    dhcp lease;
};
inline netif* netif_default = nullptr;
inline netif* netif_list = nullptr;
inline netif* currentInput = nullptr;
inline netif* ip_current_input_netif() { return currentInput; }
inline ip_addr_t currentSource;
inline dhcp* netif_dhcp_data(netif* n) { return &n->lease; }
inline uint32_t ip4_addr_get_u32(const ip4_addr_t* ip) { return ip->addr; }
inline void ip4_addr_set_u32(ip4_addr_t* ip, uint32_t raw) { ip->addr = raw; }
inline ip4_addr_t* netif_ip4_gw(netif* n) { return &n->gw; }
inline ip4_addr_t* netif_ip4_addr(netif* n) { return &n->ip_addr; }
inline bool netif_is_up(netif* n) { return n->up; }
inline bool netif_is_link_up(netif* n) { return n->link; }
inline void notifyGateway(netif* n);
inline void netif_set_gw(netif* n, const ip4_addr_t* gw) {
    if (n->gw.addr != gw->addr) { n->gw = *gw; notifyGateway(n); }
}
inline const ip_addr_t* ip_current_src_addr() { return &currentSource; }
#define IP_IS_V4(ip) ((ip) != nullptr && (ip)->is_v4)
inline const ip4_addr_t* ip_2_ip4(const ip_addr_t* ip) { return &ip->ip4; }

struct pbuf {
    const uint8_t* payload;
    uint16_t len;
    uint16_t tot_len;
    pbuf* next = nullptr;
};
inline uint16_t pbuf_copy_partial(const pbuf* p, void* out, uint16_t size, uint16_t offset) {
    auto* bytes = static_cast<uint8_t*>(out);
    uint16_t copied = 0;
    while (p && offset >= p->len) { offset -= p->len; p = p->next; }
    while (p && copied < size) {
        const uint16_t n = std::min<uint16_t>(p->len - offset, size - copied);
        std::memcpy(bytes + copied, p->payload + offset, n);
        copied += n;
        offset = 0;
        p = p->next;
    }
    return copied;
}
constexpr uint16_t DHCP_OPTIONS_LEN = 312;
constexpr uint8_t DHCP_OPTION_PAD = 0, DHCP_OPTION_END = 255;
constexpr uint8_t DHCP_OPTION_PARAMETER_REQUEST_LIST = 55;
enum { DHCP_STATE_SELECTING, DHCP_STATE_REQUESTING, DHCP_STATE_RENEWING,
       DHCP_STATE_REBINDING, DHCP_STATE_REBOOTING, DHCP_STATE_BOUND, DHCP_STATE_CHECKING, DHCP_STATE_BACKING_OFF };
inline bool dhcp_supplied_address(netif* n) { return n->lease.state == DHCP_STATE_BOUND || n->lease.state == DHCP_STATE_RENEWING || n->lease.state == DHCP_STATE_REBINDING; }
struct dhcp_msg { uint8_t options[DHCP_OPTIONS_LEN] = {}; };

using u16_t = uint16_t;
struct udp_pcb;
using udp_recv_fn = void (*)(void*, udp_pcb*, pbuf*, const ip_addr_t*, u16_t);
struct udp_pcb { uint16_t local_port = 68, remote_port = 67; udp_recv_fn recv = nullptr; void* recv_arg = nullptr; ip_addr_t local_ip; };
using netif_nsc_reason_t = uint16_t;
union netif_ext_callback_args_t { struct { uint8_t state; } link_changed; struct { uint8_t state; } status_changed; };
using netif_ext_callback_fn = void (*)(netif*, netif_nsc_reason_t, const netif_ext_callback_args_t*);
struct netif_ext_callback_t { netif_ext_callback_fn callback = nullptr; };
constexpr netif_nsc_reason_t LWIP_NSC_NETIF_REMOVED = 2, LWIP_NSC_LINK_CHANGED = 4, LWIP_NSC_STATUS_CHANGED = 8, LWIP_NSC_IPV4_ADDRESS_CHANGED = 16;
constexpr netif_nsc_reason_t LWIP_NSC_IPV4_GATEWAY_CHANGED = 32;
inline std::vector<netif_ext_callback_t*> installedCallbacks;
inline void netif_add_ext_callback(netif_ext_callback_t* record, netif_ext_callback_fn fn) { record->callback = fn; installedCallbacks.push_back(record); }
inline void notifyNetif(netif* n, netif_nsc_reason_t reason, bool state = false) {
    netif_ext_callback_args_t args{}; args.link_changed.state = state;
    for (auto* record : installedCallbacks) record->callback(n, reason, &args);
}
inline void notifyGateway(netif* n) { notifyNetif(n,LWIP_NSC_IPV4_GATEWAY_CHANGED); }
} // extern C++: SDK include shims may be included inside extern C
