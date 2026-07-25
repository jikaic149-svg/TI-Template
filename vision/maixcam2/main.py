"""Standalone MaixCAM 2 VisionLink packet monitor."""

import time

from board_config import BOARD_PROFILE
from vision_link import VisionLink


POLL_INTERVAL_MS = 10


def sleep_ms(duration_ms):
    try:
        time.sleep_ms(duration_ms)
    except AttributeError:
        time.sleep(duration_ms / 1000.0)


def handle_packet(packet):
    packet_type, sequence, payload = packet
    print("Vision RX:", packet_type, sequence, payload)


def main():
    link = VisionLink(BOARD_PROFILE)
    link.open(force=True)
    while True:
        for packet in link.read_packets():
            handle_packet(packet)
        sleep_ms(POLL_INTERVAL_MS)


if __name__ == "__main__":
    main()
