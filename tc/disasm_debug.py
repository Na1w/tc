#!/usr/bin/env python3
"""Minimal x86-64 disassembler for debugging the compiler output."""
import struct, sys

# Opcode map (subset for our needs)
OPCODES = {
    0x48: "REX.W prefix", 0x41: "REX prefix",
    0x44: "REX prefix", 0x45: "REX prefix",
    0x4a: "REX prefix", 0x4b: "REX prefix",
    0x4e: "REX prefix", 0x4f: "REX prefix",
}

REG8 = {0:"al",1:"cl",2:"dl",3:"bl",4:"ah",5:"ch",6:"dh",7:"bh"}
REG16 = {0:"ax",1:"cx",2:"dx",3:"bx",4:"sp",5:"bp",6:"si",7:"di"}
REG32 = {0:"eax",1:"ecx",2:"edx",3:"ebx",4:"esp",5:"ebp",6:"esi",7:"edi"}
REG64 = {0:"rax",1:"rcx",2:"rdx",3:"rbx",4:"rsp",5:"rbp",6:"rsi",7:"rdi",
         8:"r8",9:"r9",10:"r10",11:"r11",12:"r12",13:"r13",14:"r14",15:"r15"}

def reg64(r): return REG64.get(r, f"r?{r}")
def reg32(r): return REG32.get(r, f"r?{r}")
def reg(r, w64): return reg64(r) if w64 else reg32(r)

def disasm(data, base=0x401000):
    ip = 0
    lines = []
    while ip < len(data):
        start_ip = ip
        addr = base + ip
        rex = 0
        w64 = False

        # Detect REX prefix
        if data[ip] & 0x40 and data[ip] & 0x38:
            rex = data[ip]
            w64 = bool(rex & 8)
            ip += 1

        if ip >= len(data):
            lines.append(f"  0x{addr:08X}: <truncated>")
            break

        op = data[ip]
        mnemonic = ""
        operands = ""
        disp_bytes = 0
        disp_signed = False
        sib = False
        modrm_idx = False
        imm_bytes = 0
        imm_signed = False

        # --- Decode ---
        if op == 0x90:
            mnemonic = "nop"
            ip += 1
        elif op == 0xc3:
            mnemonic = "ret"
            ip += 1
        elif op == 0x50 and (op-0x50) <= 7:
            r = op - 0x50
            if rex & 4: r += 8
            mnemonic = f"push {reg(r, w64)}"
            ip += 1
        elif op == 0x58 and (op-0x58) <= 7:
            r = op - 0x58
            if rex & 4: r += 8
            mnemonic = f"pop {reg(r, w64)}"
            ip += 1
        elif op == 0x55:
            mnemonic = "push rbp"
            ip += 1
        elif op == 0x5d:
            mnemonic = "pop rbp"
            ip += 1
        elif op == 0x51:
            mnemonic = "push rcx"
            ip += 1
        elif op == 0x52:
            mnemonic = "push rdx"
            ip += 1
        elif op == 0x56:
            mnemonic = "push rsi"
            ip += 1
        elif op == 0x57:
            mnemonic = "push rdi"
            ip += 1
        elif op == 0x5f:
            mnemonic = "pop rdi"
            ip += 1
        elif op == 0x5e:
            mnemonic = "pop rsi"
            ip += 1
        elif op == 0x5a:
            mnemonic = "pop rdx"
            ip += 1
        elif op == 0x59:
            mnemonic = "pop rcx"
            ip += 1
        elif op == 0x50:
            mnemonic = "push rax"
            ip += 1
        elif op == 0x58:
            mnemonic = "pop rax"
            ip += 1
        elif 0x40 <= op <= 0x4f and op & 0x38:
            # Already consumed as REX, skip
            lines.append(f"  0x{addr:08X}: REW.{op:02X}")
            ip += 1
        elif op == 0xe8:
            # CALL rel32
            imm = struct.unpack('<i', data[ip+1:ip+5])[0]
            target = addr + 5 + imm
            mnemonic = f"call"
            operands = f"0x{target:08X} (rel {imm:+d})"
            ip += 5
        elif op == 0xe9:
            # JMP rel32
            imm = struct.unpack('<i', data[ip+1:ip+5])[0]
            target = addr + 5 + imm
            mnemonic = f"jmp"
            operands = f"0x{target:08X} (rel {imm:+d})"
            ip += 5
        elif op == 0xe7 or op == 0xe6:
            # MOV r/m64, imm64 (rare)
            if ip+1 < len(data):
                modrm = data[ip+1]
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                if mod == 0 and rm == 4:
                    sib = True
                disp_bytes = 8
                ip += 2 + disp_bytes
            mnemonic = "mov"
            operands = f"{reg64(reg_r)}, imm64"
        elif op == 0x89:
            # MOV r/m64, r64
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                if mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(rm)}+{disp}]"
                elif mod == 0 and rm == 4:
                    sib_byte = data[ip]; ip += 1
                    base_r = (sib_byte & 7) | (8 if rex & 1 else 0)
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(base_r)}+{disp}]"
                elif mod == 3:
                    operands = f"{reg(reg_r, w64)}, {reg(rm, w64)}"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(rm)}+{disp}]"
                mnemonic = "mov"
        elif op == 0x8b:
            # MOV r64, r/m64
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                if mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(rm)}+{disp}]"
                elif mod == 0 and rm == 4:
                    sib_byte = data[ip]; ip += 1
                    base_r = (sib_byte & 7) | (8 if rex & 1 else 0)
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(base_r)}+{disp}]"
                elif mod == 3:
                    operands = f"{reg(reg_r, w64)}, {reg(rm, w64)}"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(rm)}+{disp}]"
                mnemonic = "mov"
        elif op == 0x83:
            # ADD/SUB/AND/OR/XOR/TEST/CMP r/m, imm8
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                imm8 = data[ip]; ip += 1
                if imm8 > 127: imm8 -= 256
                ops = {0:"add",1:"or",2:"adc",3:"sbc",4:"and",5:"sub",6:"xor",7:"test"}
                mnemonic = ops.get(reg_r, f"op{reg_r}")
                if mod == 3:
                    operands = f"{reg(rm, w64)}, {imm8}"
                elif mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"[{reg64(rm)}+{disp}], {imm8}"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"[{reg64(rm)}+{disp}], {imm8}"
        elif op == 0x81:
            # ADD/SUB/AND/OR/XOR/TEST/CMP r/m, imm32
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                imm32 = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                ops = {0:"add",1:"or",2:"adc",3:"sbc",4:"and",5:"sub",6:"xor",7:"test"}
                mnemonic = ops.get(reg_r, f"op{reg_r}")
                if mod == 3:
                    operands = f"{reg(rm, w64)}, {imm32}"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"[{reg64(rm)}+{disp}], {imm32}"
        elif op == 0x8d:
            # LEA
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                if mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(rm)}+{disp}]"
                elif mod == 0 and rm == 4:
                    sib_byte = data[ip]; ip += 1
                    base_r = (sib_byte & 7) | (8 if rex & 1 else 0)
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(base_r)}+{disp}]"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(rm)}+{disp}]"
                mnemonic = "lea"
        elif op == 0x85:
            # TEST
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                if mod == 3:
                    operands = f"{reg(rm, w64)}, {reg(reg_r, w64)}"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"[{reg64(rm)}+{disp}], {reg(reg_r, w64)}"
                mnemonic = "test"
        elif op == 0x89:
            # MOV r/m64, r64
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                if mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(rm)}+{disp}]"
                elif mod == 3:
                    operands = f"{reg(reg_r, w64)}, {reg(rm, w64)}"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, [{reg64(rm)}+{disp}]"
                mnemonic = "mov"
        elif op == 0xc7:
            # MOV r/m, imm32
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                imm32 = struct.unpack('<I', data[ip:ip+4])[0]; ip += 4
                if mod == 3:
                    operands = f"{reg(rm, w64)}, {imm32}"
                elif mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"[{reg64(rm)}+{disp}], {imm32}"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"[{reg64(rm)}+{disp}], {imm32}"
                mnemonic = "mov"
        elif op == 0xc6:
            # MOV r/m8, imm8
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                imm8 = data[ip]; ip += 1
                if mod == 3:
                    operands = f"{reg(rm, w64)}, {imm8}"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"[{reg64(rm)}+{disp}], {imm8}"
                mnemonic = "mov"
        elif op == 0xc0:
            # Shift/rotate with imm8
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                imm8 = data[ip]; ip += 1
                shifts = {0:"rol",1:"ror",2:"rcl",3:"rcr",4:"shl",5:"shr",6:"sal",7:"sar"}
                mnemonic = shifts.get(reg_r, f"shift{reg_r}")
                if mod == 3:
                    operands = f"{reg(rm, w64)}, {imm8}"
                else:
                    operands = f"[mem], {imm8}"
        elif op == 0xc9:
            mnemonic = "leave"
            ip += 1
        elif op == 0xc7:
            # Already handled above
            pass
        elif op == 0x0f:
            # Two-byte opcode
            ip += 1
            if ip < len(data):
                op2 = data[ip]; ip += 1
                if op2 == 0xb6:
                    # MOVZX r64, r/m8
                    modrm = data[ip]; ip += 1
                    mod = (modrm >> 6) & 3
                    rm = (modrm & 7) | (8 if rex & 4 else 0)
                    reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                    if mod == 0 and rm == 5:
                        disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                        operands = f"{reg(reg_r, w64)}, byte [{reg64(rm)}+{disp}]"
                    elif mod == 3:
                        operands = f"{reg(reg_r, w64)}, byte {reg(rm, w64)}"
                    else:
                        disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                        operands = f"{reg(reg_r, w64)}, byte [{reg64(rm)}+{disp}]"
                    mnemonic = "movzx"
                elif op2 == 0xb7:
                    # MOVZX r64, r/m16
                    modrm = data[ip]; ip += 1
                    mod = (modrm >> 6) & 3
                    rm = (modrm & 7) | (8 if rex & 4 else 0)
                    reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                    if mod == 0 and rm == 5:
                        disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                        operands = f"{reg(reg_r, w64)}, word [{reg64(rm)}+{disp}]"
                    elif mod == 3:
                        operands = f"{reg(reg_r, w64)}, word {reg(rm, w64)}"
                    else:
                        disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                        operands = f"{reg(reg_r, w64)}, word [{reg64(rm)}+{disp}]"
                    mnemonic = "movzx"
                elif op2 == 0xb6:
                    mnemonic = "movzx"
                elif op2 == 0xb7:
                    mnemonic = "movzx"
                elif 0x80 <= op2 <= 0x8f:
                    # Conditional jumps
                    ip += 1
                    if ip+3 < len(data):
                        disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                        target = addr + start_ip + (ip - start_ip) + disp
                        target = base + ip + disp
                        jump_names = {
                            0x80:"jo",0x81:"jno",0x82:"jc",0x83:"jnc",
                            0x84:"je",0x85:"jne",0x86:"jl",0x87:"jge",
                            0x88:"jle",0x89:"jg",0x8a:"jp",0x8b:"jnp",
                            0x8c:"js",0x8d:"jns",0x8e:"jpe",0x8f:"jpo"
                        }
                        mnemonic = jump_names.get(op2, f"j?{op2:02x}")
                        target = base + ip + disp
                        operands = f"0x{target:08X} (rel {disp:+d})"
                elif op2 == 0xaf:
                    # IMUL r64, r/m64
                    modrm = data[ip]; ip += 1
                    mod = (modrm >> 6) & 3
                    rm = (modrm & 7) | (8 if rex & 4 else 0)
                    reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                    if mod == 3:
                        operands = f"{reg(rm, w64)}, {reg(reg_r, w64)}"
                    else:
                        disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                        operands = f"[{reg64(rm)}+{disp}], {reg(reg_r, w64)}"
                    mnemonic = "imul"
                elif op2 == 0x84:
                    # JZ/JE rel32
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    target = base + ip + disp
                    mnemonic = "je"
                    operands = f"0x{target:08X} (rel {disp:+d})"
                elif op2 == 0x85:
                    # JNZ/JNE rel32
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    target = base + ip + disp
                    mnemonic = "jne"
                    operands = f"0x{target:08X} (rel {disp:+d})"
                elif op2 == 0x89:
                    # JNS rel32
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    target = base + ip + disp
                    mnemonic = "jns"
                    operands = f"0x{target:08X} (rel {disp:+d})"
                elif op2 == 0x9f:
                    # SETG/SETNLE
                    modrm = data[ip]; ip += 1
                    mod = (modrm >> 6) & 3
                    rm = (modrm & 7) | (8 if rex & 4 else 0)
                    reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                    if mod == 3:
                        operands = f"{reg(rm, w64)}"
                    else:
                        operands = f"[mem]"
                    mnemonic = "setg"
                elif op2 == 0x9c:
                    # SETL
                    modrm = data[ip]; ip += 1
                    rm = (modrm & 7) | (8 if rex & 4 else 0)
                    operands = f"{reg(rm, w64)}"
                    mnemonic = "setl"
                elif op2 == 0x9e:
                    # SETLE
                    modrm = data[ip]; ip += 1
                    rm = (modrm & 7) | (8 if rex & 4 else 0)
                    operands = f"{reg(rm, w64)}"
                    mnemonic = "setle"
                elif op2 == 0x9d:
                    # SETGE
                    modrm = data[ip]; ip += 1
                    rm = (modrm & 7) | (8 if rex & 4 else 0)
                    operands = f"{reg(rm, w64)}"
                    mnemonic = "setge"
                else:
                    mnemonic = f"0x0F 0x{op2:02X}"
                    ip += 2
            else:
                mnemonic = "0x0F (truncated)"
        elif op == 0x63:
            # MOVSXD r32, r/m32
            ip += 1
            if ip < len(data):
                modrm = data[ip]; ip += 1
                mod = (modrm >> 6) & 3
                rm = (modrm & 7) | (8 if rex & 4 else 0)
                reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                if mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, dword [{reg64(rm)}+{disp}]"
                elif mod == 3:
                    operands = f"{reg(reg_r, w64)}, dword {reg(rm, w64)}"
                else:
                    disp = struct.unpack('<i', data[ip:ip+4])[0]; ip += 4
                    operands = f"{reg(reg_r, w64)}, dword [{reg64(rm)}+{disp}]"
                mnemonic = "movsxd"
        elif op == 0x66:
            # Operand size override prefix (16-bit)
            # Could be various instructions with 16-bit operand
            ip += 1
            if ip < len(data):
                # Try to decode next byte
                op2 = data[ip]
                if op2 == 0xaf:
                    # IMUL r16, r/m16, imm8
                    ip += 1
                    modrm = data[ip]; ip += 1
                    rm = (modrm & 7) | (8 if rex & 4 else 0)
                    reg_r = ((modrm >> 3) & 7) | (8 if rex & 2 else 0)
                    imm8 = data[ip]; ip += 1
                    operands = f"{reg16(rm)}, {reg16(reg_r)}, {imm8}"
                    mnemonic = "imul"
                else:
                    mnemonic = f"66h_prefix_0x{op2:02X}"
                    ip += 1
            else:
                mnemonic = "66h_prefix (truncated)"
        elif op == 0x67:
            ip += 1
            mnemonic = "67h_address_size_override"
        elif op == 0x64:
            ip += 1
            mnemonic = "FS_segment_override"
        elif op == 0x65:
            ip += 1
            mnemonic = "GS_segment_override"
        elif op == 0x66:
            ip += 1
            mnemonic = "66h_operand_size"
        elif op == 0xf0:
            ip += 1
            mnemonic = "LOCK_prefix"
        elif op == 0xf2:
            ip += 1
            mnemonic = "REPNE_prefix"
        elif op == 0xf3:
            ip += 1
            mnemonic = "REP_prefix"
        else:
            # Generic fallback
            mnemonic = f"DB 0x{op:02X}"
            ip += 1

        # Format output
        hex_bytes = " ".join(f"{b:02X}" for b in data[start_ip:ip])
        if operands:
            lines.append(f"  0x{addr:08X}: {hex_bytes:<40s} {mnemonic} {operands}")
        else:
            lines.append(f"  0x{addr:08X}: {hex_bytes:<40s} {mnemonic}")

    return lines

def main():
    if len(sys.argv) < 2:
        print("Usage: disasm.py <binary>")
        sys.exit(1)

    with open(sys.argv[1], "rb") as f:
        data = f.read()

    # Find .text section in ELF
    # ELF header: e_type[2], e_machine[2], e_entry[8], e_phoff[8], e_shoff[8]
    if data[:4] != b'\x7fELF':
        print("Not an ELF file!")
        sys.exit(1)

    e_shoff = struct.unpack_from('<I', data, 40)[0]
    e_shentsize = struct.unpack_from('<H', data, 58)[0]
    e_shnum = struct.unpack_from('<H', data, 60)[0]
    e_shstrndx = struct.unpack_from('<H', data, 62)[0]

    # Read section header string table
    shstrtab_off = e_shoff + e_shentsize * e_shstrndx
    shstrtab_size = struct.unpack_from('<I', data, shstrtab_off + 4)[0]
    shstrtab_data = data[shstrtab_off + shstrtab_size:shstrtab_off + shstrtab_size + struct.unpack_from('<I', data, shstrtab_off + 16)[0]]

    # Find .text section
    text_data = None
    text_vaddr = 0
    for i in range(e_shnum):
        sh_off = e_shoff + e_shentsize * i
        sh_name_off = struct.unpack_from('<I', data, sh_off)[0]
        sh_type = struct.unpack_from('<I', data, sh_off + 4)[0]
        sh_flags = struct.unpack_from('<I', data, sh_off + 8)[0]
        sh_addr = struct.unpack_from('<Q', data, sh_off + 16)[0]
        sh_offset = struct.unpack_from('<Q', data, sh_off + 24)[0]
        sh_size = struct.unpack_from('<Q', data, sh_off + 32)[0]

        name = ""
        for j in range(len(shstrtab_data)):
            if shstrtab_data[j] == 0:
                break
            name += chr(shstrtab_data[j])

        if name == ".text":
            text_data = data[sh_offset:sh_offset + sh_size]
            text_vaddr = sh_addr
            break

    if text_data is None:
        print("No .text section found!")
        sys.exit(1)

    print(f"\n=== Disassembly of .text section ({len(text_data)} bytes at 0x{text_vaddr:08X}) ===\n")
    for line in disasm(text_data, text_vaddr):
        print(line)

if __name__ == "__main__":
    main()
