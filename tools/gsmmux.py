#!/usr/bin/env python3
# tools/gsmmux.py
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""
GSM 07.10 Multiplexer

Supports two operating modes selected via --mode:

  python (default):
    Pure Python implementation that does NOT rely on kernel modules.
    Implements the GSM 07.10 protocol entirely in Python and creates
    virtual serial ports using PTY.

  kernel:
    Uses the Linux kernel n_gsm line discipline (requires the n_gsm
    kernel module). Attaches the discipline to the serial port and
    creates /dev/ttyGSM<N> virtual TTY device nodes.
    Supports selecting the line discipline number via -l/--ldisc.

Main features:
1. GSM 07.10 frame encoding/decoding (python mode)
2. FCS (Frame Check Sequence) calculation (python mode)
3. Virtual PTY / ttyGSM device creation
4. Multi-threaded data forwarding (python mode)
5. Support for multiple DLCI channels
"""

import argparse
import array
import fcntl
import os
import pty
import select
import signal
import stat
import sys
import termios
import threading
import time
from collections import deque
from enum import IntEnum

import serial


# GSM 07.10 protocol constants
class GSM0710:
    """GSM 07.10 protocol constants"""

    # Frame flags
    FLAG = 0xF9  # Frame boundary flag
    ESCAPE = 0x7D  # Escape character for transparency
    ESCAPE_XOR = 0x20  # XOR mask for escaped characters

    # Address field encoding
    EA_BIT = 0x01  # Extension bit (set to 1 for single-byte address)
    CR_BIT = 0x02  # Command/Response bit
    DLCI_SHIFT = 2  # DLCI bit shift

    # Control field - Frame types
    SABM = 0x2F  # Set Asynchronous Balanced Mode
    UA = 0x63  # Unnumbered Acknowledgement
    DM = 0x0F  # Disconnected Mode
    DISC = 0x43  # Disconnect
    UIH = 0xEF  # Unnumbered Information with Header check
    UI = 0x03  # Unnumbered Information

    # Poll/Final bit
    PF_BIT = 0x10

    # Length encoding
    LENGTH_EA_BIT = 0x01  # Extension bit in length field

    # Maximum values
    MAX_FRAME_SIZE = 256
    MAX_DLCI = 63

    # FCS calculation
    FCS_INIT = 0xFF
    FCS_GOOD = 0xCF  # Good FCS check value


class FrameType(IntEnum):
    """GSM 07.10 frame types"""

    SABM = GSM0710.SABM
    UA = GSM0710.UA
    DM = GSM0710.DM
    DISC = GSM0710.DISC
    UIH = GSM0710.UIH
    UI = GSM0710.UI


class GSM0710Frame:
    """GSM 07.10 frame structure"""

    # FCS lookup table for CRC calculation
    FCS_TABLE = [
        0x00,
        0x91,
        0xE3,
        0x72,
        0x07,
        0x96,
        0xE4,
        0x75,
        0x0E,
        0x9F,
        0xED,
        0x7C,
        0x09,
        0x98,
        0xEA,
        0x7B,
        0x1C,
        0x8D,
        0xFF,
        0x6E,
        0x1B,
        0x8A,
        0xF8,
        0x69,
        0x12,
        0x83,
        0xF1,
        0x60,
        0x15,
        0x84,
        0xF6,
        0x67,
        0x38,
        0xA9,
        0xDB,
        0x4A,
        0x3F,
        0xAE,
        0xDC,
        0x4D,
        0x36,
        0xA7,
        0xD5,
        0x44,
        0x31,
        0xA0,
        0xD2,
        0x43,
        0x24,
        0xB5,
        0xC7,
        0x56,
        0x23,
        0xB2,
        0xC0,
        0x51,
        0x2A,
        0xBB,
        0xC9,
        0x58,
        0x2D,
        0xBC,
        0xCE,
        0x5F,
        0x70,
        0xE1,
        0x93,
        0x02,
        0x77,
        0xE6,
        0x94,
        0x05,
        0x7E,
        0xEF,
        0x9D,
        0x0C,
        0x79,
        0xE8,
        0x9A,
        0x0B,
        0x6C,
        0xFD,
        0x8F,
        0x1E,
        0x6B,
        0xFA,
        0x88,
        0x19,
        0x62,
        0xF3,
        0x81,
        0x10,
        0x65,
        0xF4,
        0x86,
        0x17,
        0x48,
        0xD9,
        0xAB,
        0x3A,
        0x4F,
        0xDE,
        0xAC,
        0x3D,
        0x46,
        0xD7,
        0xA5,
        0x34,
        0x41,
        0xD0,
        0xA2,
        0x33,
        0x54,
        0xC5,
        0xB7,
        0x26,
        0x53,
        0xC2,
        0xB0,
        0x21,
        0x5A,
        0xCB,
        0xB9,
        0x28,
        0x5D,
        0xCC,
        0xBE,
        0x2F,
        0xE0,
        0x71,
        0x03,
        0x92,
        0xE7,
        0x76,
        0x04,
        0x95,
        0xEE,
        0x7F,
        0x0D,
        0x9C,
        0xE9,
        0x78,
        0x0A,
        0x9B,
        0xFC,
        0x6D,
        0x1F,
        0x8E,
        0xFB,
        0x6A,
        0x18,
        0x89,
        0xF2,
        0x63,
        0x11,
        0x80,
        0xF5,
        0x64,
        0x16,
        0x87,
        0xD8,
        0x49,
        0x3B,
        0xAA,
        0xDF,
        0x4E,
        0x3C,
        0xAD,
        0xD6,
        0x47,
        0x35,
        0xA4,
        0xD1,
        0x40,
        0x32,
        0xA3,
        0xC4,
        0x55,
        0x27,
        0xB6,
        0xC3,
        0x52,
        0x20,
        0xB1,
        0xCA,
        0x5B,
        0x29,
        0xB8,
        0xCD,
        0x5C,
        0x2E,
        0xBF,
        0x90,
        0x01,
        0x73,
        0xE2,
        0x97,
        0x06,
        0x74,
        0xE5,
        0x9E,
        0x0F,
        0x7D,
        0xEC,
        0x99,
        0x08,
        0x7A,
        0xEB,
        0x8C,
        0x1D,
        0x6F,
        0xFE,
        0x8B,
        0x1A,
        0x68,
        0xF9,
        0x82,
        0x13,
        0x61,
        0xF0,
        0x85,
        0x14,
        0x66,
        0xF7,
        0xA8,
        0x39,
        0x4B,
        0xDA,
        0xAF,
        0x3E,
        0x4C,
        0xDD,
        0xA6,
        0x37,
        0x45,
        0xD4,
        0xA1,
        0x30,
        0x42,
        0xD3,
        0xB4,
        0x25,
        0x57,
        0xC6,
        0xB3,
        0x22,
        0x50,
        0xC1,
        0xBA,
        0x2B,
        0x59,
        0xC8,
        0xBD,
        0x2C,
        0x5E,
        0xCF,
    ]

    def __init__(self, dlci=0, control=GSM0710.UIH, data=b"", cr=True, pf=False):
        """Initialize GSM 07.10 frame"""
        self.dlci = dlci
        self.control = control
        self.data = data
        self.cr = cr  # Command/Response bit
        self.pf = pf  # Poll/Final bit

    @staticmethod
    def calculate_fcs(data):
        """
        Calculate FCS (Frame Check Sequence)

        NuttX uses crc8rohcpart(buffer, len, 0), which implements:
        1. gsmmux_frame_fcs() calls crc8rohcpart(buffer, len, 0)
        2. crc8rohcpart() calls crc8table(table, buffer, len, 0)
        3. crc8table() implementation:
           - crc8val ^= 0xFF  (0 becomes 0xFF)
           - table lookup calculation
           - return crc8val ^ 0xFF

        Equivalent to: crc8table_with_xor(data, init=0xFF)
        """
        # Initial value 0, becomes 0xFF after XOR 0xFF
        fcs = 0xFF

        # Table lookup calculation
        for byte in data:
            fcs = GSM0710Frame.FCS_TABLE[fcs ^ byte]

        # Final XOR 0xFF
        return fcs ^ 0xFF

    @staticmethod
    def check_fcs(data, fcs):
        """Check if FCS is correct"""
        calculated_fcs = GSM0710Frame.calculate_fcs(data)
        return calculated_fcs == fcs

    def encode(self):
        """Encode GSM 07.10 frame"""
        frame = bytearray()

        # Flag
        frame.append(GSM0710.FLAG)

        # Address field (1 byte): EA=1, CR, DLCI
        address = GSM0710.EA_BIT | (self.dlci << GSM0710.DLCI_SHIFT)
        if self.cr:
            address |= GSM0710.CR_BIT
        frame.append(address)

        # Control field
        control = self.control
        if self.pf:
            control |= GSM0710.PF_BIT
        frame.append(control)

        # Length field (1 or 2 bytes)
        data_len = len(self.data)
        if data_len < 128:
            # Short format: 7 bits length + EA bit
            length = (data_len << 1) | GSM0710.LENGTH_EA_BIT
            frame.append(length)
        else:
            # Long format: 15 bits length (not commonly used)
            length_low = (data_len << 1) & 0xFF
            length_high = (data_len >> 7) & 0xFF
            frame.append(length_low)
            frame.append(length_high)

        # Data
        frame.extend(self.data)

        # FCS
        fcs_end_idx = 3 if data_len < 128 else 4  # Short format: 3, Long format: 4
        fcs_data = frame[1 : fcs_end_idx + 1]  # Address + Control + Length(s)
        fcs = self.calculate_fcs(fcs_data)
        frame.append(fcs)

        # Flag
        frame.append(GSM0710.FLAG)

        return bytes(frame)

    @staticmethod
    def decode(frame_bytes, debug=False, debug_callback=None):
        """Decode GSM 07.10 frame"""

        def _debug_out(msg):
            if debug and debug_callback:
                debug_callback(msg)

        if len(frame_bytes) < 5:  # Minimum frame size
            _debug_out(f"    [Decode] Frame too short: {len(frame_bytes)} bytes")
            return None

        # Check flags
        if frame_bytes[0] != GSM0710.FLAG or frame_bytes[-1] != GSM0710.FLAG:
            _debug_out(
                f"    [Decode] Invalid frame flags: start=0x{frame_bytes[0]:02x} end=0x{frame_bytes[-1]:02x}"
            )
            return None

        idx = 1

        # Parse address field
        address = frame_bytes[idx]
        idx += 1
        if not (address & GSM0710.EA_BIT):
            # Multi-byte address not supported
            _debug_out(
                f"    [Decode] Multi-byte address not supported: 0x{address:02x}"
            )
            return None

        dlci = (address >> GSM0710.DLCI_SHIFT) & 0x3F
        cr = bool(address & GSM0710.CR_BIT)

        # Parse control field
        control_raw = frame_bytes[idx]
        idx += 1
        pf = bool(control_raw & GSM0710.PF_BIT)
        control = control_raw & ~GSM0710.PF_BIT  # Remove PF bit

        # Parse length field
        length_byte = frame_bytes[idx]
        idx += 1
        if length_byte & GSM0710.LENGTH_EA_BIT:
            # Short format
            data_len = length_byte >> 1
        else:
            # Long format
            if idx >= len(frame_bytes):
                _debug_out("    [Decode] Length field incomplete")
                return None
            length_high = frame_bytes[idx]
            idx += 1
            data_len = (length_high << 7) | (length_byte >> 1)

        # Extract data
        if idx + data_len + 2 > len(frame_bytes):  # +2 for FCS and flag
            _debug_out(
                f"    [Decode] Data length mismatch: expected {data_len} actual {len(frame_bytes)-idx-2}"
            )
            return None

        data = frame_bytes[idx : idx + data_len]
        idx += data_len

        # Check FCS
        # NuttX calculates Address + Control + Length for all frame types
        fcs = frame_bytes[idx]

        # Determine FCS range - always include length bytes
        if length_byte & GSM0710.LENGTH_EA_BIT:
            # Short format - one length byte
            fcs_data = frame_bytes[1:4]  # Address, Control, Length
        else:
            # Long format - two length bytes
            fcs_data = frame_bytes[1:5]  # Address, Control, Length0, Length1

        calculated_fcs = GSM0710Frame.calculate_fcs(fcs_data)
        if calculated_fcs != fcs:
            _debug_out(
                f"    [Decode] FCS check failed: expected 0x{calculated_fcs:02x} received 0x{fcs:02x}"
            )
            _debug_out(f"    [Decode] FCS data: {fcs_data.hex()}")
            return None

        return GSM0710Frame(dlci=dlci, control=control, data=data, cr=cr, pf=pf)


class GSM0710Decoder:
    """GSM 07.10 frame decoder - handles byte stream and collects non-frame data"""

    def __init__(self, debug=False, raw_data_callback=None, debug_callback=None):
        self.buffer = bytearray()
        self.in_frame = False
        self.debug = debug
        self.raw_data_callback = raw_data_callback  # Raw data callback function
        self.debug_callback = debug_callback  # Debug message callback function
        self.raw_buffer = bytearray()  # Buffer for collecting raw data
        self.last_flag_time = 0  # Track time of last FLAG for timeout detection
        self.stats = {
            "total_bytes": 0,
            "frames_decoded": 0,
            "frames_failed": 0,
            "raw_bytes_collected": 0,  # Number of raw data bytes collected
            "fragmented_frames": 0,  # Frames received across multiple reads
        }

    def _debug_print(self, message):
        """Print debug message via callback or directly"""
        if self.debug:
            if self.debug_callback:
                self.debug_callback(message)
            # If no callback, don't print (silent debug mode)

    def feed(self, data):
        """
        Feed data byte stream, return list of decoded frames

        Handles mixed data stream:
        - GSM 07.10 frames (F9 ... F9)
        - Raw data (non-frame data, collected and output via callback)

        State machine logic (FIXED for fragmentation):
        - INIT state (not in_frame): Only non-F9 bytes are raw data
        - IN_FRAME state: Collecting frame data until closing F9
        - Consecutive F9 flags are treated as single frame boundary
        - Frame buffer persists across feed() calls to handle fragmentation
        """
        frames = []
        self.stats["total_bytes"] += len(data)

        for byte in data:
            if byte == GSM0710.FLAG:
                if self.in_frame:
                    # End of current frame
                    self.buffer.append(byte)

                    # Only try to decode if we have more than just opening F9
                    if len(self.buffer) > 1:
                        frame = GSM0710Frame.decode(
                            bytes(self.buffer),
                            debug=self.debug,
                            debug_callback=self._debug_print,
                        )
                        if frame:
                            frames.append(frame)
                            self.stats["frames_decoded"] += 1

                            # Check if this was a fragmented frame
                            if len(self.buffer) > 200:  # Likely fragmented
                                self.stats["fragmented_frames"] += 1

                            self._debug_print(
                                f"[Decoder] Frame decoded: DLCI={frame.dlci} len={len(frame.data)} buffer={len(self.buffer)}"
                            )
                        else:
                            self.stats["frames_failed"] += 1
                            hex_preview = self.buffer[:50].hex()
                            ellipsis = "..." if len(self.buffer) > 50 else ""
                            self._debug_print(
                                f"[Decoder] Frame decode failed "
                                f"({len(self.buffer)} bytes): "
                                f"{hex_preview}{ellipsis}"
                            )

                    # Reset for next frame - consecutive F9 is handled by staying in_frame
                    self.buffer.clear()
                    self.buffer.append(byte)  # This F9 is the start of next frame
                    # Keep in_frame = True to handle consecutive F9
                else:
                    # Not in frame, flush any raw data before starting new frame
                    if len(self.raw_buffer) > 0:
                        self._flush_raw_data()

                    # Start new frame
                    self.buffer.append(byte)
                    self.in_frame = True
            elif self.in_frame:
                # In frame - accumulate data (may span multiple feed() calls)
                self.buffer.append(byte)

                # Prevent buffer overflow - but increase limit for fragmented frames
                if len(self.buffer) > GSM0710.MAX_FRAME_SIZE * 4:  # Increased from *2
                    self._debug_print(
                        f"[Decoder] Buffer overflow, treating as raw data ({len(self.buffer)} bytes)"
                    )
                    # This is likely corrupted data, not a valid frame
                    # Treat the buffered data (except leading F9) as raw data
                    if self.buffer[0] == GSM0710.FLAG:
                        self.raw_buffer.extend(self.buffer[1:])
                        self.stats["raw_bytes_collected"] += len(self.buffer) - 1
                    else:
                        self.raw_buffer.extend(self.buffer)
                        self.stats["raw_bytes_collected"] += len(self.buffer)
                    self._flush_raw_data()
                    self.buffer.clear()
                    self.in_frame = False
            else:
                # Not in frame and not FLAG = raw data
                self.raw_buffer.append(byte)
                self.stats["raw_bytes_collected"] += 1

                # Flush on newline for better real-time display
                if byte == 0x0A:  # \n
                    self._flush_raw_data()

        return frames

    def _flush_raw_data(self):
        """Output collected raw data"""
        if len(self.raw_buffer) > 0 and self.raw_data_callback:
            try:
                # Try to decode as text
                text = self.raw_buffer.decode("utf-8", errors="replace")
                self.raw_data_callback(text)
            except Exception:
                # If decode fails, output hex
                self.raw_data_callback(f"[HEX] {self.raw_buffer.hex()}")
            self.raw_buffer.clear()

    def get_stats(self):
        """Get statistics"""
        return self.stats.copy()


class VirtualSerialPort:
    """Virtual serial port - implemented using PTY"""

    def __init__(self, dlci, name=None):
        self.dlci = dlci
        self.name = name
        self.master_fd = None
        self.slave_fd = None
        self.slave_name = None
        self.running = False
        self.write_queue = deque()
        self.write_lock = threading.Lock()

    def open(self):
        """Create PTY device"""
        self.master_fd, self.slave_fd = pty.openpty()

        # Get slave device name
        self.slave_name = os.ttyname(self.slave_fd)

        # Set raw mode
        attrs = termios.tcgetattr(self.slave_fd)
        attrs[0] &= ~(
            termios.IGNBRK
            | termios.BRKINT
            | termios.PARMRK
            | termios.ISTRIP
            | termios.INLCR
            | termios.IGNCR
            | termios.ICRNL
            | termios.IXON
        )
        attrs[1] &= ~termios.OPOST
        attrs[2] &= ~(termios.CSIZE | termios.PARENB)
        attrs[2] |= termios.CS8
        attrs[3] &= ~(
            termios.ECHO
            | termios.ECHONL
            | termios.ICANON
            | termios.ISIG
            | termios.IEXTEN
        )
        termios.tcsetattr(self.slave_fd, termios.TCSANOW, attrs)

        self.running = True
        return self.slave_name

    def close(self):
        """Close PTY device"""
        self.running = False
        if self.master_fd:
            try:
                os.close(self.master_fd)
            except OSError:
                pass
        if self.slave_fd:
            try:
                os.close(self.slave_fd)
            except OSError:
                pass

    def read(self, size=1024):
        """Read data from master side"""
        if not self.running or not self.master_fd:
            return b""
        try:
            return os.read(self.master_fd, size)
        except OSError:
            return b""

    def write(self, data):
        """Write data to master side"""
        if not self.running or not self.master_fd:
            return 0
        try:
            return os.write(self.master_fd, data)
        except OSError:
            return 0

    def fileno(self):
        """Return file descriptor for select"""
        return self.master_fd if self.master_fd else -1


class GSMMultiplexerKernel:
    """GSM 07.10 Multiplexer - Linux kernel n_gsm line discipline mode

    Requires the n_gsm kernel module. Attaches the GSM 07.10 line
    discipline to the serial port and exposes /dev/ttyGSM<N> devices.
    """

    MTU = 127
    MRU = 127
    DEVICE_NAME = "/dev/ttyGSM"
    DRIVER_NAME = "gsmtty"
    N_GSM0710 = 21
    TIOCSETD = 0x5423
    GSMIOC_GETCONF = 0x804C4700
    GSMIOC_SETCONF = 0x404C4701
    GSMIOC_GETFIRST = 0x80044704

    def __init__(self):
        self.device_fd = None
        self.device_name = None
        self.created_nodes = []

    def get_major_number(self, driver_name):
        """Get driver major number from /proc/devices"""
        try:
            with open("/proc/devices", "r") as f:
                for line in f:
                    if driver_name in line:
                        parts = line.strip().split()
                        if len(parts) >= 2:
                            try:
                                return int(parts[0])
                            except ValueError:
                                continue
        except IOError as e:
            print(f"Cannot open /proc/devices: {e}")
        return -1

    def set_raw_mode(self, fd, baudrate=0):
        """Set serial port to raw mode"""
        try:
            attrs = termios.tcgetattr(fd)
            attrs[0] &= ~(
                termios.IGNBRK
                | termios.BRKINT
                | termios.PARMRK
                | termios.ISTRIP
                | termios.INLCR
                | termios.IGNCR
                | termios.ICRNL
                | termios.IXON
            )
            attrs[1] &= ~termios.OPOST
            attrs[2] &= ~(termios.CSIZE | termios.PARENB)
            attrs[2] |= termios.CS8
            attrs[3] &= ~(
                termios.ECHO
                | termios.ECHONL
                | termios.ICANON
                | termios.ISIG
                | termios.IEXTEN
            )
            if baudrate > 0:
                speed_map = {
                    9600: termios.B9600,
                    19200: termios.B19200,
                    38400: termios.B38400,
                    57600: termios.B57600,
                    115200: termios.B115200,
                    230400: termios.B230400,
                    460800: termios.B460800,
                    500000: termios.B500000,
                    576000: termios.B576000,
                    921600: termios.B921600,
                    1000000: termios.B1000000,
                    1152000: termios.B1152000,
                    1500000: termios.B1500000,
                    2000000: termios.B2000000,
                    2500000: termios.B2500000,
                }
                if baudrate in speed_map:
                    attrs[4] = attrs[5] = speed_map[baudrate]
                else:
                    print(f"Unsupported baudrate: {baudrate}")
                    return False
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            return True
        except (termios.error, OSError) as e:
            print(f"Error setting raw mode: {e}")
            return False

    def set_line_discipline(self, fd, ldisc_type):
        """Set GSM line discipline"""
        try:
            ldisc = array.array("i", [ldisc_type])
            fcntl.ioctl(fd, self.TIOCSETD, ldisc)
            return True
        except OSError as e:
            print(f"Error setting line discipline: {e}")
            return False

    def configure_gsm(self, fd):
        """Configure GSM multiplexing parameters"""
        try:
            # struct gsm_config: adaption, encapsulation, initiator, t1, t2,
            #                    t3, n2, mru, mtu, k, i, unused[8]
            gsm_config = array.array("I", [0] * 19)
            fcntl.ioctl(fd, self.GSMIOC_GETCONF, gsm_config)
            gsm_config[1] = 0  # encapsulation: basic encoding mode
            gsm_config[2] = 1  # initiator: act as initiator
            gsm_config[7] = self.MRU
            gsm_config[8] = self.MTU
            fcntl.ioctl(fd, self.GSMIOC_SETCONF, gsm_config)
            return True
        except OSError as e:
            print(f"Error configuring GSM multiplexing parameters: {e}")
            return False

    def make_device_nodes(self, major, basename, count):
        """Create virtual TTY device nodes"""
        created = 0
        old_umask = os.umask(0)
        try:
            for minor in range(1, count + 1):
                devname = f"{basename}{minor}"
                device_num = os.makedev(major, minor)
                try:
                    os.mknod(devname, stat.S_IFCHR | 0o666, device_num)
                    self.created_nodes.append(devname)
                    created += 1
                    print(f"Created {devname}")
                except OSError as e:
                    print(f"Error creating {devname}: {e}")
        finally:
            os.umask(old_umask)
        return created

    def remove_device_nodes(self):
        """Remove created device nodes"""
        if not self.created_nodes:
            return
        for devname in self.created_nodes:
            try:
                os.unlink(devname)
                print(f"Removed {devname}")
            except OSError as e:
                print(f"Error removing {devname}: {e}")
        self.created_nodes.clear()

    def test_device_access(self, fd, basename):
        """Check if the device is accessible, with retries for link establishment"""
        try:
            first_minor = array.array("I", [0])
            fcntl.ioctl(fd, self.GSMIOC_GETFIRST, first_minor)
            test_devname = f"{basename}{first_minor[0]}"

            max_retries = 10
            for attempt in range(max_retries):
                try:
                    test_fd = os.open(
                        test_devname, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK
                    )
                    os.close(test_fd)
                    return True
                except OSError as e:
                    if attempt < max_retries - 1:
                        print(
                            f"Waiting for n_gsm link ({attempt + 1}/{max_retries}): "
                            f"{e}"
                        )
                        time.sleep(1)
                    else:
                        print(
                            f"Device access test failed after {max_retries} "
                            f"attempts: {e}"
                        )
                        return False
        except OSError as e:
            print(f"Device access test failed: {e}")
            return False

    def signal_handler(self, signum, frame):
        """Signal handler"""
        print(f"Caught signal {signum}")
        self.cleanup()
        sys.exit(0)

    def run(self, device_name, node_count, baudrate, ldisc_type):
        """Run GSM multiplexer in kernel mode"""
        try:
            self.device_fd = os.open(
                device_name, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK
            )
            self.device_name = device_name
            print(f"serial: {device_name}, fd: {self.device_fd}")

            if not self.set_raw_mode(self.device_fd, baudrate):
                return False
            if not self.set_line_discipline(self.device_fd, ldisc_type):
                return False
            if not self.configure_gsm(self.device_fd):
                return False

            time.sleep(2)
            print("Line discipline set")

            major = self.get_major_number(self.DRIVER_NAME)
            if major < 0:
                print("Cannot get major number")
                return False

            created = self.make_device_nodes(major, self.DEVICE_NAME, node_count)
            if created == 0:
                print("No nodes have been created")
                return False
            elif created < node_count:
                print(
                    f"Cannot create all nodes, only {created}/{node_count} "
                    "have been created"
                )

            if not self.test_device_access(self.device_fd, self.DEVICE_NAME):
                self.remove_device_nodes()
                return False

            signal.signal(signal.SIGINT, self.signal_handler)
            signal.signal(signal.SIGTERM, self.signal_handler)
            print("Running in foreground, use Ctrl+C(SIGINT) or kill(SIGTERM) to stop")
            signal.pause()
            self.cleanup()
            return True

        except OSError as e:
            print(f"Error opening {device_name}: {e}")
            self.cleanup()
            return False

    def cleanup(self):
        """Cleanup resources"""
        print("Cleaning up")
        self.remove_device_nodes()
        if self.device_fd is not None:
            try:
                os.close(self.device_fd)
            except OSError:
                pass


class GSMMultiplexer:
    """GSM 07.10 Multiplexer"""

    def __init__(
        self,
        device_path,
        num_channels,
        baudrate=115200,
        debug=False,
        raw_log_file=None,
        hex_dump_file=None,
        show_raw_data=False,
        debug_log_file=None,
        symlink_prefix=None,
    ):
        self.device_path = device_path
        self.num_channels = num_channels
        self.baudrate = baudrate
        self.serial_port = None
        self.virtual_ports = {}
        self.raw_log_file = raw_log_file
        self.raw_log_fd = None
        self.hex_dump_file = hex_dump_file
        self.hex_dump_fd = None
        self.debug_log_file = debug_log_file
        self.debug_log_fd = None
        self.show_raw_data = show_raw_data
        self.debug = debug
        self.symlink_prefix = (
            symlink_prefix  # e.g. /dev/ttyGSM -> /dev/ttyGSM1, /dev/ttyGSM2
        )
        self.symlinks = []  # Track created symlinks for cleanup
        self.decoder = GSM0710Decoder(
            debug=debug,
            raw_data_callback=self._handle_raw_data,
            debug_callback=self._debug_log,
        )
        self.running = False
        self.threads = []
        self.initialized = False

    def _debug_log(self, message):
        """Write debug message to file"""
        # Only output if debug is enabled AND debug_log_fd is available
        if not self.debug or not self.debug_log_fd:
            return

        try:
            import datetime

            timestamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            self.debug_log_fd.write(f"[{timestamp}] {message}\n")
            self.debug_log_fd.flush()
        except Exception as e:
            # Only print error, don't print the message itself
            print(f"[Error] Failed to write debug log: {e}")

    def _handle_raw_data(self, text):
        """Handle raw data (non-GSM frame data)"""
        # Output to terminal if enabled
        if self.show_raw_data:
            print(f"\033[90m[RawData] {text}\033[0m", end="")

        # Write to file if specified
        if self.raw_log_fd:
            try:
                self.raw_log_fd.write(text)
                self.raw_log_fd.flush()
            except Exception as e:
                self._debug_log(f"[Error] Failed to write raw data log: {e}")

    def open_serial(self):
        """Open physical serial port"""
        self.serial_port = serial.Serial(
            port=self.device_path,
            baudrate=self.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            write_timeout=1.0,
        )
        print(f"Opened serial port: {self.device_path} @ {self.baudrate} baud")

        # Open raw data log file
        if self.raw_log_file:
            try:
                self.raw_log_fd = open(self.raw_log_file, "a", encoding="utf-8")
                print(f"Raw data log file: {self.raw_log_file}")
            except Exception as e:
                print(
                    f"Warning: Cannot open raw data log file {self.raw_log_file}: {e}"
                )

        # Open hex dump file
        if self.hex_dump_file:
            try:
                self.hex_dump_fd = open(self.hex_dump_file, "ab")
                print(f"Hex dump file: {self.hex_dump_file}")
            except Exception as e:
                print(f"Warning: Cannot open hex dump file {self.hex_dump_file}: {e}")

        # Open debug log file
        if self.debug_log_file and self.debug:
            try:
                self.debug_log_fd = open(self.debug_log_file, "a", encoding="utf-8")
                print(f"Debug log file: {self.debug_log_file}")
            except Exception as e:
                print(f"Warning: Cannot open debug log file {self.debug_log_file}: {e}")

    def create_virtual_ports(self):
        """Create virtual serial ports, optionally with fixed-name symlinks"""
        for dlci in range(1, self.num_channels + 1):
            vport = VirtualSerialPort(dlci)
            slave_name = vport.open()
            self.virtual_ports[dlci] = vport

            # Create symlink for fixed device name if prefix is specified
            if self.symlink_prefix:
                symlink_path = f"{self.symlink_prefix}{dlci}"
                try:
                    if os.path.lexists(symlink_path):
                        os.unlink(symlink_path)
                    os.symlink(slave_name, symlink_path)
                    self.symlinks.append(symlink_path)
                    print(
                        f"Created virtual port DLCI {dlci}: {slave_name} -> {symlink_path}"
                    )
                except OSError as e:
                    print(f"Warning: Cannot create symlink {symlink_path}: {e}")
                    print(f"Created virtual port DLCI {dlci}: {slave_name}")
            else:
                print(f"Created virtual port DLCI {dlci}: {slave_name}")

    def gsm_initialize(self):
        """
        Initialize GSM 07.10 connection

        Local side as Master (initiator), device side as Slave
        """
        print("Initializing GSM 07.10 multiplexer (Master mode)...")

        # Send SABM on DLCI 0 (control channel)
        # cr=True means this is a command (master sends)
        sabm_frame = GSM0710Frame(dlci=0, control=GSM0710.SABM, cr=True, pf=True)
        sabm_bytes = sabm_frame.encode()
        self._debug_log(f"[TX] SABM DLCI 0: {sabm_bytes.hex()}")
        self.serial_port.write(sabm_bytes)
        print("Sent SABM to DLCI 0")

        # Wait for UA response
        time.sleep(0.5)
        response = self.serial_port.read(256)
        if response:
            self._debug_log(f"[RX] Raw data: {response.hex()}")
        frames = self.decoder.feed(response)

        ua_received = False
        for frame in frames:
            self._debug_log(f"[Parse] DLCI={frame.dlci} Control=0x{frame.control:02x}")
            if frame.dlci == 0 and frame.control == GSM0710.UA:
                print("Received UA response, control channel established")
                ua_received = True
                break

        if not ua_received:
            print("Warning: No UA response received, continuing anyway...")

        # Open each DLCI channel
        for dlci in range(1, self.num_channels + 1):
            sabm_frame = GSM0710Frame(dlci=dlci, control=GSM0710.SABM, cr=True, pf=True)
            sabm_bytes = sabm_frame.encode()
            self._debug_log(f"[TX] SABM DLCI {dlci}: {sabm_bytes.hex()}")
            self.serial_port.write(sabm_bytes)
            print(f"Sent SABM to DLCI {dlci}")
            time.sleep(0.1)

        time.sleep(0.5)
        self.initialized = True
        print("GSM 07.10 multiplexer initialization completed")

    def serial_read_thread(self):
        """Serial read thread - read from physical port and distribute to virtual ports"""
        print("Serial read thread started")
        while self.running:
            try:
                if self.serial_port.in_waiting > 0:
                    data = self.serial_port.read(self.serial_port.in_waiting)
                    if data:
                        # Write hex dump if enabled
                        if self.hex_dump_fd:
                            try:
                                self.hex_dump_fd.write(data.hex().encode() + b"\n")
                                self.hex_dump_fd.flush()
                            except Exception as e:
                                self._debug_log(
                                    f"[Error] Failed to write hex dump: {e}"
                                )

                        self._debug_log(
                            f"[Serial←] Received {len(data)} bytes: {data.hex()}"
                        )

                        frames = self.decoder.feed(data)

                        if len(frames) > 0:
                            buf_len = len(self.decoder.buffer)
                            self._debug_log(
                                f"[Parse] Successfully parsed "
                                f"{len(frames)} frame(s), "
                                f"decoder buffer: {buf_len} bytes"
                            )
                        elif len(self.decoder.buffer) > 0 and self.decoder.in_frame:
                            buf_len = len(self.decoder.buffer)
                            self._debug_log(
                                f"[Parse] Incomplete frame "
                                f"buffered: {buf_len} bytes "
                                f"(waiting for more data)"
                            )
                        elif len(frames) == 0:
                            self._debug_log(
                                "[Parse] No frame parsed from " "this data block"
                            )

                        for frame in frames:
                            self._debug_log(
                                f"[Frame] DLCI={frame.dlci} "
                                f"Control=0x{frame.control:02x} "
                                f"CR={frame.cr} PF={frame.pf} "
                                f"DataLen={len(frame.data)}"
                            )

                            # UIH/UI frame - data frames
                            # UIH = 0xEF, UI = 0x03, but also accept 0xFF (some implementations use it)
                            is_data_frame = frame.control in (
                                GSM0710.UIH,
                                GSM0710.UI,
                                0xFF,
                            )

                            if is_data_frame and frame.dlci in self.virtual_ports:
                                # Data frame - forward to virtual port
                                if len(frame.data) > 0:
                                    vport = self.virtual_ports[frame.dlci]
                                    written = vport.write(frame.data)
                                    hex_data = frame.data[:40].hex()
                                    ellip = "..." if len(frame.data) > 40 else ""
                                    self._debug_log(
                                        f"[VPort→] DLCI {frame.dlci} "
                                        f"wrote {written}/"
                                        f"{len(frame.data)} bytes: "
                                        f"{hex_data}{ellip}"
                                    )
                            elif frame.control == GSM0710.SABM:
                                # SABM request - send UA response
                                print(f"Received SABM request DLCI {frame.dlci}")
                                ua_frame = GSM0710Frame(
                                    dlci=frame.dlci,
                                    control=GSM0710.UA,
                                    cr=False,
                                    pf=True,
                                )
                                self.serial_port.write(ua_frame.encode())
                            else:
                                self._debug_log(
                                    f"[Ignore] Unhandled frame type: DLCI={frame.dlci} Control=0x{frame.control:02x}"
                                )
                else:
                    time.sleep(0.01)
            except Exception as e:
                if self.running:
                    print(f"Serial read error: {e}")
                    import traceback

                    traceback.print_exc()
                    time.sleep(0.1)

    def virtual_port_read_thread(self, dlci):
        """Virtual port read thread - read from virtual port and send to physical port"""
        print(f"Virtual port DLCI {dlci} read thread started")
        vport = self.virtual_ports[dlci]

        while self.running:
            try:
                # Use select to check if data is available
                readable, _, _ = select.select([vport.fileno()], [], [], 0.1)
                if readable:
                    data = vport.read(127)  # MTU limit
                    if data:
                        hex_preview = data[:20].hex()
                        ellipsis = "..." if len(data) > 20 else ""
                        self._debug_log(
                            f"[VPort←] DLCI {dlci} read "
                            f"{len(data)} bytes: "
                            f"{hex_preview}{ellipsis}"
                        )
                        # Encapsulate in UIH frame and send
                        frame = GSM0710Frame(
                            dlci=dlci, control=GSM0710.UIH, data=data, cr=True
                        )
                        frame_bytes = frame.encode()
                        self.serial_port.write(frame_bytes)
                        self._debug_log(
                            f"[Serial→] DLCI {dlci} sent frame {len(frame_bytes)} bytes"
                        )
            except Exception as e:
                if self.running:
                    print(f"Virtual port DLCI {dlci} read error: {e}")
                    import traceback

                    traceback.print_exc()
                    time.sleep(0.1)

    def start(self):
        """Start multiplexer"""
        self.running = True

        # Open physical serial port
        self.open_serial()

        # Create virtual ports
        self.create_virtual_ports()

        # Initialize GSM 07.10
        self.gsm_initialize()

        # Start serial read thread
        serial_thread = threading.Thread(target=self.serial_read_thread, daemon=True)
        serial_thread.start()
        self.threads.append(serial_thread)

        # Start virtual port read threads
        for dlci in self.virtual_ports:
            vport_thread = threading.Thread(
                target=self.virtual_port_read_thread, args=(dlci,), daemon=True
            )
            vport_thread.start()
            self.threads.append(vport_thread)

        print("\nGSM 07.10 multiplexer running...")
        print(f"Physical serial port: {self.device_path}")
        print("Virtual ports:")
        for dlci, vport in self.virtual_ports.items():
            if self.symlink_prefix:
                symlink_path = f"{self.symlink_prefix}{dlci}"
                print(f"  DLCI {dlci}: {vport.slave_name} -> {symlink_path}")
            else:
                print(f"  DLCI {dlci}: {vport.slave_name}")
        print("\nPress Ctrl+C to stop")

    def stop(self):
        """Stop multiplexer"""
        print("\nStopping multiplexer...")
        self.running = False

        # Print statistics
        print("\n=== Statistics ===")
        stats = self.decoder.get_stats()
        print(f"Total bytes received: {stats['total_bytes']}")
        if stats["total_bytes"] > 0:
            raw_pct = stats["raw_bytes_collected"] / stats["total_bytes"] * 100
            print(
                f"Raw data collected: "
                f"{stats['raw_bytes_collected']} bytes "
                f"({raw_pct:.1f}%)"
            )
        else:
            print(f"Raw data collected: {stats['raw_bytes_collected']} bytes")
        print(f"Frames decoded successfully: {stats['frames_decoded']}")
        print(f"Frames failed to decode: {stats['frames_failed']}")
        print(f"Fragmented frames (>200B): {stats['fragmented_frames']}")
        if stats["frames_decoded"] + stats["frames_failed"] > 0:
            success_rate = (
                stats["frames_decoded"]
                / (stats["frames_decoded"] + stats["frames_failed"])
                * 100
            )
            print(f"Frame decode success rate: {success_rate:.1f}%")
            if stats["frames_decoded"] > 0 and stats["fragmented_frames"] > 0:
                frag_rate = stats["fragmented_frames"] / stats["frames_decoded"] * 100
                print(f"Frame fragmentation rate: {frag_rate:.1f}%")

        # Send DISC on all DLCIs
        if self.serial_port and self.initialized:
            for dlci in range(self.num_channels, -1, -1):
                try:
                    disc_frame = GSM0710Frame(
                        dlci=dlci, control=GSM0710.DISC, cr=True, pf=True
                    )
                    self.serial_port.write(disc_frame.encode())
                    time.sleep(0.05)
                except Exception:
                    pass

        # Wait for threads
        for thread in self.threads:
            thread.join(timeout=1.0)

        # Close virtual ports
        for vport in self.virtual_ports.values():
            vport.close()

        # Remove symlinks
        for symlink_path in self.symlinks:
            try:
                os.unlink(symlink_path)
            except OSError:
                pass

        # Close serial port
        if self.serial_port:
            self.serial_port.close()

        # Close raw log file
        if self.raw_log_fd:
            try:
                self.raw_log_fd.close()
            except Exception:
                pass

        # Close hex dump file
        if self.hex_dump_fd:
            try:
                self.hex_dump_fd.close()
            except Exception:
                pass

        # Close debug log file
        if self.debug_log_fd:
            try:
                self.debug_log_fd.close()
            except Exception:
                pass

        print("Multiplexer stopped")


def test_frame_codec():
    """Test frame encoding/decoding"""
    print("=== Testing GSM 07.10 Frame Codec ===\n")

    # Test SABM frame
    print("1. Test SABM frame:")
    sabm = GSM0710Frame(dlci=0, control=GSM0710.SABM, cr=True, pf=True)
    sabm_bytes = sabm.encode()
    print(f"   Encoded: {sabm_bytes.hex()}")
    decoded = GSM0710Frame.decode(sabm_bytes)
    if decoded:
        print(
            f"   Decoded: DLCI={decoded.dlci}, Control=0x{decoded.control:02x}, CR={decoded.cr}, PF={decoded.pf}"
        )
    else:
        print("   Decode failed!")

    # Test UA frame
    print("\n2. Test UA frame:")
    ua = GSM0710Frame(dlci=0, control=GSM0710.UA, cr=False, pf=True)
    ua_bytes = ua.encode()
    print(f"   Encoded: {ua_bytes.hex()}")
    decoded = GSM0710Frame.decode(ua_bytes)
    if decoded:
        print(
            f"   Decoded: DLCI={decoded.dlci}, Control=0x{decoded.control:02x}, CR={decoded.cr}, PF={decoded.pf}"
        )
    else:
        print("   Decode failed!")

    # Test UIH frame with data
    print("\n3. Test UIH data frame:")
    test_data = b"Hello GSM!"
    uih = GSM0710Frame(dlci=1, control=GSM0710.UIH, data=test_data, cr=True)
    uih_bytes = uih.encode()
    print(f"   Encoded: {uih_bytes.hex()}")
    print(f"   Data: {test_data}")
    decoded = GSM0710Frame.decode(uih_bytes)
    if decoded:
        print(
            f"   Decoded: DLCI={decoded.dlci}, Control=0x{decoded.control:02x}, CR={decoded.cr}"
        )
        print(f"   Data: {decoded.data}")
        if decoded.data == test_data:
            print("   ✓ Data matches!")
        else:
            print("   ✗ Data mismatch!")
    else:
        print("   Decode failed!")

    # Test decoder with stream
    print("\n4. Test stream decoder:")
    decoder = GSM0710Decoder()
    frames = decoder.feed(sabm_bytes + ua_bytes + uih_bytes)
    print(f"   Parsed {len(frames)} frame(s)")
    for i, frame in enumerate(frames):
        print(
            f"   Frame {i+1}: DLCI={frame.dlci}, Control=0x{frame.control:02x}, DataLen={len(frame.data)}"
        )

    print("\n=== Test completed ===\n")


def main():
    """Main function"""
    parser = argparse.ArgumentParser(
        description="GSM 07.10 Multiplexer",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Python mode (default, no kernel module required)
  %(prog)s -d /dev/ttyUSB0 -n 3
  %(prog)s -d /dev/ttyUSB0 -n 2 -b 115200
  %(prog)s -d /dev/ttyUSB0 -n 2 -r raw_data.log

  # Kernel mode (requires n_gsm kernel module)
  %(prog)s --mode kernel -d /dev/ttyUSB0 -n 3
  %(prog)s --mode kernel -d /dev/ttyUSB0 -n 3 -l 21
        """,
    )

    parser.add_argument(
        "--mode",
        choices=["python", "kernel"],
        default="python",
        help="Operating mode: 'python' (pure Python PTY, default) or "
        "'kernel' (Linux n_gsm line discipline)",
    )

    parser.add_argument(
        "-d",
        "--device",
        help="Physical serial port device path (not needed in test mode)",
    )

    parser.add_argument(
        "-n",
        "--number",
        type=int,
        default=1,
        help="Number of virtual ports to create (default: 1)",
    )

    parser.add_argument(
        "-b",
        "--baudrate",
        type=int,
        default=0,
        help="Serial port baud rate (default: 0 - no setting)",
    )

    parser.add_argument(
        "-l",
        "--ldisc",
        type=int,
        default=GSMMultiplexerKernel.N_GSM0710,
        help=f"Line discipline number, kernel mode only (default: {GSMMultiplexerKernel.N_GSM0710})",
    )

    parser.add_argument(
        "-t",
        "--test",
        action="store_true",
        help="Run frame encoding/decoding test (python mode only)",
    )

    parser.add_argument(
        "-r",
        "--raw-log",
        metavar="FILE",
        help="Save raw data to file (python mode only)",
    )

    parser.add_argument(
        "-x",
        "--hex-dump",
        metavar="FILE",
        help="Save received serial data as hex dump (python mode only)",
    )

    parser.add_argument(
        "--show-raw-data",
        action="store_true",
        help="Display raw data to terminal (python mode only)",
    )

    parser.add_argument(
        "--debug",
        metavar="FILE",
        help="Enable debug mode and save messages to file (python mode only)",
    )

    args = parser.parse_args()

    # Test mode (python only)
    if args.test:
        test_frame_codec()
        return 0

    # Check device parameter
    if not args.device:
        print("Error: Device path must be specified (-d/--device)")
        return 1

    if not os.path.exists(args.device):
        print(f"Error: Device file does not exist: {args.device}")
        return 1

    if args.number <= 0:
        print("Error: Number of channels must be greater than 0")
        return 1

    # Kernel mode
    if args.mode == "kernel":
        mux = GSMMultiplexerKernel()
        baudrate = args.baudrate if args.baudrate > 0 else 0
        success = mux.run(args.device, args.number, baudrate, args.ldisc)
        return 0 if success else 1

    # Python mode
    if args.number > GSM0710.MAX_DLCI:
        print(f"Error: Number of channels must be between 1 and {GSM0710.MAX_DLCI}")
        return 1

    baudrate = args.baudrate if args.baudrate > 0 else 115200
    mux = GSMMultiplexer(
        args.device,
        args.number,
        baudrate,
        debug=bool(args.debug),
        raw_log_file=args.raw_log,
        hex_dump_file=args.hex_dump,
        show_raw_data=args.show_raw_data,
        debug_log_file=args.debug,
        symlink_prefix="/dev/ttyGSM",
    )

    def signal_handler(signum, frame):
        print(f"\nReceived signal {signum}")
        mux.running = False

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    try:
        mux.start()
        while mux.running:
            time.sleep(1)
    except Exception as e:
        print(f"Error: {e}")
    finally:
        mux.stop()

    return 0


if __name__ == "__main__":
    sys.exit(main())
