"""Stable MaixCAM UART and packet interface for this application."""

import time as std_time


VISION_LINK_INTERFACE_VERSION = (1, 0)
VISION_WIRE_VERSION = 1


def now_seconds():
    try:
        return std_time.monotonic()
    except AttributeError:
        return std_time.time()


def now_ms():
    return int(now_seconds() * 1000.0)


def make_boot_session():
    value = (int(std_time.time() * 1000.0) ^
        int(now_seconds() * 1000000.0)) & 0xFFFFFFFF
    if value == 0:
        value = 1
    return "%08X" % value


def crc16_ccitt_false(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF \
                if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_packet(packet_type, sequence, payload):
    if not isinstance(packet_type, str) or not packet_type or \
            not 0 <= int(sequence) <= 0xFFFF:
        raise ValueError("invalid packet type or sequence")
    payload_bytes = payload.encode("ascii")
    prefix = "@%d|%s|%d|%d|" % (
        VISION_WIRE_VERSION, packet_type, sequence, len(payload_bytes))
    covered = prefix.encode("ascii") + payload_bytes + b"|"
    return covered + ("%04X\n" % crc16_ccitt_false(covered)).encode("ascii")


def parse_packet(line):
    if line.endswith(b"\r"):
        line = line[:-1]
    parts = line.split(b"|")
    version = ("@%d" % VISION_WIRE_VERSION).encode("ascii")
    if len(parts) != 6 or parts[0] != version:
        return None
    try:
        sequence = int(parts[2])
        payload_length = int(parts[3])
        packet_type = parts[1].decode("ascii")
        payload = parts[4].decode("ascii")
        received_crc = int(parts[5], 16)
    except Exception:
        return None
    covered = b"|".join(parts[:5]) + b"|"
    if not 0 <= sequence <= 0xFFFF or \
            payload_length != len(parts[4]) or \
            crc16_ccitt_false(covered) != received_crc:
        return None
    return packet_type, sequence, payload


class VisionBoardProfile:
    """Immutable board-owned UART binding consumed by VisionLink."""

    __slots__ = (
        "name", "uart_device", "baud_rate", "tx_pin", "rx_pin",
        "tx_function", "rx_function", "_locked",
    )

    def __init__(self, name, uart_device, baud_rate, tx_pin, rx_pin,
            tx_function, rx_function):
        values = (name, uart_device, tx_pin, rx_pin,
            tx_function, rx_function)
        if any(not isinstance(value, str) or not value for value in values) or \
                int(baud_rate) <= 0 or tx_pin == rx_pin:
            raise ValueError("invalid Vision board profile")
        object.__setattr__(self, "name", name)
        object.__setattr__(self, "uart_device", uart_device)
        object.__setattr__(self, "baud_rate", int(baud_rate))
        object.__setattr__(self, "tx_pin", tx_pin)
        object.__setattr__(self, "rx_pin", rx_pin)
        object.__setattr__(self, "tx_function", tx_function)
        object.__setattr__(self, "rx_function", rx_function)
        object.__setattr__(self, "_locked", True)

    def __setattr__(self, name, value):
        if getattr(self, "_locked", False):
            raise AttributeError("Vision board profile is immutable")
        object.__setattr__(self, name, value)


class VisionLink:
    """Owns pinmux, UART lifetime, framing, counters and telemetry sequence."""

    def __init__(self, profile, reopen_ms=2000, rx_limit=256):
        if not isinstance(profile, VisionBoardProfile) or \
                int(reopen_ms) < 0 or int(rx_limit) <= 0:
            raise ValueError("invalid Vision link configuration")
        self.profile = profile
        self.reopen_ms = int(reopen_ms)
        self.rx_limit = int(rx_limit)
        self.session = make_boot_session()
        self._port = None
        self._rx_buffer = bytearray()
        self._last_open_seconds = -1000000.0
        self._telemetry_sequence = 0
        self.rx_byte_count = 0
        self.valid_packet_count = 0
        self.rejected_line_count = 0
        self.last_packet_type = "NONE"

    @property
    def connected(self):
        return self._port is not None

    def open(self, force=False):
        if self._port is not None:
            return self._port
        current = now_seconds()
        if not force and \
                (current - self._last_open_seconds) * 1000.0 < self.reopen_ms:
            return None
        self._last_open_seconds = current
        try:
            from maix import pinmap, uart
            pinmap.set_pin_function(
                self.profile.tx_pin, self.profile.tx_function)
            pinmap.set_pin_function(
                self.profile.rx_pin, self.profile.rx_function)
            self._port = uart.UART(
                self.profile.uart_device, self.profile.baud_rate)
            print("Vision UART:", self.profile.name,
                self.profile.tx_pin, self.profile.rx_pin,
                self.profile.uart_device, self.profile.baud_rate)
        except Exception as error:
            self._port = None
            print("UART open failed:", error)
        return self._port

    def write_frame(self, frame):
        port = self.open()
        if port is None:
            return False
        try:
            return port.write(frame) == len(frame)
        except Exception as error:
            print("UART write failed:", error)
            self._port = None
            return False

    def read_packets(self):
        port = self.open()
        if port is None:
            return []
        try:
            received = port.read(self.rx_limit)
        except Exception as error:
            print("UART read failed:", error)
            self._port = None
            return []
        if received:
            self._rx_buffer.extend(received)
            self.rx_byte_count += len(received)
        packets = []
        while True:
            newline = self._rx_buffer.find(b"\n")
            if newline < 0:
                break
            line = bytes(self._rx_buffer[:newline])
            del self._rx_buffer[: newline + 1]
            packet = parse_packet(line)
            if packet is None:
                self.rejected_line_count += 1
            else:
                self.valid_packet_count += 1
                self.last_packet_type = packet[0]
                packets.append(packet)
        if len(self._rx_buffer) > 512:
            self._rx_buffer.clear()
        return packets

    def send_ack(self, sequence, confirmed_session):
        return self.write_frame(
            encode_packet("ACK", sequence, confirmed_session))

    def send_telemetry(self, packet_type, payload):
        sequence = self._telemetry_sequence
        self._telemetry_sequence = (sequence + 1) & 0xFFFF
        return self.write_frame(encode_packet(packet_type, sequence, payload))


class ReliableSender:
    """Owns one reliable queue; payload meaning remains in the vision app."""

    def __init__(self, link, retry_ms=250, queue_limit=20):
        if not isinstance(link, VisionLink) or int(retry_ms) <= 0 or \
                int(queue_limit) <= 0:
            raise ValueError("invalid reliable sender configuration")
        self.link = link
        self.retry_ms = int(retry_ms)
        self.queue_limit = int(queue_limit)
        self.sequence = 0
        self.queue = []
        self.pending = None

    def clear(self):
        self.queue = []
        self.pending = None

    def enqueue(self, packet_type, payload):
        if len(self.queue) >= self.queue_limit:
            return False
        self.queue.append((packet_type, payload))
        return True

    def has_type(self, packet_type):
        if self.pending is not None and self.pending["type"] == packet_type:
            return True
        return any(item[0] == packet_type for item in self.queue)

    def acknowledge(self, sequence, payload):
        if payload != self.link.session or self.pending is None:
            return False
        if sequence != self.pending["sequence"]:
            return False
        self.pending = None
        return True

    def service(self):
        if self.pending is None and self.queue:
            packet_type, payload = self.queue.pop(0)
            sequence = self.sequence
            self.sequence = (self.sequence + 1) & 0xFFFF
            self.pending = {
                "type": packet_type,
                "sequence": sequence,
                "frame": encode_packet(packet_type, sequence, payload),
                "last_send": -1000000000,
            }
        if self.pending is None:
            return
        current = now_ms()
        if current - self.pending["last_send"] >= self.retry_ms and \
                self.link.write_frame(self.pending["frame"]):
            self.pending["last_send"] = current

