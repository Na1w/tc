#!/usr/bin/env python3
"""Manual x86-64 disassembler for compiler output analysis."""
import struct, sys

REG64 = {0:"rax",1:"rcx",2:"rdx",3:"rbx",4:"rsp",5:"rbp",6:"rsi",7:"rdi",
         8:"r8",9:"r9",10:"r10",11:"r11",12:"r12",13:"r13",14:"r14",15:"r15"}
REG32 = {0:"eax",1:"ecx",2:"edx",3:"ebx",4:"esp",5:"ebp",6:"esi",7:"edi",
         8:"r8d",9:"r9d",10:"r10d",11:"r11d",12:"r12d",13:"r13d",14:"r14d",15:"r15d"}
REG8 = {0:"al",1:"cl",2:"dl",3:"bl",4:"spl",5:"bpl",6:"sil",7:"dil"}

def r64(n): return REG64.get(n, f"r{n}")
def r32(n): return REG32.get(n, f"r{n}")
def r8(n): return REG8.get(n, f"r{n}")

def disasm(data, base=0x401000):
    ip = 0
    lines = []
    while ip < len(data):
        start = ip
        addr = base + ip
        rex = 0
        rex_w = False

        # REX prefix
        if data[ip] & 0x40 and (data[ip] & 0x38):
            rex = data[ip]
            rex_w = bool(rex & 0x08)
            ip += 1

        if ip >= len(data):
            lines.append((addr, "<truncated>", ""))
            break

        op = data[ip]
        instr = ""
        ops = ""
        err = ""

        # --- Simple opcodes ---
        if op == 0x90:
            instr = "nop"; ip += 1
        elif op == 0xc3:
            instr = "ret"; ip += 1
        elif op == 0xc9:
            instr = "leave"; ip += 1

        # PUSH/POP (0x50-0x5F)
        elif 0x50 <= op <= 0x5f:
            r = (op & 7) | ((rex & 0x04) >> 2) * 8
            if op < 0x58:
                instr = f"push {r64(r)}"
            else:
                instr = f"pop {r64(r)}"
            ip += 1

        # --- CALL rel32 (E8) ---
        elif op == 0xe8:
            rel32 = struct.unpack_from('<i', data, ip+1)[0]
            target = addr + 5 + rel32
            ops = f"{hex(target)} (rel {rel32:+d})"
            instr = "call"
            ip += 5

        # --- JMP rel32 (E9) ---
        elif op == 0xe9:
            rel32 = struct.unpack_from('<i', data, ip+1)[0]
            target = addr + 5 + rel32
            ops = f"{hex(target)} (rel {rel32:+d})"
            instr = "jmp"
            ip += 5

        # --- 0x0F two-byte ---
        elif op == 0x0f:
            ip += 1
            if ip >= len(data):
                instr = "0F?"; err = "TRUNCATED"
            else:
                op2 = data[ip]

                # Conditional jumps 0F 80-8F
                if 0x80 <= op2 <= 0x8f:
                    ip += 1
                    rel32 = struct.unpack_from('<i', data, ip)[0]
                    target = addr + (ip - start) + 4 + rel32
                    jnames = {0x80:"jo",0x81:"jno",0x82:"jc",0x83:"jnc",
                              0x84:"je",0x85:"jne",0x86:"jl",0x87:"jge",
                              0x88:"jle",0x89:"jg",0x8a:"jp",0x8b:"jnp",
                              0x8c:"js",0x8d:"jns",0x8e:"jpe",0x8f:"jpo"}
                    instr = jnames.get(op2, f"j??{op2:02x}")
                    ops = f"{hex(target)} (rel {rel32:+d})"
                    ip += 4

                # SETcc (0F 90-9F)
                elif 0x90 <= op2 <= 0x9f:
                    ip += 1
                    mr = data[ip]; ip += 1
                    rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
                    mod = (mr >> 6) & 3
                    if mod == 3:
                        ops = r8(rm)
                    else:
                        disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                        ops = f"byte [{r64(rm)} + {disp:+d}]"
                    snames = {0x90:"seto",0x91:"setno",0x92:"setc",0x93:"setnc",
                              0x94:"sete",0x95:"setne",0x96:"setl",0x97:"setge",
                              0x98:"setle",0x99:"setg",0x9a:"setp",0x9b:"setnp",
                              0x9c:"sets",0x9d:"setns",0x9e:"setpe",0x9f:"setpo"}
                    instr = snames.get(op2, f"set??{op2:02x}")

                # MOVZX (0F B6/B7)
                elif op2 in (0xb6, 0xb7):
                    ip += 1
                    mr = data[ip]; ip += 1
                    mod = (mr >> 6) & 3
                    rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
                    reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
                    sz = "byte" if op2 == 0xb6 else "word"
                    if mod == 3:
                        ops = f"{r32(reg) if not rex_w else r64(reg)}, {sz} {r64(rm)}"
                    elif mod == 0 and rm == 5:
                        disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                        ops = f"{r32(reg) if not rex_w else r64(reg)}, {sz} [{r64(rm)} + {disp:+d}]"
                    else:
                        disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                        ops = f"{r32(reg) if not rex_w else r64(reg)}, {sz} [{r64(rm)} + {disp:+d}]"
                    instr = "movzx"

                # IMUL (0F AF)
                elif op2 == 0xaf:
                    ip += 1
                    mr = data[ip]; ip += 1
                    mod = (mr >> 6) & 3
                    rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
                    reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
                    if mod == 3:
                        ops = f"{r64(rm) if rex_w else r32(rm)}, {r64(reg) if rex_w else r32(reg)}"
                    else:
                        disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                        ops = f"[{r64(rm)} + {disp:+d}], {r64(reg)}"
                    instr = "imul"

                else:
                    instr = f"0F {op2:02X}"; err = "UNKNOWN"; ip += 1

        # --- 0x83: ADD/SUB/AND/OR/XOR/TEST/CMP r/m, imm8 ---
        elif op == 0x83:
            ip += 1
            mr = data[ip]; ip += 1
            mod = (mr >> 6) & 3
            rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
            reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
            imm8 = struct.unpack_from('<b', data, ip)[0]; ip += 1
            ops_map = {0:"add",1:"or",2:"adc",3:"sbc",4:"and",5:"sub",6:"xor",7:"cmp"}
            instr = ops_map.get(reg, f"op{reg}")
            if mod == 3:
                ops = f"{r64(rm) if rex_w else r32(rm)}, {imm8}"
            else:
                if mod == 0 and rm == 5:
                    disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                    ops = f"[{r64(rm)} + {disp:+d}], {imm8}"
                else:
                    disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                    ops = f"[{r64(rm)} + {disp:+d}], {imm8}"

        # --- 0x81: ADD/SUB/... r/m, imm32 ---
        elif op == 0x81:
            ip += 1
            mr = data[ip]; ip += 1
            mod = (mr >> 6) & 3
            rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
            reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
            imm32 = struct.unpack_from('<i', data, ip)[0]; ip += 4
            ops_map = {0:"add",1:"or",2:"adc",3:"sbc",4:"and",5:"sub",6:"xor",7:"cmp"}
            instr = ops_map.get(reg, f"op{reg}")
            if mod == 3:
                ops = f"{r64(rm) if rex_w else r32(rm)}, {imm32}"
            else:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"[{r64(rm)} + {disp:+d}], {imm32}"

        # --- 0x85: TEST ---
        elif op == 0x85:
            ip += 1
            mr = data[ip]; ip += 1
            mod = (mr >> 6) & 3
            rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
            reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
            instr = "test"
            if mod == 3:
                ops = f"{r64(rm) if rex_w else r32(rm)}, {r64(reg) if rex_w else r32(reg)}"
            else:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"[{r64(rm)} + {disp:+d}], {r64(reg)}"

        # --- 0x89: MOV r/m, r ---
        elif op == 0x89:
            ip += 1
            mr = data[ip]; ip += 1
            mod = (mr >> 6) & 3
            rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
            reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
            instr = "mov"
            if mod == 3:
                ops = f"{r64(reg) if rex_w else r32(reg)}, {r64(rm) if rex_w else r32(rm)}"
            elif mod == 0 and rm == 5:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"{r64(reg) if rex_w else r32(reg)}, [{r64(rm)} + {disp:+d}]"
            else:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"{r64(reg) if rex_w else r32(reg)}, [{r64(rm)} + {disp:+d}]"

        # --- 0x8B: MOV r, r/m ---
        elif op == 0x8b:
            ip += 1
            mr = data[ip]; ip += 1
            mod = (mr >> 6) & 3
            rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
            reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
            instr = "mov"
            if mod == 3:
                ops = f"{r64(reg) if rex_w else r32(reg)}, {r64(rm) if rex_w else r32(rm)}"
            elif mod == 0 and rm == 5:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"{r64(reg) if rex_w else r32(reg)}, [{r64(rm)} + {disp:+d}]"
            else:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"{r64(reg) if rex_w else r32(reg)}, [{r64(rm)} + {disp:+d}]"

        # --- 0x8D: LEA ---
        elif op == 0x8d:
            ip += 1
            mr = data[ip]; ip += 1
            mod = (mr >> 6) & 3
            rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
            reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
            instr = "lea"
            if mod == 0 and rm == 5:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"{r64(reg) if rex_w else r32(reg)}, [{r64(rm)} + {disp:+d}]"
            else:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"{r64(reg) if rex_w else r32(reg)}, [{r64(rm)} + {disp:+d}]"

        # --- 0xC7: MOV r/m, imm32 ---
        elif op == 0xc7:
            ip += 1
            mr = data[ip]; ip += 1
            mod = (mr >> 6) & 3
            rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
            reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
            imm32 = struct.unpack_from('<I', data, ip)[0]; ip += 4
            instr = "mov"
            if mod == 3:
                ops = f"{r64(rm) if rex_w else r32(rm)}, {imm32}"
            elif mod == 0 and rm == 5:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"[{r64(rm)} + {disp:+d}], {imm32}"
            else:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"[{r64(rm)} + {disp:+d}], {imm32}"

        # --- 0x63: MOVSXD ---
        elif op == 0x63:
            ip += 1
            mr = data[ip]; ip += 1
            mod = (mr >> 6) & 3
            rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
            reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
            instr = "movsxd"
            if mod == 3:
                ops = f"{r32(reg)}, dword {r64(rm)}"
            else:
                disp = struct.unpack_from('<i', data, ip)[0]; ip += 4
                ops = f"{r32(reg)}, dword [{r64(rm)} + {disp:+d}]"

        # --- 0x66 prefix (16-bit override) ---
        elif op == 0x66:
            ip += 1
            if ip < len(data):
                op2 = data[ip]
                if op2 == 0xaf:
                    # 66 IMUL r/m16, r16, imm8
                    ip += 1
                    mr = data[ip]; ip += 1
                    rm = (mr & 7) | ((rex & 0x04) >> 2) * 8
                    reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
                    imm8 = data[ip]; ip += 1
                    instr = "imul"
                    ops = f"{r64(rm)}, {r64(reg)}, {imm8}"
                else:
                    instr = f"66h 0x{op2:02X}"; err = "UNKNOWN"; ip += 1
            else:
                instr = "66h"; err = "TRUNCATED"

        # --- 0x67: Address size override ---
        elif op == 0x67:
            instr = "67h (addr size override)"; ip += 1

        # --- 0x64/0x65: Segment override ---
        elif op in (0x64, 0x65):
            seg = "FS" if op == 0x64 else "GS"
            instr = f"{seg} segment override"; ip += 1

        else:
            instr = f"DB 0x{op:02X}"; err = "UNKNOWN"; ip += 1

        hex_bytes = " ".join(f"{b:02X}" for b in data[start:ip])
        lines.append((addr, hex_bytes, instr, ops, err))

    return lines

def print_disasm(data, base, label):
    print(f"\n{'='*80}")
    print(f"  DISASSEMBLY: {label}")
    print(f"  {len(data)} bytes at base 0x{base:08X}")
    print(f"{'='*80}")
    lines = disasm(data, base)
    for line in lines:
        addr, hex_b, instr = line[0], line[1], line[2]
        ops = line[3] if len(line) > 3 else ""
        err = line[4] if len(line) > 4 else ""
        op_str = f" {ops}" if ops else ""
        err_str = f"  *** {err}" if err else ""
        print(f"  {hex(addr):>10s}: {hex_b:<40s} {instr}{op_str}{err_str}")

def analyze_jumps(data, base, label):
    """Analyze jump/call targets to check validity."""
    print(f"\n  --- JUMP/CALL ANALYSIS: {label} ---")
    lines = disasm(data, base)
    for addr, hex_b, instr, ops, err in lines:
        if instr in ("call", "jmp", "je", "jne", "jl", "jg", "jle", "jge", "js", "jns",
                      "jo", "jno", "jc", "jnc", "jp", "jnp", "jpe", "jpo"):
            # Extract target from ops string
            if ops:
                parts = ops.split()
                target_hex = parts[0] if parts else "?"
                rel = parts[2] if len(parts) > 2 else "?"
                target = int(target_hex, 16)
                in_range = base <= target < base + len(data)
                status = "IN .text" if in_range else "OUTSIDE .text!"
                print(f"    0x{addr:08X}: {instr:8s} -> {target_hex} (rel {rel}) [{status}]")

# Read both binaries
def read_text(path):
    with open(path,'rb') as f: data=f.read()
    import struct as st
    e_shoff = st.unpack_from('<Q', data, 40)[0]
    e_shentsize = st.unpack_from('<H', data, 58)[0]
    sh1_off = e_shoff + e_shentsize * 1
    sh_offset = st.unpack_from('<Q', data, sh1_off+24)[0]
    sh_size = st.unpack_from('<Q', data, sh1_off+32)[0]
    sh_addr = st.unpack_from('<Q', data, sh1_off+16)[0]
    return data[sh_offset:sh_offset+sh_size], sh_addr

cb_text, cb_addr = read_text('/tmp/cb_out')
fc_text, fc_addr = read_text('/tmp/fc_out')

print_disasm(cb_text, cb_addr, "compare_branch.c")
analyze_jumps(cb_text, cb_addr, "compare_branch.c")

print_disasm(fc_text, fc_addr, "function_call.c")
analyze_jumps(fc_text, fc_addr, "function_call.c")
