"""Host-side protocol tests for the ARM7 RTCom driver.

Run from the repository root:

    python3 -I -m unittest discover -s tools -v

IMPORTANT: These tests do NOT execute the embedded ARM7 code.  They are a
host-side reimplementation that mirrors the register-level protocol decisions
made in arm7/source/rtcom_driver.c, so the design can be checked without
hardware.  The embedded code itself uses direct MMIO and cannot run on a host.
"""

import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


# Bit/pin layout copied from arm7/source/rtcom_driver.c and verified against
# Calico's calico/nds/arm7/gpio.h:
#   bit 0 = SIO, bit 1 = SCK, bit 2 = CS,
#   bit 4 = SIO_OUT, bit 5 = SCK_OUT, bit 6 = CS_OUT.
PIN_SIO = 0x0001
PIN_SCK = 0x0002
PIN_CS  = 0x0004
PIN_SIO_OUT = 0x0010
PIN_SCK_OUT = 0x0020
PIN_CS_OUT  = 0x0040
DIR_OUT = PIN_SIO_OUT | PIN_SCK_OUT | PIN_CS_OUT

RTC_WRITE_112 = 0x6C
RTC_WRITE_113 = 0x6E
RTC_READ_112 = 0x6D
RTC_READ_113 = 0x6F

# Reference-pinning table.  These opcode/status VALUES are hardware facts
# cited from the reference implementation at
#   $HOME/3ds/RtcPwalker/arm7_rtcom_patch/arm7/source/rtcom.h
# (GPLv3).  Only the numeric values are reused here; the names and this test
# are original to TWL-HD.
EXPECTED_DEFINES = {
    # Raw probe literal (not a named service opcode).
    "RTCOM_REQ_PROBE": 0x01,

    # Service opcodes.
    "RTCOM_OP_KIL": 0x00,
    "RTCOM_OP_UPLOAD_UCODE": 0x41,
    "RTCOM_OP_FINISH_UCODE": 0x42,
    "RTCOM_OP_EXECUTE_UCODE": 0x44,
    "RTCOM_OP_NEXT": 0x81,
    "RTCOM_OP_KEEPALIVE": 0x83,
    "RTCOM_OP_SYNC_KIL": 0xFE,

    # Status bytes.
    "RTCOM_STATUS_READY": 0x00,
    "RTCOM_STATUS_ACK": 0x80,
    "RTCOM_STATUS_DONE": 0x82,
}


def _header_path():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "..", "arm7", "include", "rtcom_driver.h")


def _parse_header_defines(path):
    defines = {}
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r"^\s*#define\s+(RTCOM_\w+)\s+(0x[0-9A-Fa-f]+|[0-9]+)u?\s*$",
                         line)
            if m:
                defines[m.group(1)] = int(m.group(2), 0)
    return defines


def emulate_bitbang_byte(cmd, value, fast_mode=False):
    """Emulate one rtcom_write_byte() transfer and return the register writes.

    The implementation mirrors the fixed-up logic in rtcom_driver.c:
      - MSB-first for both command and data bytes
      - pin mapping per Calico/reference rtcom.cpp
      - clock pattern: low -> high per bit, CS ends low
    """
    delay = 14 if fast_mode else 48
    writes = []
    reg = 0

    def write(v):
        reg = v
        writes.append(reg)
        return reg

    def delay_step():
        pass

    def begin():
        nonlocal reg
        base = (reg & ~(DIR_OUT | PIN_CS | PIN_SCK | PIN_SIO)) | DIR_OUT
        reg = write(base | DIR_OUT | PIN_CS | PIN_SCK | PIN_SIO)
        delay_step()

    def end():
        nonlocal reg
        reg = write((reg & ~PIN_CS) | DIR_OUT | PIN_SCK)
        delay_step()

    def put_bit(bit):
        nonlocal reg
        line = (reg & ~(PIN_SIO | PIN_SCK)) | DIR_OUT | PIN_CS
        if bit:
            line |= PIN_SIO
        reg = write(line)
        delay_step()
        reg = write(line | PIN_SCK)
        delay_step()

    begin()

    for i in range(7, -1, -1):
        put_bit((cmd >> i) & 1)

    for i in range(7, -1, -1):
        put_bit((value >> i) & 1)

    end()
    return writes, delay


def emulate_bitbang_read_byte(cmd, returned_value, fast_mode=False):
    """Emulate one rtcom_read_byte() transfer and return the register writes.

    Mirrors the corrected logic in rtcom_driver.c:
      - command sent MSB-first with SIO in output mode (SIO_OUT set)
      - 8 data bits sampled MSB-first with SIO_OUT clear, because the RTC
        chip drives SIO during the read half of each transaction
    """
    delay = 14 if fast_mode else 48
    writes = []
    reg = 0

    def write(v):
        reg = v
        writes.append(reg)
        return reg

    def delay_step():
        pass

    def begin():
        nonlocal reg
        base = (reg & ~(DIR_OUT | PIN_CS | PIN_SCK | PIN_SIO)) | DIR_OUT
        reg = write(base | DIR_OUT | PIN_CS | PIN_SCK | PIN_SIO)
        delay_step()

    def end():
        nonlocal reg
        reg = write((reg & ~PIN_CS) | DIR_OUT | PIN_SCK)
        delay_step()

    def put_bit(bit):
        nonlocal reg
        line = (reg & ~(PIN_SIO | PIN_SCK)) | DIR_OUT | PIN_CS
        if bit:
            line |= PIN_SIO
        reg = write(line)
        delay_step()
        reg = write(line | PIN_SCK)
        delay_step()

    def get_bit(bit_from_rtc):
        nonlocal reg
        line = (reg & ~(PIN_SIO | PIN_SCK | PIN_SIO_OUT)) | PIN_CS_OUT | PIN_CS | PIN_SCK_OUT
        reg = write(line)
        delay_step()
        reg = write(line | PIN_SCK)
        delay_step()
        # The C code reads REG_RTC here; the written value keeps SIO_OUT clear.
        return bit_from_rtc

    begin()

    for i in range(7, -1, -1):
        put_bit((cmd >> i) & 1)

    out = 0
    for i in range(7, -1, -1):
        out = (out << 1) | get_bit((returned_value >> i) & 1)

    end()
    return writes, delay, out


class BitSerializationTest(unittest.TestCase):
    """Verify the CS/SCK/SIO write sequence for a known command/data pair."""

    def test_pin_mapping_and_msb_first(self):
        writes, _ = emulate_bitbang_byte(RTC_WRITE_112, 0xA5)

        # First write after begin() must have CS, SCK, SIO high and all
        # direction bits set.
        self.assertEqual(writes[0] & DIR_OUT, DIR_OUT,
                         "direction bits 4/5/6 must be set")
        self.assertTrue(writes[0] & PIN_CS, "CS must be high during transfer")
        self.assertTrue(writes[0] & PIN_SCK, "SCK starts high")
        self.assertTrue(writes[0] & PIN_SIO, "SIO starts high")

        # Extract the data-line values for the 16 bit cells (command then data).
        # Each bit cell is two writes: clock-low then clock-high.
        bit_cells = []
        i = 1
        while i < len(writes) - 1:
            low = writes[i]
            high = writes[i + 1]
            self.assertEqual(high, low | PIN_SCK,
                             "clock must rise between the two writes")
            bit_cells.append(1 if low & PIN_SIO else 0)
            i += 2

        self.assertEqual(len(bit_cells), 16, "expected 16 bit cells")

        # Command byte is shifted MSB-first.
        command_bits = bit_cells[:8]
        self.assertEqual(
            command_bits,
            [(RTC_WRITE_112 >> i) & 1 for i in range(7, -1, -1)],
            "command byte must be MSB-first")

        # Data byte is also shifted MSB-first (this was a prior defect: data
        # was previously LSB-first).
        data_bits = bit_cells[8:]
        self.assertEqual(
            data_bits,
            [(0xA5 >> i) & 1 for i in range(7, -1, -1)],
            "data byte must be MSB-first")

        # Final write must drop CS while keeping SCK high.
        final = writes[-1]
        self.assertFalse(final & PIN_CS, "CS must be low at end")
        self.assertTrue(final & PIN_SCK, "SCK must remain high at end")
        self.assertEqual(final & DIR_OUT, DIR_OUT,
                         "direction bits must remain outputs at end")


class RegisterOrderTest(unittest.TestCase):
    """Verify register 112 receives the parameter and register 113 the request.

    The reference rtcom_requestAsync(u8 request, u8 param) writes param to
    register 112 and request to register 113, with 113 written last.
    """

    def test_param_to_112_request_to_113(self):
        request = 0x42
        param = 0x37

        writes_112, _ = emulate_bitbang_byte(RTC_WRITE_112, param)
        writes_113, _ = emulate_bitbang_byte(RTC_WRITE_113, request)

        def byte_from_cells(writes, cell_start, cell_end):
            """Decode a byte from the clock-low SIO values of bit cells."""
            bits = []
            for low, high in zip(writes[1::2], writes[2::2]):
                bits.append(1 if low & PIN_SIO else 0)
            selected = bits[cell_start:cell_end]
            self.assertEqual(len(selected), 8)
            val = 0
            for b in selected:
                val = (val << 1) | b
            return val

        # The first eight bit cells encode the command byte, the next eight
        # encode the payload byte.
        self.assertEqual(byte_from_cells(writes_112, 0, 8), RTC_WRITE_112)
        self.assertEqual(byte_from_cells(writes_113, 0, 8), RTC_WRITE_113)
        self.assertEqual(byte_from_cells(writes_112, 8, 16), param)
        self.assertEqual(byte_from_cells(writes_113, 8, 16), request)


class ReadPathDirectionTest(unittest.TestCase):
    """Verify SIO_OUT is clear while sampling each bit of a read transaction."""

    def test_sio_out_clear_during_read_data_phase(self):
        returned = 0xA5
        writes, _, decoded = emulate_bitbang_read_byte(RTC_READ_113, returned)

        self.assertEqual(decoded, returned, "read byte must match RTC return value")

        # begin() is one write; command phase is 16 writes; data phase is 16
        # writes; end() is one write.
        data_writes = writes[1 + 16:1 + 16 + 16]
        self.assertEqual(len(data_writes), 16,
                         "expected 16 writes during 8-bit data sampling")

        for i, w in enumerate(data_writes):
            self.assertFalse(
                w & PIN_SIO_OUT,
                f"SIO_OUT must be clear during read sample {i // 2} "
                f"(write #{i}, value=0x{w:04x})")


class TimeoutStateMachineTest(unittest.TestCase):
    """Verify that rtcom_poll_ack() returns RTCOM_ERR_TIMEOUT after N polls."""

    def test_timeout_after_exhaustion(self):
        RTCOM_OK = 0
        RTCOM_ERR_TIMEOUT = 1

        def poll_ack(timeout_iters, irq_always_clear):
            for _ in range(timeout_iters):
                if irq_always_clear:
                    continue
                return RTCOM_OK
            return RTCOM_ERR_TIMEOUT

        # Tiny timeout to keep the test fast; the real constant is ~2 million.
        self.assertEqual(poll_ack(5, irq_always_clear=True), RTCOM_ERR_TIMEOUT)
        self.assertEqual(poll_ack(5, irq_always_clear=False), RTCOM_OK)


class ReferencePinningTest(unittest.TestCase):
    """Pin the opcode/status constants to the values observed in the reference.

    The expected values are literal facts from
        $HOME/3ds/RtcPwalker/arm7_rtcom_patch/arm7/source/rtcom.h
    If arm7/include/rtcom_driver.h drifts from these values the protocol will
    not match the real TwlBg RTCom service and this test fails.
    """

    def test_rtcom_constants_match_reference(self):
        header_path = _header_path()
        defines = _parse_header_defines(header_path)

        for name, expected in EXPECTED_DEFINES.items():
            self.assertIn(
                name, defines,
                f"{header_path} must define {name}")
            self.assertEqual(
                defines[name], expected,
                f"{name} must be 0x{expected:02X}; "
                f"saw 0x{defines[name]:02X}")


IRQ_NETWORK = 0x80  # libnds IRQ_RTC / IRQ_NETWORK = bit 7
RCNT_REFERENCE_VALUE = 0x8100  # RCNT_MODE_GPIO | RCNT_SI_IRQ_ENABLE
RTCOM_ACK_TIMEOUT = 2062500


class UploadRetryPolicyTest(unittest.TestCase):
    """Model-level check of the upload retry policy.

    The embedded driver cannot run on a host, but this test mirrors the exact
    decision made in rtcom_upload_ucode(): individual steps are NOT retried.
    A single failed step aborts the entire attempt so the next attempt can
    restart cleanly from UPLOAD_UCODE.  Resending one step after the service
    has already shifted its receive cursor would corrupt the blob position.
    """

    def _attempt_upload(self, blob, len_bytes, send_request, wait_done):
        """One upload attempt matching rtcom_attempt_upload()."""
        steps = [(EXPECTED_DEFINES["RTCOM_OP_UPLOAD_UCODE"], len_bytes[0])]
        steps.extend((EXPECTED_DEFINES["RTCOM_OP_NEXT"], b) for b in len_bytes[1:])
        steps.extend((EXPECTED_DEFINES["RTCOM_OP_NEXT"], b) for b in blob)

        for opcode, param in steps:
            if send_request(opcode, param) != 0:
                return 1  # abort whole attempt on first failed step

        if wait_done(RTCOM_ACK_TIMEOUT) != 0:
            return 1

        if send_request(EXPECTED_DEFINES["RTCOM_OP_FINISH_UCODE"], None) != 0:
            return 1

        return 0

    def test_failed_step_aborts_whole_attempt_no_resend(self):
        blob = [0xAA, 0xBB]
        len_bytes = [0x02, 0x00, 0x00, 0x00]
        log = []

        def send_request(opcode, param):
            log.append((opcode, param))
            # Fail on the first payload byte (5th step overall).
            if len(log) == 5:
                return 1
            return 0

        def wait_done(_timeout):
            return 0

        result = self._attempt_upload(blob, len_bytes, send_request, wait_done)

        self.assertNotEqual(result, 0)
        # The attempt must stop at the failed step; no retry of that step.
        self.assertEqual(
            log,
            [
                (EXPECTED_DEFINES["RTCOM_OP_UPLOAD_UCODE"], len_bytes[0]),
                (EXPECTED_DEFINES["RTCOM_OP_NEXT"], len_bytes[1]),
                (EXPECTED_DEFINES["RTCOM_OP_NEXT"], len_bytes[2]),
                (EXPECTED_DEFINES["RTCOM_OP_NEXT"], len_bytes[3]),
                (EXPECTED_DEFINES["RTCOM_OP_NEXT"], blob[0]),  # failure here
            ],
        )

    def test_outer_retry_restarts_from_upload_ucode(self):
        """Exhausting one attempt must let the next attempt restart cleanly."""
        blob = [0xCC]
        len_bytes = [0x01, 0x00, 0x00, 0x00]
        attempts = []

        def send_request(opcode, param):
            attempts[-1].append((opcode, param))
            # Fail the first attempt at the first payload byte; second succeeds.
            if len(attempts) == 1 and len(attempts[-1]) == 5:
                return 1
            return 0

        def wait_done(_timeout):
            return 0

        for _ in range(2):
            attempts.append([])
            if self._attempt_upload(blob, len_bytes, send_request, wait_done) == 0:
                break

        self.assertEqual(len(attempts), 2)
        self.assertEqual(
            attempts[0][0],
            (EXPECTED_DEFINES["RTCOM_OP_UPLOAD_UCODE"], len_bytes[0]),
        )
        self.assertEqual(
            attempts[1][0],
            (EXPECTED_DEFINES["RTCOM_OP_UPLOAD_UCODE"], len_bytes[0]),
        )


class BusLockIrqOrderingTest(unittest.TestCase):
    """Verify the clear -> configure -> clear IRQ ordering in rtcom_bus_lock().

    The embedded driver uses direct MMIO, so this is a host-side model of the
    exact sequence implemented in arm7/source/rtcom_driver.c.
    """

    def test_if_cleared_before_and_after_rcnt_write(self):
        events = []

        def enter_critical_section():
            events.append(("IME", 0))
            return 0

        def read_rcnt():
            events.append(("READ_RCNT", 0x1234))
            return 0x1234

        def write_if(value):
            events.append(("IF", value))

        def write_rcnt(value):
            events.append(("RCNT", value))

        def bus_lock():
            ime = enter_critical_section()
            saved_rcnt = read_rcnt()
            write_if(IRQ_NETWORK)
            write_rcnt(RCNT_REFERENCE_VALUE)
            write_if(IRQ_NETWORK)
            return ime, saved_rcnt

        bus_lock()

        self.assertEqual(
            events,
            [
                ("IME", 0),
                ("READ_RCNT", 0x1234),
                ("IF", IRQ_NETWORK),
                ("RCNT", RCNT_REFERENCE_VALUE),
                ("IF", IRQ_NETWORK),
            ],
        )


if __name__ == "__main__":
    unittest.main()
