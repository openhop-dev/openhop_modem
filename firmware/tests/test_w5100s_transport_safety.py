from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "w5100s_ethernet_transport.cpp"


class W5100sTransportSafetyTest(unittest.TestCase):
    def test_tcp_protocol_send_path_is_cooperative_and_bounded(self) -> None:
        text = SOURCE.read_text(encoding="utf-8")
        self.assertNotIn("client.write(", text)
        self.assertIn("TX_DEADLINE_MS", text)
        self.assertIn("W5100.readSnTX_FSR(socket)", text)
        self.assertIn("W5100.writeSnCR(socket, Sock_SEND)", text)
        self.assertIn("SnIR::SEND_OK", text)
        self.assertIn("SnIR::TIMEOUT", text)
        self.assertIn("RX_BYTES_PER_LOOP", text)
        self.assertIn(
            "while (processed < RX_BYTES_PER_LOOP && client.available() && !txBusy())",
            text,
        )
        self.assertGreaterEqual(text.count("if (client || txBusy()) disconnectClient();"), 2)


if __name__ == "__main__":
    unittest.main()
