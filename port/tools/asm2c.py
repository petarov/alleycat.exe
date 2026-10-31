#!/usr/bin/env python3
"""Translate the listing of CAT.EXE into C for native and WebAssembly builds.
The C counterpart of asm2nasm.py, whose listing parser
and tables it shares.

Usage: asm2c.py <re/CAT.asm> <CAT.EXE> <out.c> [--external-data]

The output is one function, game_run(): a switch on pc with one case per
instruction (its CS offset), in listing order, so straight-line code falls
through and every jump, call and return sets pc and continues the loop.
CALL pushes the CS offset of the next instruction on the runtime stack, RET
pops it into pc. The register, flag and memory model is described in
src/runtime.h. After every BIOS, port or yielding C call the function
returns if game_yield is set, with pc at the next instruction, so the web
build can give control back to the browser and continue later.

The data segment image (CAT.EXE from file offset 300h) is emitted as the
ds_base array. Instructions the translator does not handle become a call to
game_trap(<cs>), which aborts at runtime with the offset.
With --external-data, ds_base is zero-initialized instead; the native importer
loads the user's original file before starting the game.
"""
import os
import re
import sys

# python3 -I leaves the script's directory off sys.path; no __pycache__ in tools/.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from asm2nasm import (CPU_PACE_NS, JUMP_TABLE_ENTRIES, JUMP_TABLE_RE, MARKERS,  # noqa: E402
                      STRING_RE, Untranslatable, in_hand_written, parse_listing,
                      split_operands, target_label)

DS_FILE_OFFSET = 0x300  # DS:0000 in CAT.EXE (see data.asm)
DS_IMAGE_SIZE = 0x7130  # DS:0000..712F

# The hand-written CS ranges of asm2nasm.HAND_WRITTEN (game.asm); only their
# entry points are reached.
HAND_WRITTEN_C = {
    0x13A5: ['return 0;'],              # exit to DOS: the game ends
    0x1419: ['pc = pop(); continue;'],  # install_int_vectors: nothing to do (RET)
}

# Return points inside marker code get pc values right after the last
# instruction, which no instruction uses. This keeps the case range small:
# above 64K the switch's jump table outgrew a WebAssembly br_table (at most
# 65520 entries in V8) and the -O2 build dispatched wrongly.


def jump(target):
    return 'pc = 0x%04X; continue;' % target


def gate(flag, target):
    """CMP byte [flag], 0 / JNE target, as in the NASM markers."""
    return 'zf = !%s; cf = sf = of = af = 0; if (!zf) { pc = 0x%04X; continue; }' % (flag, target)


# The same CS offsets and effects as asm2nasm.MARKERS (see the comments
# there). Items are C statements, or ('call', cs) for a CALL from marker
# code, or ('ccall', 'fn()') for a C call after which the game may yield.
MARKERS_C = {
    0x5CB0: ['game_in_title = 1;'],
    0x0099: ['game_in_title = 0;'],
    0x1338: ['port_hotkeys();'],
    0x1374: [jump(0x1381)],
    0x00FF: ['if (zf) { pc = 0x0101; continue; }', 'ds[0x41a] = 0;', jump(0x0081)],
    0x0173: ['ds[0x41a] = 0;', jump(0x0081)],
    0x042E: ['ds[0x41a] = 0;', jump(0x0081)],
    0x5EF7: [('ccall', 'port_idle()')],
    0x5F29: [('ccall', 'port_idle()')],
    0x5FAA: [('ccall', 'port_idle()')],
    0x6007: [('ccall', 'port_idle()')],
    0x00F3: [gate('infinite_lives', 0x00FA)],
    0x0155: [gate('infinite_lives', 0x015F)],
    0x01AD: [gate('infinite_lives', 0x01B7)],
    0x1DB8: [gate('infinite_lives', 0x1DBF)],
    0x20F5: [gate('invincible', 0x2134)],
    0x4E00: [gate('invincible', 0x2E4F)],
    0x2300: [gate('invincible', 0x2E4F)],
    0x1A1A: [gate('invincible', 0x1A75)],
    0x5EEB: ['wr16(ds, 0x6d8f, 0x2);', 'ds[0x69b] = 0;', jump(0x5F1C)],
    0x5F53: ['wr16(ds, 0x6d37 + 0x10, HELP_CTRL_R);',
             'wr16(ds, 0x6d37 + 0x12, 0x6b92);',
             'CX = 4;',
             jump(0x5F56)],
    0x5F5D: ['DX = 0x0d00;', ('call', 0x5E5B), 'SI = HELP_CTRL_L;', ('call', 0x5E2B),
             'DX = 0x0e00;', ('call', 0x5E5B), 'SI = HELP_CTRL_I;', ('call', 0x5E2B),
             'DX = 0x0f00;', ('call', 0x5E5B), 'SI = HELP_CTRL_P;', ('call', 0x5E2B),
             'DX = 0x1000;', ('call', 0x5E5B), 'SI = HELP_FULLSCREEN;', ('call', 0x5E2B),
             'wr16(ds, 0x6d7f, 0x1200);',
             'wr16(ds, 0x6d81, 0x1300);',
             'wr16(ds, 0x6d79, 0x1500);'],
}

REG16 = ('AX', 'BX', 'CX', 'DX', 'SI', 'DI', 'BP')
REG8 = ('AL', 'BL', 'CL', 'DL', 'AH', 'BH', 'CH', 'DH')
SEG_BASE = {'DS': 'ds', 'ES': 'es'}

CONDITIONS = {
    'JZ': 'zf', 'JNZ': '!zf', 'JC': 'cf', 'JNC': '!cf', 'JA': '!cf && !zf', 'JBE': 'cf || zf',
    'JS': 'sf', 'JNS': '!sf', 'JL': 'sf != of', 'JGE': 'sf == of', 'JLE': 'zf || sf != of',
    'JG': '!zf && sf == of', 'JO': 'of', 'JNO': '!of',
}
ARITH = {'ADD': ('f_add', '0'), 'ADC': ('f_add', 'cf'), 'SUB': ('f_sub', '0'), 'SBB': ('f_sub', 'cf')}
LOGIC = {'AND': '&', 'OR': '|', 'XOR': '^'}
SHIFTS = {'SHL': 'SH_SHL', 'SHR': 'SH_SHR', 'SAR': 'SH_SAR', 'ROL': 'SH_ROL', 'ROR': 'SH_ROR',
          'RCL': 'SH_RCL', 'RCR': 'SH_RCR'}
NOARG = {'CLC': 'cf = 0;', 'STC': 'cf = 1;', 'CMC': 'cf ^= 1;', 'CLD': 'df = 0;', 'STD': 'df = 1;',
         'NOP': ';', 'CLI': ';', 'STI': ';', 'RET': 'pc = pop(); continue;',
         'PUSHF': 'push(flags_image());', 'POPF': 'set_flags_image((uint16_t)pop());',
         'LAHF': 'f_lahf();', 'SAHF': 'f_sahf();', 'AAA': 'f_aaa();'}


def target(text):
    return int(target_label(text)[2:], 16)


class Operand:
    """One operand as C: read() gives an expression, write(v) a statement.
    A memory operand with registers in its address needs `ea` declared by
    the caller (setup)."""

    def __init__(self, text):
        self.setup = None
        self.bits = None
        t = text.strip()
        m = re.match(r'(byte|word) ptr (.*)$', t)
        if m:
            self.bits = 8 if m.group(1) == 'byte' else 16
            t = m.group(2)
        seg, explicit = 'DS', False
        m = re.match(r'(ES|DS|CS|SS):(\[.*\])$', t)
        if m:
            seg, t, explicit = m.group(1), m.group(2), True
        if t.startswith('['):
            self.kind = 'mem'
            self._memory(t[1:-1], seg, explicit)
        elif t in REG16:
            self.kind, self.name, self.bits = 'reg', t, 16
        elif t in REG8:
            self.kind, self.name, self.bits = 'reg', t, 8
        elif re.fullmatch(r'0x[0-9a-f]+', t):
            self.kind, self.name = 'imm', t
        else:
            raise Untranslatable('operand ' + text)

    def _memory(self, inner, seg, explicit):
        regs, disp = [], 0
        for term in (x.strip() for x in inner.split('+')):
            if term in ('BX', 'SI', 'DI', 'BP'):
                regs.append(term)
            elif re.fullmatch(r'0x[0-9a-f]+', term):
                disp += int(term, 16)
            else:
                raise Untranslatable('address ' + inner)
        if 'BP' in regs and not explicit:
            raise Untranslatable('BP addressing defaults to SS')
        if seg == 'CS' and not regs:
            self.base = 'cs_seg'    # code-segment variables (CS:13DF..13E7)
        elif seg in SEG_BASE:
            self.base = SEG_BASE[seg]
        else:
            raise Untranslatable(seg + ' segment operand')
        if regs:
            self.setup = 'uint16_t ea = (uint16_t)(%s + 0x%x);' % (' + '.join(regs), disp & 0xFFFF)
            self.addr = 'ea'
        else:
            self.addr = '0x%x' % (disp & 0xFFFF)

    def read(self):
        if self.kind == 'mem':
            if self.bits == 8:
                return '%s[%s]' % (self.base, self.addr)
            return 'rd16(%s, %s)' % (self.base, self.addr)
        return self.name

    def write(self, value):
        if self.kind == 'reg':
            return '%s = (uint%d_t)(%s);' % (self.name, self.bits, value)
        if self.kind == 'mem':
            if self.bits == 8:
                return '%s[%s] = (uint8_t)(%s);' % (self.base, self.addr, value)
            return 'wr16(%s, %s, %s);' % (self.base, self.addr, value)
        raise Untranslatable('write to an immediate')


def yield_check(resume):
    return 'if (game_yield) { pc = 0x%04X; return 1; }' % resume


def branch(cs, dest):
    """Jump to dest; backward jumps may end the slice in the web build."""
    if dest <= cs:
        return 'BACKEDGE(0x%04X); pc = 0x%04X; continue;' % (dest, dest)
    return jump(dest)


def translate(cs, nxt, mnem, args, jump_tables):
    """Return C lines for one listing instruction; nxt is the CS offset of the
    instruction after it."""
    args = args.split('   ;')[0].strip()

    m = JUMP_TABLE_RE.fullmatch(args)
    if mnem == 'JMP' and m:
        table = int(m.group(1), 16)
        if table not in jump_tables:
            raise Untranslatable('jump table at CS:%04X without data' % table)
        # BX = case index * 2.
        cases = ' '.join('case %d: pc = 0x%04X; break;' % (2 * i, e)
                         for i, e in enumerate(jump_tables[table]))
        return ['switch (BX) { %s default: game_trap(0x%04X); }' % (cases, cs), 'continue;']

    if mnem == 'JMP':
        return [branch(cs, target(args))]
    if mnem in ('JP', 'JNP'):
        raise Untranslatable('parity flag')
    if mnem in CONDITIONS:
        return ['if (%s) { %s }' % (CONDITIONS[mnem], branch(cs, target(args)))]
    if mnem == 'CALL':
        return ['push(0x%04X); pc = 0x%04X; continue;' % (nxt, target(args))]
    if mnem in ('LOOP', 'LOOPZ', 'LOOPNZ'):
        cond = {'LOOP': '', 'LOOPZ': ' && zf', 'LOOPNZ': ' && !zf'}[mnem]
        return ['if (--CX != 0%s) { %s }' % (cond, branch(cs, target(args)))]
    if mnem in NOARG and not args:
        return [NOARG[mnem]]

    m = STRING_RE.match(mnem)
    if m:
        op, width, rep = m.groups()
        rep_code = {None: 0, 'REP': 1, 'REPE': 1, 'REPNE': 2}[rep]
        compares = 1 if op in ('SCAS', 'CMPS') else 0
        return ['STRING_OP(str_%s, %d, %d, %d)' % (op.lower(), 1 if width == 'B' else 2, rep_code, compares)]

    if mnem == 'INT':
        n = int(args, 16)
        if n in (0x10, 0x11, 0x1A):
            return ['bios_int%02x();' % n, yield_check(nxt)]
        raise Untranslatable('INT ' + args)
    if mnem == 'IN':
        dst, port = split_operands(args)
        if dst != 'AL':
            raise Untranslatable('IN ' + args)
        return ['cpu.port = %s;' % port, 'AL = (uint8_t)io_in();', yield_check(nxt)]
    if mnem == 'OUT':
        port, src = split_operands(args)
        if src != 'AL':
            raise Untranslatable('OUT ' + args)
        return ['cpu.port = %s;' % port, 'io_out();', yield_check(nxt)]

    if mnem in ('PUSH', 'POP'):
        if args in REG16:
            return ['push(%s);' % args] if mnem == 'PUSH' else ['%s = (uint16_t)pop();' % args]
        if args in ('DS', 'ES'):
            if mnem == 'PUSH':
                return ['push(seg_%s);' % args.lower()]
            return ['set_%s((uint16_t)pop());' % args.lower()]
        raise Untranslatable(mnem + ' ' + args)

    ops = split_operands(args)
    if mnem == 'MOV' and len(ops) == 2 and ops[0] in ('DS', 'ES'):
        if ops[1] not in REG16:
            raise Untranslatable('MOV sreg from ' + ops[1])
        return ['set_%s(%s);' % (ops[0].lower(), ops[1])]
    if mnem == 'MOV' and len(ops) == 2 and ops[1] in ('ES', 'DS', 'CS', 'SS'):
        if ops[0] in REG16 and ops[1] in ('DS', 'ES'):
            return ['%s = seg_%s;' % (ops[0], ops[1].lower())]
        raise Untranslatable('MOV from ' + ops[1])

    p = [Operand(o) for o in ops]
    setup = [x.setup for x in p if x.setup]
    bits = p[0].bits or (p[1].bits if len(p) > 1 else None)
    if bits is None:
        raise Untranslatable('operand size')
    for x in p:
        if x.kind == 'mem' and x.bits is None:  # MOV [0x2ac4],AL: size from the register
            x.bits = bits
    d = p[0]

    if mnem == 'MOV':
        body = [d.write(p[1].read())]
    elif mnem in ARITH:
        fn, carry = ARITH[mnem]
        body = [d.write('%s(%s, %s, %s, %d)' % (fn, d.read(), p[1].read(), carry, bits))]
    elif mnem == 'CMP':
        body = ['f_sub(%s, %s, 0, %d);' % (d.read(), p[1].read(), bits)]
    elif mnem in LOGIC:
        body = [d.write('f_logic(%s %s %s, %d)' % (d.read(), LOGIC[mnem], p[1].read(), bits))]
    elif mnem == 'TEST':
        body = ['f_logic(%s & %s, %d);' % (d.read(), p[1].read(), bits)]
    elif mnem in ('INC', 'DEC'):
        body = [d.write('f_%s(%s, %d)' % (mnem.lower(), d.read(), bits))]
    elif mnem == 'NOT':
        body = [d.write('~%s' % d.read())]
    elif mnem == 'NEG':
        body = [d.write('f_sub(0, %s, 0, %d)' % (d.read(), bits))]
    elif mnem in SHIFTS:
        body = [d.write('f_shift(%s, %s, %s, %d)' % (SHIFTS[mnem], d.read(), p[1].read(), bits))]
    elif mnem == 'MUL':
        body = ['f_mul%d(%s);' % (bits, d.read())]
    elif mnem == 'XCHG':
        body = ['uint32_t t = %s;' % d.read(), d.write(p[1].read()), p[1].write('t')]
    else:
        raise Untranslatable('mnemonic ' + mnem)

    lines = setup + body
    if len(lines) == 1:
        return lines
    return ['{ ' + ' '.join(lines) + ' }']


def marker_lines(items, synthetic, base):
    """C lines for one MARKERS_C entry; synthetic is a list of the return
    points allocated so far (extended here), numbered from base."""
    out = []
    for item in items:
        if isinstance(item, str):
            out.append(item)
            continue
        kind, what = item
        resume = base + len(synthetic)
        synthetic.append(resume)
        if kind == 'call':
            out.append('push(0x%X); pc = 0x%04X; continue;' % (resume, what))
        else:
            out.append('%s; %s' % (what, yield_check(resume)))
        out.append(('case', resume))
    return out


def main():
    src, exe, out = sys.argv[1], sys.argv[2], sys.argv[3]
    external_data = '--external-data' in sys.argv[4:]
    if set(MARKERS_C) != set(MARKERS):
        sys.exit('asm2c.py: MARKERS_C and asm2nasm.MARKERS differ at %s' % ', '.join(
            '%04X' % x for x in sorted(set(MARKERS_C) ^ set(MARKERS))))
    rows, proc_names, jump_tables = parse_listing(src)

    defined = {cs for cs, *_ in rows if not in_hand_written(cs)} | set(HAND_WRITTEN_C)
    body = []
    synthetic = []
    synthetic_base = max(cs + len(raw) // 2 for cs, raw, *_ in rows)
    traps = 0
    for cs, raw, mnem, args in rows:
        if in_hand_written(cs):
            if cs in HAND_WRITTEN_C:
                body.append('    case 0x%04X:   /* hand-written */' % cs)
                body.extend('        ' + ln for ln in HAND_WRITTEN_C[cs])
            continue
        if cs in proc_names:
            body.append('')
            body.append('    /* ---- %s (CS:%04X) */' % (proc_names[cs], cs))
        body.append('    case 0x%04X:' % cs)
        for ln in marker_lines(MARKERS_C.get(cs, []), synthetic, synthetic_base):
            if isinstance(ln, tuple):
                body.append('    case 0x%X:' % ln[1])
            else:
                body.append('        ' + ln)
        if cs in CPU_PACE_NS:
            body.append('        cpu_pace(%d);' % CPU_PACE_NS[cs])
        nxt = cs + len(raw) // 2
        try:
            lines = translate(cs, nxt, mnem, args, jump_tables)
        except Untranslatable as e:
            lines = ['game_trap(0x%04X);   /* %s %s (%s) */' % (cs, mnem, args.split('   ;')[0], e)]
            traps += 1
        for ln in lines:
            for ref in re.findall(r'pc = 0x([0-9A-F]{4});', ln):
                if int(ref, 16) not in defined:
                    sys.exit('asm2c.py: CS:%04X transfers to CS:%s, which is not an instruction' % (cs, ref))
            body.append('        ' + ln)
    last_cs = rows[-1][0]
    body.append('        game_trap(0x%04X);   /* ran past the last instruction */' % last_cs)

    image = open(exe, 'rb').read()[DS_FILE_OFFSET:DS_FILE_OFFSET + DS_IMAGE_SIZE]
    if len(image) != DS_IMAGE_SIZE:
        sys.exit('asm2c.py: %s is too short for the data segment image' % exe)

    with open(out, 'w', encoding='ascii') as f:
        f.write('/* GENERATED by tools/asm2c.py from re/CAT.asm and CAT.EXE - do not edit.\n')
        f.write(' * %d instructions, %d traps. */\n\n' % (len(defined) - len(HAND_WRITTEN_C), traps))
        f.write('#include "runtime.h"\n\n')
        f.write('/* The data segment (DS = para 0010h): DS:0000..%04X from CAT.EXE, the\n'
                ' * rest zero (the port\'s help lines are copied in by runtime.c). */\n'
                % (DS_IMAGE_SIZE - 1))
        if external_data:
            f.write('/* Loaded from the user-supplied CAT.EXE before game_start. */\n')
            f.write('uint8_t ds_base[SEGMENT_SIZE];\n\n')
        else:
            f.write('uint8_t ds_base[SEGMENT_SIZE] = {\n')
            for i in range(0, len(image), 16):
                f.write('    %s,\n' % ', '.join('0x%02x' % b for b in image[i:i + 16]))
            f.write('};\n\n')
        f.write('#ifdef __EMSCRIPTEN__\n'
                '/* Every 65536th backward jump checks whether the slice is used up, so a\n'
                ' * loop without BIOS or port calls cannot keep the browser waiting. */\n'
                'static uint16_t backedges;\n'
                '#define BACKEDGE(t) if (!++backedges && bios_slice_over()) { pc = (t); return 1; }\n'
                '#else\n'
                '#define BACKEDGE(t)\n'
                '#endif\n\n')
        f.write('int game_run(void)\n{\n    for (;;) {\n    switch (pc) {\n')
        f.write('\n'.join(body))
        f.write('\n    default:\n'
                '        if (pc == UINT32_MAX)   /* main returned (see game_start) */\n'
                '            return 0;\n'
                '        game_trap(pc);\n'
                '    }\n    }\n}\n')
    print('%s: %d instructions, %d marker return points, %d traps'
          % (out, len(defined) - len(HAND_WRITTEN_C), len(synthetic), traps))


if __name__ == '__main__':
    main()
