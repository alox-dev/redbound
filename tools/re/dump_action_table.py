#!/usr/bin/env python3
USAGE = "usage: dump_action_table.py path/to/bin/bounce [symbol] [count]"
import shutil
import struct
import subprocess
import sys


def sections(data):
    if data[:4] != b"\x7fELF":
        raise SystemExit("not an ELF file")
    is64 = data[4] == 2
    end = "<" if data[5] == 1 else ">"
    if is64:
        shoff, = struct.unpack_from(end + "Q", data, 0x28)
        shentsize, shnum = struct.unpack_from(end + "HH", data, 0x3A)
    else:
        shoff, = struct.unpack_from(end + "I", data, 0x20)
        shentsize, shnum = struct.unpack_from(end + "HH", data, 0x2E)
    out = []
    for i in range(shnum):
        o = shoff + i * shentsize
        if is64:
            _, typ, _, addr, off, size = struct.unpack_from(end + "IIQQQQ", data, o)
        else:
            _, typ, _, addr, off, size = struct.unpack_from(end + "IIIIII", data, o)
        if addr and typ != 8:
            out.append((addr, off, size))
    return is64, end, out


def to_offset(secs, vaddr):
    for addr, off, size in secs:
        if addr <= vaddr < addr + size:
            return off + (vaddr - addr)
    raise ValueError("address 0x%x is outside the data sections" % vaddr)


def cstring(data, off):
    end = data.index(b"\0", off)
    return data[off:end].decode("latin-1")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(USAGE)
    path = sys.argv[1]
    symbol = sys.argv[2] if len(sys.argv) > 2 else "C_202"
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 88
    nm = shutil.which("arm-linux-gnueabi-nm") or shutil.which("nm")
    lines = subprocess.run([nm, "-C", path], capture_output=True, text=True).stdout.splitlines()
    names = {symbol, symbol.replace("_", "."), symbol.replace(".", "_")}
    hits = [l for l in lines if any(l.split(" ", 2)[-1].endswith(n) for n in names)]
    if not hits:
        raise SystemExit("symbol %s not found with %s" % (symbol, nm))
    vaddr = int(hits[0].split()[0], 16)
    data = open(path, "rb").read()
    is64, end, secs = sections(data)
    psize = 8 if is64 else 4
    base = to_offset(secs, vaddr)
    print("%s at 0x%x (%s)" % (symbol, vaddr, hits[0].split(" ", 2)[-1]))
    for i in range(count):
        p, = struct.unpack_from(end + ("Q" if is64 else "I"), data, base + i * psize)
        print("%2d %s" % (i, cstring(data, to_offset(secs, p)) if p else "(null)"))


if __name__ == "__main__":
    main()
