#!/home/maker/miniforge3/envs/serial/bin/python
"""Drive the tea machine cycle by cycle over serial.

    pip install pyserial

    python brew.py                 # 5 cycles, the standard profile
    python brew.py -n 8            # 8 cycles
    python brew.py -i              # interactive: taste between cycles
    python brew.py --port /dev/ttyUSB0

The firmware owns the hard limits. This script owns the recipe, so the
profile can change without reflashing.
"""

import argparse
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial not found - check the shebang points at the env "
             "where it is installed")

BAUD = 115200

# Ramp-up cycles, then CX repeated. (brew_seconds, wait_seconds)
C1 = (5, 20)
C2 = (10, 25)
CX = (15, 45)

# Must not exceed the firmware's own ceilings, or it will NAK.
PUMP_MAX_S = 30
WAIT_MAX_S = 120


def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid is not None:
            return p.device
    return None


def profile(n):
    """The cycle list for an n-cycle brew. No wait after the last pulse."""
    steps = []
    for i in range(1, n + 1):
        b, w = C1 if i == 1 else C2 if i == 2 else CX
        steps.append((b, 0 if i == n else w))
    return steps


class Machine:
    def __init__(self, ser, verbose=False):
        self.ser = ser
        self.verbose = verbose

    def _readline(self):
        line = self.ser.readline().decode(errors="replace").strip()
        if line and self.verbose:
            print(f"    < {line}")
        return line

    def send(self, cmd, timeout):
        """Send one command and block until it closes.

        Returns the closing line. Raises on NAK or timeout.
        """
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()

        deadline = time.time() + timeout
        acked = False

        while time.time() < deadline:
            line = self._readline()
            if not line:
                continue

            verb = line.split()[0]

            if verb == "NAK":
                raise RuntimeError(f"{cmd}: {line}")
            if verb == "ACK":
                acked = True
            elif verb == "STEP":
                if not self.verbose:
                    print(f"    {line}")
            elif verb in ("DONE", "ABORT"):
                if not acked:
                    raise RuntimeError(f"{cmd}: closed without ACK")
                if verb == "ABORT":
                    raise RuntimeError(f"{cmd}: aborted")
                return line

        raise TimeoutError(f"{cmd}: no reply within {timeout:.0f}s")

    def cycle(self, b, w):
        if not 1 <= b <= PUMP_MAX_S:
            raise ValueError(f"brew {b}s outside 1-{PUMP_MAX_S}")
        if not 0 <= w <= WAIT_MAX_S:
            raise ValueError(f"wait {w}s outside 0-{WAIT_MAX_S}")
        # Generous margin: the board is blocking for b+w and cannot answer.
        return self.send(f"brew:{b},{w}", timeout=b + w + 15)

    def emergency_reset(self):
        """Restart the board. While handle() is blocking this is the only
        way to stop the pump from the host side."""
        try:
            self.ser.write(b"reset\n")
            self.ser.flush()
        except Exception:
            pass


def run(m, steps, interactive):
    total = sum(b for b, _ in steps)
    print(f"{len(steps)} cycles, {total}s pump time\n")

    for i, (b, w) in enumerate(steps, 1):
        print(f"C{i}  {b}s brew / {w}s wait")
        started = time.time()
        m.cycle(b, w)
        print(f"    done in {time.time() - started:.0f}s\n")

        if interactive and i < len(steps):
            try:
                ans = input("    taste - [enter] next, 'q' stop, or B,W: ").strip()
            except EOFError:
                ans = "q"
            if ans.lower() == "q":
                print("    stopping")
                return
            if ans:
                try:
                    nb, nw = (int(x) for x in ans.split(","))
                except ValueError:
                    print("    (unparsed, using profile)")
                else:
                    steps[i] = (nb, nw)
                    print(f"    next cycle overridden to {nb},{nw}")

    if interactive:
        while True:
            try:
                ans = input("    more? [enter] stop, or B,W: ").strip()
            except EOFError:
                return
            if not ans:
                return
            try:
                nb, nw = (int(x) for x in ans.split(","))
            except ValueError:
                print("    want B,W")
                continue
            m.cycle(nb, nw)
            print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="serial device (auto-detected if omitted)")
    ap.add_argument("-n", "--cycles", type=int, default=5)
    ap.add_argument("-i", "--interactive", action="store_true",
                    help="pause after each cycle to taste")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="show every line from the board")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        print("No serial port found. Pass --port /dev/ttyUSB0")
        return 1

    if not 1 <= args.cycles <= 12:
        print("cycles must be 1-12")
        return 1

    print(f"opening {port}")
    with serial.Serial(port, BAUD, timeout=1) as ser:
        # Opening the port resets the D1 Mini. Let it boot, then discard
        # the help banner.
        time.sleep(2)
        ser.reset_input_buffer()

        # A bare newline flushes any partial line still sitting in the
        # firmware's command buffer, so our first real command cannot be
        # prefixed by a leftover fragment. The firmware ignores empty lines.
        ser.write(b"\n")
        ser.flush()
        time.sleep(0.2)
        ser.reset_input_buffer()

        m = Machine(ser, verbose=args.verbose)
        try:
            run(m, profile(args.cycles), args.interactive)
        except KeyboardInterrupt:
            print("\ninterrupted - resetting board to stop the pump")
            m.emergency_reset()
            return 130
        except (RuntimeError, TimeoutError, ValueError) as e:
            print(f"\nerror: {e}")
            print("resetting board to stop the pump")
            m.emergency_reset()
            return 1

    print("finished")
    return 0


if __name__ == "__main__":
    sys.exit(main())