#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# 2024 Ahoy, https://ahoydtu.de
# Creative Commons - https://creativecommons.org/licenses/by-nc-sa/4.0/deed
# -----------------------------------------------------------------------------
# SunSpec / Modbus TCP test client for the Ahoy SunSpec module (ENABLE_SUNSPEC)
#
# Walks the SunSpec model chain the same way the Victron GX (dbus-fronius)
# detector does and prints / checks the decoded values.
# Python 3 standard library only.
#
# examples:
#   python3 sunspec_read.py 192.168.1.50                  # unit 126 (combined)
#   python3 sunspec_read.py 192.168.1.50 --scan 1-32      # separate mode
#   python3 sunspec_read.py 192.168.1.50 --negative       # exception tests
# -----------------------------------------------------------------------------

import argparse
import socket
import struct
import sys

BASES = [40000, 50000, 0]
NI_INT16 = -32768
NI_UINT16 = 0xFFFF
NI_SF = -32768

STATES = {1: "OFF", 2: "SLEEPING", 3: "STARTING", 4: "MPPT", 5: "THROTTLED",
          6: "SHUTTING_DOWN", 7: "FAULT", 8: "STANDBY"}


class ModbusError(Exception):
    def __init__(self, code):
        super().__init__("modbus exception 0x%02X" % code)
        self.code = code


class Client:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.tid = 0

    def close(self):
        self.sock.close()

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("connection closed by server")
            buf += chunk
        return buf

    def request(self, unit, pdu):
        self.tid = (self.tid + 1) & 0xFFFF
        self.sock.sendall(struct.pack(">HHHB", self.tid, 0, len(pdu) + 1, unit) + pdu)
        hdr = self._recv(7)
        tid, pid, length, uid = struct.unpack(">HHHB", hdr)
        body = self._recv(length - 1)
        if tid != self.tid or pid != 0 or uid != unit:
            raise ValueError("bad MBAP header in response")
        if body[0] & 0x80:
            raise ModbusError(body[1])
        return body

    def read(self, unit, addr, cnt, fc=3):
        body = self.request(unit, struct.pack(">BHH", fc, addr, cnt))
        if body[0] != fc or body[1] != 2 * cnt:
            raise ValueError("bad response length")
        return list(struct.unpack(">%dH" % cnt, body[2:2 + 2 * cnt]))


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def string(regs):
    b = b"".join(struct.pack(">H", r) for r in regs)
    return b.split(b"\x00")[0].decode("ascii", "replace").strip()


def scaled(val, sf, signed=True):
    sf = s16(sf)
    if sf == NI_SF:
        return None
    if signed:
        val = s16(val)
        if val == NI_INT16:
            return None
    elif val == NI_UINT16:
        return None
    return val * (10 ** sf)


def fmt(v, unit=""):
    return "n/a" if v is None else ("%g %s" % (round(v, 3), unit)).strip()


def read_block(cl, unit, addr, cnt, fc):
    out = []
    while cnt > 0:
        n = min(cnt, 125)
        out += cl.read(unit, addr, n, fc)
        addr += n
        cnt -= n
    return out


def walk(cl, unit, fc, verbose=True):
    errors = []
    base = None
    for b in BASES:
        try:
            if string(cl.read(unit, b, 2, fc)) == "SunS":
                base = b
                break
        except ModbusError:
            continue
    if base is None:
        return ["no SunS marker found"], {}

    if verbose:
        print("unit %d: SunS at %d" % (unit, base))
    addr = base + 2
    found = {}
    while True:
        mid, length = cl.read(unit, addr, 2, fc)
        if mid == 0xFFFF:
            if verbose:
                print("  end model at %d" % addr)
            break
        regs = read_block(cl, unit, addr + 2, length, fc)
        found[mid] = regs
        if verbose:
            print("  model %d at %d, length %d" % (mid, addr, length))

        if mid == 1:
            if length != 66:
                errors.append("model 1 length %d != 66" % length)
            info = dict(Mn=string(regs[0:16]), Md=string(regs[16:32]), Opt=string(regs[32:40]),
                        Vr=string(regs[40:48]), SN=string(regs[48:64]), DA=regs[64])
            if not info["SN"]:
                errors.append("model 1 serial number empty (Victron ignores such devices)")
            if verbose:
                for k, v in info.items():
                    print("    %-4s %s" % (k, v))
        elif mid in (101, 102, 103):
            if length != 50:
                errors.append("model %d length %d != 50" % (mid, length))
            a_sf, v_sf, w_sf, hz_sf = regs[4], regs[11], regs[13], regs[15]
            wh = (regs[22] << 16) | regs[23]
            st = regs[36]
            if verbose:
                print("    A      %s (L1 %s, L2 %s, L3 %s)" % (fmt(scaled(regs[0], a_sf), "A"), fmt(scaled(regs[1], a_sf)),
                                                             fmt(scaled(regs[2], a_sf)), fmt(scaled(regs[3], a_sf))))
                print("    PhV    L1 %s, L2 %s, L3 %s" % (fmt(scaled(regs[8], v_sf, False), "V"), fmt(scaled(regs[9], v_sf, False), "V"),
                                                       fmt(scaled(regs[10], v_sf, False), "V")))
                print("    W      %s" % fmt(scaled(regs[12], w_sf), "W"))
                print("    Hz     %s" % fmt(scaled(regs[14], hz_sf, False), "Hz"))
                print("    VAr    %s" % fmt(scaled(regs[18], regs[19]), "var"))
                print("    PF     %s" % fmt(scaled(regs[20], regs[21]), "%"))
                print("    WH     %s (WH_SF %d)" % (fmt(wh * 10 ** s16(regs[24]), "Wh"), s16(regs[24])))
                print("    DCW    %s" % fmt(scaled(regs[29], regs[30]), "W"))
                print("    Tmp    %s" % fmt(scaled(regs[31], regs[35]), "C"))
                print("    St     %d (%s)" % (st, STATES.get(st, "?")))
            if st not in STATES:
                errors.append("invalid operating state %d" % st)
            # Victron splits W over the phases by V * I: every phase voltage must be valid
            for ph in range(mid % 10):
                if regs[8 + ph] == 0xFFFF:
                    errors.append("model %d: voltage of phase %d not implemented" % (mid, ph + 1))
        elif mid == 120:
            if length != 26:
                errors.append("model 120 length %d != 26" % length)
            if verbose:
                print("    DERTyp %d, WRtg %s" % (regs[0], fmt(scaled(regs[1], regs[2], False), "W")))
        elif mid == 160:
            n = regs[6]
            if length != 8 + 20 * n:
                errors.append("model 160 length %d != 8 + 20 * %d" % (length, n))
            a_sf, v_sf, w_sf, wh_sf = regs[0], regs[1], regs[2], regs[3]
            for i in range(n):
                r = regs[8 + 20 * i: 28 + 20 * i]
                wh = (r[12] << 16) | r[13]
                if verbose:
                    print("    MPPT %d '%s': %s, %s, %s, %s" % (
                        r[0], string(r[1:9]), fmt(scaled(r[9], a_sf, False), "A"), fmt(scaled(r[10], v_sf, False), "V"),
                        fmt(scaled(r[11], w_sf, False), "W"), fmt(wh * 10 ** s16(wh_sf), "Wh")))
        addr += 2 + length

    if 1 not in found:
        errors.append("model 1 missing")
    if not any(m in found for m in (101, 102, 103)):
        errors.append("model 101/102/103 missing")
    return errors, found


def negative_tests(cl, unit):
    errors = []
    tests = [
        ("write single register (FC06)", struct.pack(">BHH", 6, 40000, 1), 0x01),
        ("read outside map", struct.pack(">BHH", 3, 39000, 2), 0x02),
        ("read 126 registers", struct.pack(">BHH", 3, 40000, 126), 0x03),
    ]
    for name, pdu, exp in tests:
        try:
            cl.request(unit, pdu)
            errors.append("%s: no exception (expected 0x%02X)" % (name, exp))
        except ModbusError as e:
            ok = e.code == exp
            print("  %-30s exception 0x%02X %s" % (name, e.code, "ok" if ok else "(expected 0x%02X)" % exp))
            if not ok:
                errors.append(name)
    return errors


def parse_range(txt):
    a, _, b = txt.partition("-")
    return range(int(a), int(b or a) + 1)


def main():
    ap = argparse.ArgumentParser(description="SunSpec Modbus TCP test client")
    ap.add_argument("host")
    ap.add_argument("--port", type=int, default=502)
    ap.add_argument("--unit", type=int, default=126)
    ap.add_argument("--scan", help="scan unit ids, e.g. 1-32 (separate mode)")
    ap.add_argument("--fc", type=int, default=3, choices=[3, 4])
    ap.add_argument("--negative", action="store_true", help="run exception tests")
    ap.add_argument("--timeout", type=float, default=3.0)
    args = ap.parse_args()

    cl = Client(args.host, args.port, args.timeout)
    errors = []
    try:
        units = parse_range(args.scan) if args.scan else [args.unit]
        found_units = 0
        for u in units:
            try:
                err, found = walk(cl, u, args.fc, verbose=True)
            except ModbusError as e:
                if args.scan:
                    continue
                err, found = ["unit %d: %s" % (u, e)], {}
            if found:
                found_units += 1
            elif args.scan:
                continue  # unit not served, fine while scanning
            errors += ["unit %d: %s" % (u, e) for e in err]
        if args.scan:
            print("%d unit(s) answered" % found_units)
        if args.negative:
            errors += negative_tests(cl, args.unit)
    finally:
        cl.close()

    if errors:
        print("\nFAILED:")
        for e in errors:
            print("  - " + e)
        return 1
    print("\nOK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
