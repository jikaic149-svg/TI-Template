"""Host tests for the standalone MaixCAM 2 application."""

import unittest

from board_config import BOARD_PROFILE
from vision_link import (
    VISION_LINK_INTERFACE_VERSION,
    VISION_WIRE_VERSION,
    VisionBoardProfile,
    crc16_ccitt_false,
    encode_packet,
    parse_packet,
)


class VisionApplicationTest(unittest.TestCase):
    def test_fixed_versions(self):
        self.assertEqual((1, 0), VISION_LINK_INTERFACE_VERSION)
        self.assertEqual(1, VISION_WIRE_VERSION)

    def test_packet_round_trip(self):
        frame = encode_packet("CONTEXT", 42, "1234ABCD,2,4,7,3")
        self.assertEqual(
            ("CONTEXT", 42, "1234ABCD,2,4,7,3"),
            parse_packet(frame.rstrip(b"\n")),
        )

    def test_bad_crc_is_rejected(self):
        frame = bytearray(encode_packet("START", 1, "1234ABCD"))
        frame[-3] = ord("0") if frame[-3] != ord("0") else ord("1")
        self.assertIsNone(parse_packet(bytes(frame).rstrip(b"\n")))

    def test_known_crc_vector(self):
        self.assertEqual(0x29B1, crc16_ccitt_false(b"123456789"))

    def test_board_profile_is_immutable(self):
        with self.assertRaises(AttributeError):
            BOARD_PROFILE.tx_pin = "A16"

    def test_board_uart_binding(self):
        self.assertIsInstance(BOARD_PROFILE, VisionBoardProfile)
        self.assertEqual(("B0", "B1"),
            (BOARD_PROFILE.tx_pin, BOARD_PROFILE.rx_pin))
        self.assertEqual(("UART2_TX", "UART2_RX"),
            (BOARD_PROFILE.tx_function, BOARD_PROFILE.rx_function))
        self.assertEqual("/dev/ttyS2", BOARD_PROFILE.uart_device)
        self.assertEqual(115200, BOARD_PROFILE.baud_rate)


if __name__ == "__main__":
    unittest.main()

