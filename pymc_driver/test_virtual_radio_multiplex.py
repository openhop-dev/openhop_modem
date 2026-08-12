"""Host-side contract tests for the virtual-radio scheduling policy.

The firmware scheduler is cooperative C++ and is not directly linkable into
the Python test runner.  This file therefore models only the externally
observable policy: four consecutive ports, unchanged framing, one-deep
generation-tagged queues, RX fan-out for matching radio profiles, TX
priority/fairness, CAD hold, and timeout classification.  Keeping these tests
independent of sockets makes them fast enough to run in CI while the live
hardware checklist remains separate.
"""

import importlib.util
import re
import unittest
from pathlib import Path


# Load the framing helpers without importing pymc_driver.__init__.py.  The
# package initializer imports the optional serial transport, while these
# contract tests intentionally need no hardware dependencies.
_PROTOCOL_PATH = Path(__file__).with_name("protocol_constants.py")
_PROTOCOL_SPEC = importlib.util.spec_from_file_location(
    "virtual_radio_protocol_constants", _PROTOCOL_PATH
)
if _PROTOCOL_SPEC is None or _PROTOCOL_SPEC.loader is None:
    raise ImportError(f"cannot load protocol constants from {_PROTOCOL_PATH}")
_PROTOCOL = importlib.util.module_from_spec(_PROTOCOL_SPEC)
_PROTOCOL_SPEC.loader.exec_module(_PROTOCOL)

CMD_TX_REQUEST = _PROTOCOL.CMD_TX_REQUEST
PROTO_SYNC = _PROTOCOL.PROTO_SYNC
build_frame = _PROTOCOL.build_frame
crc16_ccitt = _PROTOCOL.crc16_ccitt


class VirtualRadioSchedulerModel:
    """Small reference model for the firmware's externally visible policy."""

    SLOT_COUNT = 4

    def __init__(self, base_port=5055, rx_slot_ms=100, activity_hold_ms=2000,
                 tx_echo_hold_multiplier=1):
        self.base_port = 5055 if base_port == 0 else base_port
        if self.base_port < 1 or self.base_port + self.SLOT_COUNT - 1 > 0xFFFF:
            raise ValueError("base port cannot allocate four listeners")
        if rx_slot_ms < 5:
            raise ValueError("rx slot dwell must be at least 5 ms")
        if activity_hold_ms < 0:
            raise ValueError("activity hold must be non-negative")
        if tx_echo_hold_multiplier < 0:
            raise ValueError("TX echo hold multiplier must be non-negative")
        self.rx_slot_ms = rx_slot_ms
        self.activity_hold_ms = activity_hold_ms
        self.tx_echo_hold_multiplier = tx_echo_hold_multiplier
        self.generation = [0] * self.SLOT_COUNT
        self.connected = [False] * self.SLOT_COUNT
        self.standby = [False] * self.SLOT_COUNT
        self.radio_settings = [None] * self.SLOT_COUNT
        self.pending = [None] * self.SLOT_COUNT
        self.next_tx_slot = 0
        self.operation = "RX_SLOT"
        self.tx_owner = None
        self.cad_owner = None
        self.hold_until = 0
        self.recent_rx = {}
        self.last_transmitted = None
        self.suppressed_rx_count = 0

    def port_for_slot(self, slot):
        if not 0 <= slot < self.SLOT_COUNT:
            raise IndexError(slot)
        return self.base_port + slot

    def disconnect(self, slot):
        self.connected[slot] = False
        self.generation[slot] += 1

    def connect(self, slot):
        self.connected[slot] = True
        self.generation[slot] += 1

    def set_radio_settings(self, slot, settings):
        self.radio_settings[slot] = tuple(settings)

    def set_standby(self, slot, standby=True):
        self.standby[slot] = standby

    @staticmethod
    def _receive_settings(settings):
        return (settings[0], settings[1], settings[2], settings[3], settings[5])

    def receive_targets(self, active_slot):
        """Return ready clients using the radio profile currently on air."""
        if not self.connected[active_slot] or self.standby[active_slot]:
            return []
        active_settings = self.radio_settings[active_slot]
        return [
            slot for slot in range(self.SLOT_COUNT)
            if self.connected[slot] and not self.standby[slot] and
            self._receive_settings(self.radio_settings[slot]) ==
            self._receive_settings(active_settings)
        ]

    def receive_mirrors(self, slot):
        """Return other slots sharing only the receive-relevant settings."""
        if not self.connected[slot] or self.standby[slot]:
            return []
        receive_settings = self._receive_settings(self.radio_settings[slot])
        return [
            other for other in range(self.SLOT_COUNT)
            if other != slot
            and self.connected[other]
            and not self.standby[other]
            and self._receive_settings(self.radio_settings[other]) == receive_settings
        ]

    def fanout_received_packet(self, active_slot, payload, now_ms):
        """Deliver each packet/profile pair to each slot at most once per TTL."""
        targets = self.receive_targets(active_slot)
        if not targets:
            return []

        profile = self._receive_settings(self.radio_settings[active_slot])
        key = (profile, bytes(payload))
        entry = self.recent_rx.get(key)
        if entry is None or now_ms - entry[0] >= 5000:
            entry = (now_ms, set())
            self.recent_rx[key] = entry

        seen_at, delivered = entry
        delivered_now = [slot for slot in targets if slot not in delivered]
        delivered.update(delivered_now)
        self.recent_rx[key] = (now_ms, delivered)
        return delivered_now

    def next_receive_slot(self, start):
        """Return the next connected slot, skipping unused listeners."""
        for offset in range(self.SLOT_COUNT):
            slot = (start + offset) % self.SLOT_COUNT
            if self.connected[slot]:
                return slot
        return None

    def enqueue(self, slot, payload):
        if not self.connected[slot]:
            return False
        active_owner = (
            self.operation == "TX" and self.tx_owner == slot
        ) or (
            self.operation == "CAD_FOR_TX" and self.cad_owner == slot
        )
        if active_owner:
            return False
        queued = self.pending[slot]
        if queued is not None and queued[0] != self.generation[slot]:
            self.pending[slot] = None
        if self.pending[slot] is not None:
            return False
        self.pending[slot] = (self.generation[slot], payload)
        return True

    def next_pending(self, now=None):
        if now is not None and not self.rotation_allowed(now):
            return None
        for offset in range(self.SLOT_COUNT):
            slot = (self.next_tx_slot + offset) % self.SLOT_COUNT
            if not self.connected[slot]:
                continue
            queued = self.pending[slot]
            if queued is None:
                continue
            if queued[0] != self.generation[slot]:
                self.pending[slot] = None
                continue
            return slot
        return None

    def start_tx(self, slot):
        self.operation = "TX"
        self.tx_owner = slot

    def finish_tx(self):
        slot = self.tx_owner
        self.pending[slot] = None
        self.next_tx_slot = (slot + 1) % self.SLOT_COUNT
        self.tx_owner = None
        self.operation = "RX_SLOT"

    def start_cad(self, operation, owner):
        if self.operation != "RX_SLOT":
            return "BUSY"
        self.operation = operation
        self.cad_owner = owner
        return "STARTED"

    def finish_cad(self, result):
        self.operation = "RX_SLOT"
        self.cad_owner = None
        return result

    def hold_activity(self, now, duration):
        self.hold_until = now + duration

    def rotation_allowed(self, now):
        return now >= self.hold_until

    def remember_transmitted(self, payload, completed_ms):
        self.last_transmitted = (bytes(payload), completed_ms)

    def accepts_received_packet(self, payload, now_ms):
        if self.last_transmitted is None:
            return True
        sent_payload, completed_ms = self.last_transmitted
        window = self.activity_hold_ms * self.tx_echo_hold_multiplier
        suppressed = (
            window > 0
            and bytes(payload) == sent_payload
            and now_ms - completed_ms < window
        )
        if suppressed:
            self.suppressed_rx_count += 1
        return not suppressed


class VirtualRadioMultiplexContractTests(unittest.TestCase):
    def test_default_ports_and_slot_zero_compatibility(self):
        scheduler = VirtualRadioSchedulerModel()
        self.assertEqual([scheduler.port_for_slot(i) for i in range(4)],
                         [5055, 5056, 5057, 5058])

    def test_custom_base_port_and_overflow_validation(self):
        scheduler = VirtualRadioSchedulerModel(60000)
        self.assertEqual(scheduler.port_for_slot(3), 60003)
        with self.assertRaises(ValueError):
            VirtualRadioSchedulerModel(65533)

    def test_runtime_scheduler_defaults_and_validation(self):
        scheduler = VirtualRadioSchedulerModel()
        self.assertEqual(scheduler.rx_slot_ms, 100)
        self.assertEqual(scheduler.activity_hold_ms, 2000)
        self.assertEqual(scheduler.tx_echo_hold_multiplier, 1)
        configured = VirtualRadioSchedulerModel(
            5055, rx_slot_ms=5, activity_hold_ms=0,
            tx_echo_hold_multiplier=3,
        )
        self.assertEqual(configured.rx_slot_ms, 5)
        self.assertEqual(configured.activity_hold_ms, 0)
        self.assertEqual(configured.tx_echo_hold_multiplier, 3)
        with self.assertRaises(ValueError):
            VirtualRadioSchedulerModel(rx_slot_ms=4)

    def test_wire_frame_is_unchanged(self):
        payload = b"radio-payload"
        frame = build_frame(CMD_TX_REQUEST, payload)
        self.assertEqual(frame[:4], bytes([PROTO_SYNC, CMD_TX_REQUEST,
                                           len(payload), 0]))
        self.assertEqual(frame[4:-2], payload)
        self.assertEqual(frame[-2:], crc16_ccitt(frame[1:-2]).to_bytes(2, "little"))

    def test_one_deep_queue_and_round_robin_fairness(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(0)
        scheduler.connect(1)
        self.assertTrue(scheduler.enqueue(0, b"zero-1"))
        self.assertFalse(scheduler.enqueue(0, b"zero-2"))
        self.assertTrue(scheduler.enqueue(1, b"one-1"))
        self.assertEqual(scheduler.next_pending(), 0)
        scheduler.start_tx(0)
        scheduler.finish_tx()
        self.assertEqual(scheduler.next_pending(), 1)

    def test_tx_has_priority_over_rx_rotation(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(2)
        self.assertTrue(scheduler.enqueue(2, b"urgent"))
        self.assertEqual(scheduler.next_pending(), 2)
        scheduler.start_tx(2)
        self.assertEqual(scheduler.operation, "TX")
        scheduler.finish_tx()
        self.assertEqual(scheduler.operation, "RX_SLOT")

    def test_pending_tx_waits_until_activity_hold_expires(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(0)
        scheduler.connect(1)
        scheduler.hold_activity(now=1000, duration=2000)
        self.assertTrue(scheduler.enqueue(1, b"forward-after-rx"))

        self.assertIsNone(scheduler.next_pending(now=2999))
        self.assertEqual(scheduler.next_pending(now=3000), 1)

    def test_exact_last_tx_reflection_is_not_counted_during_scaled_hold(self):
        scheduler = VirtualRadioSchedulerModel(
            activity_hold_ms=2000, tx_echo_hold_multiplier=2
        )
        scheduler.remember_transmitted(b"last-tx", completed_ms=1000)

        self.assertFalse(scheduler.accepts_received_packet(b"last-tx", now_ms=4999))
        self.assertEqual(scheduler.suppressed_rx_count, 1)
        self.assertTrue(scheduler.accepts_received_packet(b"last-tx", now_ms=5000))
        self.assertTrue(scheduler.accepts_received_packet(b"different", now_ms=1100))
        self.assertEqual(scheduler.suppressed_rx_count, 1)

    def test_zero_tx_echo_multiplier_disables_reflection_suppression(self):
        scheduler = VirtualRadioSchedulerModel(tx_echo_hold_multiplier=0)
        scheduler.remember_transmitted(b"last-tx", completed_ms=1000)
        self.assertTrue(scheduler.accepts_received_packet(b"last-tx", now_ms=1001))

    def test_cad_activity_hold_blocks_rotation_until_deadline(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.hold_activity(now=1000, duration=2000)
        self.assertFalse(scheduler.rotation_allowed(2999))
        self.assertTrue(scheduler.rotation_allowed(3000))

    def test_cad_busy_is_distinct_from_start_failure_and_timeout(self):
        scheduler = VirtualRadioSchedulerModel()
        self.assertEqual(scheduler.start_cad("CAD_FOR_HOST", 0), "STARTED")
        self.assertEqual(scheduler.start_cad("CAD_FOR_HOST", 1), "BUSY")
        self.assertEqual(scheduler.finish_cad("TIMEOUT"), "TIMEOUT")
        self.assertEqual(scheduler.start_cad("CAD_FOR_HOST", 1), "STARTED")
        self.assertEqual(scheduler.finish_cad("START_FAILED"), "START_FAILED")

    def test_disconnect_drops_stale_queue_and_allows_generation_reuse(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(1)
        self.assertTrue(scheduler.enqueue(1, b"old-client"))
        scheduler.disconnect(1)
        self.assertIsNone(scheduler.next_pending())
        scheduler.connect(1)
        self.assertTrue(scheduler.enqueue(1, b"new-client"))
        self.assertEqual(scheduler.pending[1][1], b"new-client")

    def test_disconnect_during_tx_does_not_accept_reused_slot_until_done(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(2)
        self.assertTrue(scheduler.enqueue(2, b"in-flight"))
        scheduler.start_tx(2)
        scheduler.disconnect(2)
        self.assertFalse(scheduler.enqueue(2, b"replacement"))
        scheduler.finish_tx()
        scheduler.connect(2)
        self.assertTrue(scheduler.enqueue(2, b"replacement"))

    def test_receive_rotation_skips_unconnected_slots(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(1)
        scheduler.connect(3)

        self.assertEqual(scheduler.next_receive_slot(0), 1)
        self.assertEqual(scheduler.next_receive_slot(2), 3)
        self.assertEqual(scheduler.next_receive_slot(0), 1)

        scheduler.disconnect(1)
        self.assertEqual(scheduler.next_receive_slot(0), 3)
        scheduler.disconnect(3)
        self.assertIsNone(scheduler.next_receive_slot(0))

    def test_received_packet_fans_out_to_matching_radio_profiles(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(0)
        scheduler.connect(1)
        scheduler.connect(2)
        scheduler.set_radio_settings(0, (869618000, 62500, 8, 8, 22, 0x12, 16))
        scheduler.set_radio_settings(1, (869618000, 62500, 8, 8, 22, 0x12, 16))
        scheduler.set_radio_settings(2, (869618000, 62500, 7, 8, 22, 0x12, 16))

        self.assertEqual(scheduler.receive_targets(0), [0, 1])

    def test_received_packet_is_not_repeated_to_slots_after_echo(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(0)
        scheduler.connect(1)
        settings = (869618000, 62500, 8, 8, 22, 0x12, 16)
        scheduler.set_radio_settings(0, settings)
        scheduler.set_radio_settings(1, settings)

        self.assertEqual(
            scheduler.fanout_received_packet(0, b"packet", now_ms=1000),
            [0, 1],
        )
        # A client echo/reception of the same payload must not fan out again.
        self.assertEqual(
            scheduler.fanout_received_packet(0, b"packet", now_ms=1100),
            [],
        )

    def test_received_packet_can_be_delivered_again_after_duplicate_ttl(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(0)
        scheduler.connect(1)
        settings = (869618000, 62500, 8, 8, 22, 0x12, 16)
        scheduler.set_radio_settings(0, settings)
        scheduler.set_radio_settings(1, settings)

        scheduler.fanout_received_packet(0, b"packet", now_ms=1000)
        self.assertEqual(
            scheduler.fanout_received_packet(0, b"packet", now_ms=6000),
            [0, 1],
        )

    def test_receive_mirroring_ignores_tx_power_and_preamble(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(0)
        scheduler.connect(1)
        scheduler.set_radio_settings(0, (869618000, 62500, 8, 8, 22, 0x12, 16))
        scheduler.set_radio_settings(1, (869618000, 62500, 8, 8, 14, 0x12, 32))

        self.assertEqual(scheduler.receive_mirrors(0), [1])
        self.assertEqual(scheduler.receive_mirrors(1), [0])

    def test_receive_mirroring_excludes_self_and_standby_slots(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(0)
        scheduler.connect(1)
        scheduler.connect(2)
        settings = (869618000, 62500, 8, 8, 22, 0x12, 16)
        for slot in (0, 1, 2):
            scheduler.set_radio_settings(slot, settings)
        scheduler.set_standby(2)

        self.assertEqual(scheduler.receive_mirrors(0), [1])
        self.assertEqual(scheduler.receive_mirrors(1), [0])
        self.assertEqual(scheduler.receive_mirrors(2), [])

    def test_received_packet_does_not_cross_radio_profiles(self):
        scheduler = VirtualRadioSchedulerModel()
        scheduler.connect(0)
        scheduler.connect(1)
        scheduler.set_radio_settings(0, (869618000, 62500, 8, 8, 22, 0x12, 16))
        scheduler.set_radio_settings(1, (868000000, 125000, 7, 5, 14, 0x34, 8))

        self.assertEqual(scheduler.receive_targets(0), [0])

    def test_stats_page_uses_one_stable_setting_order_for_every_slot(self):
        ota_manager = Path(__file__).parents[1] / "firmware" / "src" / "ota_manager.cpp"
        source = ota_manager.read_text()
        table_start = source.index("if (snap.virtualSlotCount > 0)")
        table_end = source.index("} else {", table_start)
        rows = re.findall(
            r'appendVirtualSlotTableRow\(body, snap, "([^"]+)"',
            source[table_start:table_end],
        )
        self.assertEqual(
            rows,
            [
                "Status",
                "TCP port",
                "Client",
                "Frequency",
                "Bandwidth",
                "Spreading factor",
                "Coding rate",
                "TX power",
                "Syncword",
                "Preamble",
                "Auto CAD",
                "CAD settings",
                "CAD symbols",
                "CAD detection peak",
                "CAD detection minimum",
                "CAD exit mode",
                "Receive mirroring",
            ],
        )

    def test_stats_json_contract_names_receive_mirroring_field(self):
        ota_manager = Path(__file__).parents[1] / "firmware" / "src" / "ota_manager.cpp"
        source = ota_manager.read_text()
        self.assertIn('\\"recieve_mirroing\\"', source)


    def test_config_exposes_tx_echo_hold_multiplier(self):
        firmware = Path(__file__).parents[1] / "firmware"
        ota_source = (firmware / "src" / "ota_manager.cpp").read_text()
        portal_source = (firmware / "src" / "config_portal.cpp").read_text()
        config_header = (firmware / "include" / "wifi_manager.h").read_text()
        self.assertIn("tx_echo_hold_multiplier", ota_source)
        self.assertIn("tx_echo_hold_multiplier", portal_source)
        self.assertIn("txEchoHoldMultiplier = 1", config_header)

    def test_counters_expose_suppressed_rx(self):
        ota_manager = Path(__file__).parents[1] / "firmware" / "src" / "ota_manager.cpp"
        source = ota_manager.read_text()
        self.assertIn('\\"suppressed_rx\\"', source)
        self.assertIn("Suppressed RX", source)


if __name__ == "__main__":
    unittest.main()
