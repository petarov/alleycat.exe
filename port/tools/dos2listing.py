#!/usr/bin/env python3
"""Rebuild an asm2c-compatible listing from the supported DOS CAT.EXE.

Capstone supplies instruction decoding; asm2c still translates the game ahead
of time. Neither the executable nor the generated listing belongs in Git.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct

from capstone import CS_ARCH_X86, CS_MODE_16, CS_GRP_CALL, CS_GRP_JUMP, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

EXE_SHA256 = "4979c8867826e08881d1d4bec95c95d6bc1ac70fd21050ccdb4f733a50d7bfdd"
CODE_OFFSET = 0x7230
LOAD_SEGMENT = 0x1000
ALIASES = {
    "JE": "JZ", "JNE": "JNZ", "JB": "JC", "JAE": "JNC", "SAL": "SHL",
    "LOOPE": "LOOPZ", "LOOPNE": "LOOPNZ", "PUSHFW": "PUSHF", "POPFW": "POPF",
}


def relocated_code(exe):
    if hashlib.sha256(exe).hexdigest() != EXE_SHA256:
        raise ValueError("Unsupported CAT.EXE: this port requires the 55,067-byte DOS version "
                         "with SHA-256 " + EXE_SHA256)
    header_size = struct.unpack_from("<H", exe, 8)[0] * 16
    image = bytearray(exe[header_size:])
    count = struct.unpack_from("<H", exe, 6)[0]
    table = struct.unpack_from("<H", exe, 24)[0]
    # Reproduce DOS/Ghidra loading: MOV AX,0010h must become MOV AX,1010h.
    for i in range(count):
        offset, segment = struct.unpack_from("<HH", exe, table + i * 4)
        address = segment * 16 + offset
        value = struct.unpack_from("<H", image, address)[0]
        struct.pack_into("<H", image, address, (value + LOAD_SEGMENT) & 0xFFFF)
    return bytes(image[CODE_OFFSET:])


def operand(decoder, op):
    if op.type == X86_OP_REG:
        return decoder.reg_name(op.reg).upper()
    if op.type == X86_OP_IMM:
        return hex(op.imm & 0xFFFF)
    if op.type != X86_OP_MEM or op.size not in (1, 2):
        raise ValueError("Unsupported operand")
    mem = op.mem
    terms = [decoder.reg_name(r).upper() for r in (mem.base, mem.index) if r]
    if mem.disp or not terms:
        terms.append(hex(mem.disp & 0xFFFF))
    segment = decoder.reg_name(mem.segment).upper() + ":" if mem.segment else ""
    size = "byte" if op.size == 1 else "word"
    return size + " ptr " + segment + "[" + " + ".join(terms) + "]"


def listing(exe):
    code = relocated_code(exe)
    decoder = Cs(CS_ARCH_X86, CS_MODE_16)
    decoder.detail = True
    # Main, interrupt/helper entries and the eight indirect room dispatches.
    # Same extra entries as re/DumpListing.java; all other targets are followed.
    pending = [0, 0x14B3, 0x14FB, 0x1554, 0x13C7]
    pending.extend(struct.unpack_from("<8H", code, 0x250))
    seen = {}
    occupied = {}
    while pending:
        address = pending.pop()
        if address in seen:
            continue
        if not 0 <= address < len(code):
            raise ValueError("Code target outside segment: %04X" % address)
        if 0x250 <= address < 0x260 or 0x13DF <= address < 0x13E8:
            raise ValueError("Code target enters data: %04X" % address)
        ins = next(decoder.disasm(code[address:address + 15], address, count=1), None)
        if ins is None:
            raise ValueError("Cannot decode CS:%04X" % address)
        for byte in range(address, address + ins.size):
            if byte in occupied:
                raise ValueError("Overlapping instructions at CS:%04X" % address)
            occupied[byte] = address
        mnemonic = ins.mnemonic.upper()
        args = ", ".join(operand(decoder, op) for op in ins.operands)
        if mnemonic.startswith(("REP ", "REPE ", "REPNE ")):
            prefix, mnemonic = mnemonic.split(" ")
            mnemonic += "." + prefix
        if re.match(r"^(MOVS|STOS|LODS|SCAS|CMPS)[BW](\.|$)", mnemonic):
            args = ""
        mnemonic = ALIASES.get(mnemonic, mnemonic)
        branch = ins.group(CS_GRP_JUMP) or ins.group(CS_GRP_CALL) or mnemonic.startswith("LOOP")
        if mnemonic in ("LJMP", "LCALL"):
            # The keyboard handler can jump to the ROM reboot entry. This
            # entire range is replaced by the existing portable runtime.
            if not 0x1419 <= address <= 0x15CF:
                raise ValueError("Unsupported far transfer at CS:%04X" % address)
        elif branch and ins.operands[0].type == X86_OP_IMM:
            target = ins.operands[0].imm & 0xFFFF
            pending.append(target)
            args = "0x1000:%x" % (target + CODE_OFFSET)
        seen[address] = (ins.bytes.hex().upper(), mnemonic, args)
        if mnemonic not in ("RET", "RETF", "IRET", "JMP", "LJMP"):
            pending.append(address + ins.size)
    rows = ["; Generated locally from CAT.EXE; do not redistribute.",
            "0250  db " + ",".join("%02Xh" % b for b in code[0x250:0x260])]
    rows.extend("%04X  %s  %s %s" % (address, *values)
                for address, values in sorted(seen.items()))
    return "\n".join(rows) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        text = listing(args.exe.read_bytes())
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + "\n")
    args.output.write_text(text, encoding="ascii")
    print("%s: %d instructions" % (args.output, len(text.splitlines()) - 2))


if __name__ == "__main__":
    main()
