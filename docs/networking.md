# Networking and management

Use this reference for network behavior, admission, recovery and HTTP credentials.
For first connection and host setup, see [installation](../INSTALL.md).
For the separate binary radio interface and independent clients, see the
[TCP integration guide](tcp-client-integration.md).

## Network exposure: LAN-only by design

On Wi-Fi / Ethernet boards the radio protocol on 5055 (plus board-declared secondary ports)
and management HTTP on 80, where supported, refuse clients whose source address is outside
RFC1918 (`10/8`, `172.16/12`, `192.168/16`), link-local (`169.254/16`)
or loopback (`127/8`). The check runs at `accept()` time before any
frame parsing or auth. NAT port-forwards / Internet tunnels with a
public source IP are dropped unconditionally — TCP closes the socket,
OTA returns 403. Hard firmware policy; lifting it means editing
`firmware/include/net_filter.h` and re-flashing.

For remote access, the source address actually visible to the modem must fall
inside one of those accepted ranges. A WireGuard overlay in `10/8`, or a subnet
router using a LAN source address, can meet that policy. A direct Tailscale
`100.64/10` source does not: the CGNAT range is not accepted by
[net_filter.h](../firmware/include/net_filter.h). A route alone does not rewrite
the source address or bypass admission.

### DHCP classless static routes (option 121)

The ESP32/ESP-IDF lwIP client does not request or parse DHCP option 121
(RFC 3442). On routed LANs where a VPN or overlay prefix is reachable
through a different next hop than the default router, replies from the
modem leave through the default gateway and router connection tracking
drops them (asymmetric routing). `firmware/include/dhcp_routes.h` adds
option 121 to the Parameter Request List and stages option 121 (or the
Microsoft 249 alias when 121 is absent) while the SDK parses a DHCP reply.
Routes are published only when the original DHCP client binds an accepted
lease, including unchanged renewals and delayed address-conflict checking.
OFFERs and rejected messages never change the active table. A newly accepted
ACK without classless routes removes the previous lease's table. Malformed
or oversized route options install no classless table for that new lease;
they never preserve another lease's routes.

Tables belong to their actual Wi-Fi/Ethernet interface. Link/admin down,
address loss and interface removal retire that interface's active and pending
state without erasing another interface's lease. Up to four interface owners
are tracked, each retaining eight nondefault routes and a separate classless
default route. Repeated
121/249 options are concatenated (up to 512 bytes per option code); 121 takes
precedence. Prefixes are normalized and unsupported zero-router/on-link entries
are ignored. When 121 is authoritative, option 3 is ignored even if no `/0`
route was provided. Learned entries are listed on `/stats` and `/api/network`
(`base_gateway`, `dhcp_routes_active`, `dhcp_routes`). The LAN-only source
filter above is unchanged and still applies.

The existing per-packet lwIP gateway technique remains: the selected next hop
is retained until another routed send or lease transition. ICMP echo replies
are covered explicitly; other explicit-netif output paths can still use the
last selected gateway. This change does not claim a general-purpose routing
stack or physical routed-LAN validation.

Host regressions (g++ with AddressSanitizer/UndefinedBehaviorSanitizer):

```bash
python3 firmware/tools/test_dhcp_routes.py
python3 firmware/tools/test_wifi_recovery.py
```

These execute production routing/lifecycle, Wi-Fi/TCP and main-loop consumer
code at recording SDK boundaries. SDK packet acceptance and network callbacks
are fixtures, not a running full network stack or a physical DHCP/VPN test.

T114 has no IP stack; its host paths are USB-CDC and the secondary UART. Use
the matching Adafruit nRF52 USB bootloader update flow. Declared in-app
`CMD_OTA_*` commands do not implement a working flash writer in current source.

### Wi-Fi outage recovery

Saved Wi-Fi connects asynchronously: USB/radio/main-loop work does not wait for
association or DHCP. Each attempt has a 30-second deadline, followed by retry
backoff of 5, 10, 20, 40 and then at most 60 seconds. Every third failed attempt
recreates **only the station interface**, with a 250 ms cooperative settling
interval. Arduino's reason-dependent automatic reconnect is disabled; its initial
one-time retry can still occur within the firmware deadline. Authentication
failure and lost-IP events therefore cannot leave the manager passively waiting
forever. Recovery neither erases credentials nor primarily relies on MCU reboot.
Driver API calls are still synchronous; this is a cooperative recovery policy,
not a hard real-time guarantee for the underlying Wi-Fi driver.

If the initial connection fails, the open `openHop-Modem-XXXX` setup AP stays
available while the saved network is retried in AP+STA mode. The setup HTTP server
checks each accepted socket's local address against the current nonzero AP IPv4
address before reading configuration, scanning, or saving changes, and requires
AP mode to remain enabled. STA, IPv6, and missing/stale local addresses are
rejected, independently of listener binding (the framework may wildcard-bind).
On STA success it is stopped/deleted and the AP is removed before normal
management HTTP/OTA starts. Failed AP startup
or shutdown is retried at five-second intervals. An empty configuration stays in
intentional setup mode without attempting STA. After the first successful STA
connection, later outages retry STA without opening a new unauthenticated portal.

Disconnect/stop/lost-IP events and address changes invalidate the old Wi-Fi TCP
session, including parser and authentication state, even when an outage completes
between loop polls. The next client must authenticate again when a token is set.
Sessions bound to a different interface address (Ethernet) and the wildcard TCP
and management listeners are not routinely restarted; an unchanged DHCP renewal
alone does not drop the client. The displayed IP follows the current usable STA
address (or the setup AP address), rather than retaining an obsolete lease.
Static/DHCP selection, hostname, antenna selection and the saved/default-on
Wi-Fi power-save policy are unchanged.

Host regression checks (C++17 compiler required):

```bash
python3 firmware/tools/test_wifi_recovery.py
```

These compile the production Wi-Fi manager, TCP server and frame parser with
recording hardware stubs, plus the complete production portal handlers and
extracted lifecycle functions. Portal admission tests deliberately use a wildcard
listener and exercise AP, STA, IPv6, missing/stale address, and disabled AP cases
on every route, checking rejected requests have no scan/configuration/RF effects.
They cover deadlines/backoff and timer wrap, failures and station restarts,
short event outages, address changes, AP setup/retry/teardown, and per-interface
TCP authentication/parser cleanup. They do **not** simulate the ESP32 driver,
prove RF/DHCP behavior or establish that a particular router/board's outage is
fixed. Hardware validation must still exercise multi-minute AP outages, wrong
credentials, DHCP expiry/address changes, HTTP/OTA and TCP recovery on the exact
board and firmware build.

### Web UI / OTA / JSON API authentication (v0.8+)

From v0.8 the HTTP surface (web management page, `/api/*` JSON endpoints,
and OTA `/update` on ESP builds) is gated by HTTP Basic Auth. Defaults on
first boot:

- **user:** `admin`
- **password:** `openhop`

Change the password via the **Change HTTP password** form in the web
UI; it is persisted in NVS under `http_pass`. ArduinoOTA (espota) uses
the same password as its `--auth` token. Examples:

```bash
# Open the web UI (browser)         → http://<host>/         (admin / openhop)
# Pull live stats                   → curl -u admin:YOUR_PASSWORD http://<host>/api/stats
# Flash a new firmware over HTTP    → curl -u admin:YOUR_PASSWORD \
#       -F firmware=@firmware/<env>/firmware.bin http://<host>/update
# Flash over espota via PlatformIO  → pio run -e <env> -t upload \
#       --upload-port <host> --upload-flags="--auth=YOUR_PASSWORD"
```

The RAK4631 stores its password in a CRC-protected, power-loss-safe dual-slot
record in bootloader-preserved nRF52 configuration pages rather than ESP NVS
or LittleFS. Its authenticated RAK-only **Bluetooth DFU** control hands off to
the installed Nordic BLE bootloader after the HTTP response is acknowledged and
the Ethernet socket is closed; a separate Nordic-compatible DFU app uploads the
nRF52 `firmware.zip`. The bootloader's Bluetooth name is not fixed. Complete BLE
DFU uploads are validated on the production gateway; keep USB serial DFU
available for interrupted-transfer recovery. This Bluetooth DFU flow is the
supported OTA update path for deployed gateways. Ethernet/WebUI `/update` and
staged activation remain disabled because they would require a custom openHop
Modem bootloader, which is not currently planned.
RAK JSON responses expose `tcp_token_set`, never the token.

Pre-v0.8 firmware used `heltec:<tcp_token>` on `/update` only — that
scheme is gone, the same credential pair now covers every HTTP path.
