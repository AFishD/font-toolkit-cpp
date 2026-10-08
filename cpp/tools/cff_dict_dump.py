#!/usr/bin/env python3
"""Decode a CFF DICT byte string into (operands, operator) pairs (debug helper)."""
import sys


def decode_dict(b):
    stack = []
    out = []
    i = 0
    n = len(b)

    def real(bs, i):
        s = ""
        while i < len(bs):
            byte = bs[i]
            i += 1
            for half in (byte >> 4, byte & 0xF):
                if half == 0xF:
                    return float(s or 0), i
                if half == 0xA:
                    s += "."
                elif half == 0xB:
                    s += "E"
                elif half == 0xC:
                    s += "E-"
                elif half == 0xE:
                    s += "-"
                elif half == 0xD:
                    pass
                else:
                    s += str(half)
        return float(s or 0), i

    while i < n:
        b0 = b[i]
        if b0 <= 21:
            i += 1
            if b0 == 12:
                op2 = b[i]
                i += 1
                out.append((stack, f"12 {op2}"))
            else:
                out.append((stack, str(b0)))
            stack = []
        elif b0 == 28:
            stack.append(int.from_bytes(b[i + 1:i + 3], "big", signed=True))
            i += 3
        elif b0 == 29:
            stack.append(int.from_bytes(b[i + 1:i + 5], "big", signed=True))
            i += 5
        elif b0 == 30:
            v, i = real(b, i + 1)
            stack.append(v)
        elif b0 == 255:
            stack.append(int.from_bytes(b[i + 1:i + 5], "big", signed=True) / 65536.0)
            i += 5
        elif 32 <= b0 <= 246:
            stack.append(b0 - 139)
            i += 1
        elif 247 <= b0 <= 250:
            stack.append((b0 - 247) * 256 + b[i + 1] + 108)
            i += 2
        elif 251 <= b0 <= 254:
            stack.append(-(b0 - 251) * 256 - b[i + 1] - 108)
            i += 2
        else:
            out.append((stack, f"?invalid({b0})"))
            stack = []
            i += 1
    if stack:
        out.append((stack, "<trailing operands, no operator>"))
    return out


def main():
    path = sys.argv[1]
    data = open(path, "rb").read()
    import struct

    numTables = struct.unpack(">H", data[4:6])[0]
    cff = None
    for i in range(numTables):
        p = 12 + 16 * i
        if data[p:p + 4] == b"CFF ":
            off, ln = struct.unpack(">II", data[p + 8:p + 16])
            cff = data[off:off + ln]
    if cff is None:
        print("no CFF")
        return

    def index(d, p):
        cnt = struct.unpack(">H", d[p:p + 2])[0]
        if cnt == 0:
            return cnt, p + 2, [], b""
        osz = d[p + 2]
        offs = [int.from_bytes(d[p + 3 + i * osz:p + 3 + (i + 1) * osz], "big")
                for i in range(cnt + 1)]
        ds = p + 3 + (cnt + 1) * osz
        return cnt, ds + (offs[-1] - offs[0]), offs, d[ds:ds + (offs[-1] - offs[0])]

    p = cff[2]
    cnt, pe, offs, items = index(cff, p)
    cnt2, pe2, offs2, topdicts = index(cff, pe)
    print("TopDICT entries:")
    for ops, op in decode_dict(topdicts):
        print("   ", ops, "op", op)
    # find FDArray offset from the top dict
    fd_off = None
    for ops, op in decode_dict(topdicts):
        if op == "12 36" and ops:
            fd_off = ops[0]
    if fd_off:
        print("FDArray at", fd_off)
        cnt3, pe3, offs3, fds = index(cff, fd_off)
        print("FDArray count", cnt3, "offs", offs3)
        cur = 0
        for k in range(cnt3):
            item = fds[cur:cur + (offs3[k + 1] - offs3[k])]
            print(f"  FontDICT[{k}] ({len(item)} bytes):", " ".join("%02X" % x for x in item[:24]))
            for ops, op in decode_dict(item):
                print("      ", ops, "op", op)
            cur += offs3[k + 1] - offs3[k]
            if k >= 2:
                print("   ...")
                break


if __name__ == "__main__":
    main()
