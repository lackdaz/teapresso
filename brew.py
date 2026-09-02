#!/usr/bin/env python3
"""Send '1' to the tea machine and print whatever it says back.

    pip install pyserial
    python brew.py                    # auto-detect the port
    python brew.py /dev/ttyUSB0       # or name it
"""

import sys
import time

import serial
import serial.tools.list_ports

BAUD = 115200


def find_port():
    """Pick the first port that looks like a USB-serial adapter."""
    for port in serial.tools.list_ports.comports():
        if port.vid is not None:
            return port.device
    return None


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    if not port:
        print("No serial port found. Pass one explicitly, e.g. /dev/ttyUSB0")
        return 1

    print(f"opening {port}")
    with serial.Serial(port, BAUD, timeout=1) as ser:
        # The D1 mini resets when the port opens; give it a moment to boot.
        time.sleep(2)
        ser.reset_input_buffer()

        ser.write(b"1")
        print("sent 1\n")

        # Print output until it goes quiet for a while.
        last = time.time()
        while time.time() - last < 60:
            line = ser.readline()
            if line:
                print(line.decode(errors="replace").rstrip())
                last = time.time()

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\ninterrupted")
        sys.exit(130)