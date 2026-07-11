#!/usr/bin/env python3
"""Simple x86-64 disassembler for analyzing tc compiler output."""
import struct, sys

def extract_text(filename):
    with open(filename, 'rb') as f:
        data = f.read()
    if data[:4] != b'\x7fELF':
        print("Not ELF"); sys.exit(1)
    ehdr = data[:64]
    ei_class = ehdr[4]
    if ei_class != 2:
        print("Not 64-bit"); sys.exit(1)
    e_shoff = struct.unpack_from('<Q', ehdr, 40)[0]
    e_shentsize = struct.unpack_from('<H', ehdr, 58)[0]
    e_shnum = struct.unpack_from('<H', ehdr, 60)[0]
    e_shstrndx = struct.unpack_from('<H', ehdr, 62)[0]
    
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        shdr = data[off:off+e_shentsize]
        sections.append({
            'name_off': struct.unpack_from('<I', shdr, 0)[0],
            'type': struct.unpack_from('<I', shdr, 4)[0],
            'addr': struct.unpack_from('<Q', shdr, 24)[0],
            'offset': struct.unpack_from('<Q', shdr, 32)[0],
            'size': struct.unpack_from('<Q', shdr, 40)[0],
        })
    
    strtab = sections[e_shstrndx]
    str_data = data[strtab['offset']:strtab['offset']+strtab['size']]
    
    for sec in sections:
        name = str_data[sec['name_off']:].split(b'\x00')[0].decode()
        sec['name'] = name
        if name == '.text':
            return data[sec['offset']:sec['offset']+sec['size']], sec['addr']
    print("No .text"); sys.exit(1)

R64 = ["RAX","RCX","RDX","RBX","RSP","RSI","RDI","RBP","R8","R9","R10","R11","R12","R13","R14","R15"]

def disasm(data, base):
    ip = base
    pos = 0
    lines = []
    while pos < len(data):
        start = pos
        ip_start = ip
        b0 = data[pos]; pos += 1
        
        # REX prefix
        rex = 0
        if 0x40 <= b0 <= 0x4F:
            rex = b0
            b0 = data[pos]; pos += 1
        
        # Helper: read modrm
        def read_modrm():
            m = data[pos]; pos += 1
            return m
        
        def read_imm8():
            v = data[pos]; pos += 1
            return v
        
        def read_imm32():
            v = struct.unpack_from('<I', data, pos)[0]; pos += 4
            return v
        
        def read_imm32s():
            v = struct.unpack_from('<i', data, pos)[0]; pos += 4
            return v
        
        def read_imm16():
            v = struct.unpack_from('<H', data, pos)[0]; pos += 2
            return v
        
        def decode_modrm_rm(mod, rm, has_rex_b):
            """Decode rm field, handling SIB and disp."""
            nonlocal pos
            if has_rex_b:
                rm |= 8
            if mod == 3:
                return R64[rm & 7] if rm < 8 else R64[rm]
            # Memory addressing
            disp = 0
            if mod == 0 and rm == 5:
                disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
            elif mod == 1:
                disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
            elif mod == 2:
                disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
            # SIB byte when rm==4 and not (mod==0,rm==5)
            if rm == 4 and not (mod == 0 and (data[pos-1] if disp==0 else True)):
                pass  # simplified
            if mod == 0 and rm != 5:
                if rm == 4:
                    sib = data[pos]; pos += 1
                    scale = (sib >> 6) & 3
                    idx = (sib >> 3) & 7
                    base_r = sib & 7
                    if has_rex_b: base_r |= 8
                    scales = [1,2,4,8]
                    idx_names = [R64[i] for i in range(8)] + [R64[8],R64[9],R64[10],R64[11]]
                    if idx == 4:
                        return f"[{R64[base_r]}+{disp}]"
                    return f"[{R64[base_r]}+{idx_names[idx]}*{scales[scale]}+{disp}]"
            return f"[{R64[rm & 7]}+{disp}]"
        
        def decode_modrm_reg(mod, reg, has_rex_r):
            if has_rex_r:
                reg |= 8
            return R64[reg & 7] if reg < 8 else R64[reg]
        
        def parse_modrm():
            m = data[pos]; pos += 1
            mod = (m >> 6) & 3
            reg = (m >> 3) & 7
            rm = m & 7
            has_rex_r = bool(rex & 0x08)
            has_rex_b = bool(rex & 0x04)
            has_rex_w = bool(rex & 0x08)  # Actually REX.W is bit 3
            has_rex_w = bool(rex & 0x08)
            return mod, reg, rm, has_rex_r, has_rex_b
        
        # Now decode instruction
        if b0 == 0x50:
            m = read_modrm()  # No, 0x50-0x5F are single-byte push/pop
            pass  # handled below
        elif 0x50 <= b0 <= 0x57:
            reg = b0 - 0x50
            if rex & 0x04: reg |= 8
            lines.append(f"0x{ip_start:08x}  PUSH {R64[reg]}")
        elif 0x58 <= b0 <= 0x5F:
            reg = b0 - 0x58
            if rex & 0x04: reg |= 8
            lines.append(f"0x{ip_start:08x}  POP {R64[reg]}")
        elif b0 == 0xc3:
            lines.append(f"0x{ip_start:08x}  RET")
        elif b0 == 0xc9:
            lines.append(f"0x{ip_start:08x}  LEAVE")
        elif b0 == 0xe8:
            rel = read_imm32s()
            target = ip_start + (pos - ip_start) + rel
            lines.append(f"0x{ip_start:08x}  CALL 0x{target:08x}  ; rel={rel:+d}")
        elif b0 == 0xe9:
            rel = read_imm32s()
            target = ip_start + (pos - ip_start) + rel
            lines.append(f"0x{ip_start:08x}  JMP 0x{target:08x}  ; rel={rel:+d} ({rel:+d})")
        elif b0 == 0x0f:
            b1 = data[pos]; pos += 1
            if b1 == 0x84:
                m = read_modrm()
                mod = (m >> 6) & 3
                rel = read_imm32s()
                target = ip_start + (pos - ip_start) + rel
                lines.append(f"0x{ip_start:08x}  JAE/JNB 0x{target:08x}  ; rel={rel:+d}")
            elif b1 == 0x85:
                m = read_modrm()
                mod = (m >> 6) & 3
                rel = read_imm32s()
                target = ip_start + (pos - ip_start) + rel
                lines.append(f"0x{ip_start:08x}  JNE/JNZ 0x{target:08x}  ; rel={rel:+d}")
            elif b1 == 0x84:
                m = read_modrm()
                rel = read_imm32s()
                target = ip_start + (pos - ip_start) + rel
                lines.append(f"0x{ip_start:08x}  JAE 0x{target:08x}  ; rel={rel:+d}")
            elif b1 == 0x85:
                m = read_modrm()
                rel = read_imm32s()
                target = ip_start + (pos - ip_start) + rel
                lines.append(f"0x{ip_start:08x}  JNE 0x{target:08x}  ; rel={rel:+d}")
            elif b1 == 0x8e:
                m = read_modrm()
                rel = read_imm32s()
                target = ip_start + (pos - ip_start) + rel
                lines.append(f"0x{ip_start:08x}  JLE 0x{target:08x}  ; rel={rel:+d}")
            elif b1 == 0x8f:
                m = read_modrm()
                rel = read_imm32s()
                target = ip_start + (pos - ip_start) + rel
                lines.append(f"0x{ip_start:08x}  JG 0x{target:08x}  ; rel={rel:+d}")
            elif b1 == 0x85:
                m = read_modrm()
                rel = read_imm32s()
                target = ip_start + (pos - ip_start) + rel
                lines.append(f"0x{ip_start:08x}  JNZ 0x{target:08x}  ; rel={rel:+d}")
            elif b1 == 0x84:
                m = read_modrm()
                rel = read_imm32s()
                target = ip_start + (pos - ip_start) + rel
                lines.append(f"0x{ip_start:08x}  JAE 0x{target:08x}  ; rel={rel:+d}")
            elif b1 == 0xb6:
                # MOVZX r64, r/m8
                m = read_modrm()
                mod = (m >> 6) & 3
                reg = (m >> 3) & 7
                rm = m & 7
                has_rex_r = bool(rex & 0x08)
                has_rex_b = bool(rex & 0x04)
                if has_rex_r: reg |= 8
                if has_rex_b: rm |= 8
                if mod == 3:
                    lines.append(f"0x{ip_start:08x}  MOVZX {R64[reg]}, {R64[rm]}")
                else:
                    disp = 0
                    if mod == 0 and rm == 5:
                        disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                    elif mod == 1:
                        disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
                    elif mod == 2:
                        disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                    lines.append(f"0x{ip_start:08x}  MOVZX {R64[reg]}, byte [{R64[rm & 7]}+{disp}]")
            elif b1 == 0xb7:
                # MOVZX r64, r/m16
                m = read_modrm()
                mod = (m >> 6) & 3
                reg = (m >> 3) & 7
                rm = m & 7
                has_rex_r = bool(rex & 0x08)
                has_rex_b = bool(rex & 0x04)
                if has_rex_r: reg |= 8
                if has_rex_b: rm |= 8
                if mod == 3:
                    lines.append(f"0x{ip_start:08x}  MOVZX {R64[reg]}, {R64[rm]}")
                else:
                    disp = 0
                    if mod == 0 and rm == 5:
                        disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                    elif mod == 1:
                        disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
                    elif mod == 2:
                        disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                    lines.append(f"0x{ip_start:08x}  MOVZX {R64[reg]}, word [{R64[rm & 7]}+{disp}]")
            elif b1 == 0x9f:
                # SETG r/m8
                m = read_modrm()
                mod = (m >> 6) & 3
                rm = m & 7
                has_rex_b = bool(rex & 0x04)
                if has_rex_b: rm |= 8
                lines.append(f"0x{ip_start:08x}  SETG r/m8  ; modrm=0x{m:02x}")
            elif b1 == 0x89:
                # CMP r/m32, imm32 (no, this is group 3)
                m = read_modrm()
                imm32 = read_imm32()
                lines.append(f"0x{ip_start:08x}  CMP r/m32, {imm32}")
            else:
                lines.append(f"0x{ip_start:08x}  2BYTE_0x{b1:02x}")
        elif b0 == 0x8b:
            # MOV r64, r/m64
            m = read_modrm()
            mod = (m >> 6) & 3
            reg = (m >> 3) & 7
            rm = m & 7
            has_rex_r = bool(rex & 0x08)
            has_rex_b = bool(rex & 0x04)
            if has_rex_r: reg |= 8
            if has_rex_b: rm |= 8
            if mod == 3:
                lines.append(f"0x{ip_start:08x}  MOV {R64[reg]}, {R64[rm]}")
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                elif mod == 1:
                    disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
                elif mod == 2:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                lines.append(f"0x{ip_start:08x}  MOV {R64[reg]}, [{R64[rm & 7]}+{disp}]")
        elif b0 == 0x89:
            # MOV r/m64, r64
            m = read_modrm()
            mod = (m >> 6) & 3
            reg = (m >> 3) & 7
            rm = m & 7
            has_rex_r = bool(rex & 0x08)
            has_rex_b = bool(rex & 0x04)
            if has_rex_r: reg |= 8
            if has_rex_b: rm |= 8
            if mod == 3:
                lines.append(f"0x{ip_start:08x}  MOV {R64[rm]}, {R64[reg]}")
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                elif mod == 1:
                    disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
                elif mod == 2:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                lines.append(f"0x{ip_start:08x}  MOV [{R64[rm & 7]}+{disp}], {R64[reg]}")
        elif b0 == 0x8d:
            # LEA r64, r/m64
            m = read_modrm()
            mod = (m >> 6) & 3
            reg = (m >> 3) & 7
            rm = m & 7
            has_rex_r = bool(rex & 0x08)
            has_rex_b = bool(rex & 0x04)
            if has_rex_r: reg |= 8
            if has_rex_b: rm |= 8
            disp = 0
            if mod == 0 and rm == 5:
                disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
            elif mod == 1:
                disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
            elif mod == 2:
                disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
            lines.append(f"0x{ip_start:08x}  LEA {R64[reg]}, [{R64[rm & 7]}+{disp}]")
        elif b0 == 0x83:
            # ADD/SUB/etc r/m32, imm8
            m = read_modrm()
            mod = (m >> 6) & 3
            reg_op = (m >> 3) & 7
            rm = m & 7
            has_rex_b = bool(rex & 0x04)
            if has_rex_b: rm |= 8
            imm8 = read_imm8()
            ops = ["ADD","OR","ADC","SBB","AND","SUB","XOR","CMP"]
            disp = 0
            if mod == 0 and rm == 5:
                disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
            elif mod == 1:
                disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
            elif mod == 2:
                disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
            lines.append(f"0x{ip_start:08x}  {ops[reg_op]} [{R64[rm & 7]}+{disp}], {imm8}")
        elif b0 == 0x85:
            # TEST r/m32, r32
            m = read_modrm()
            mod = (m >> 6) & 3
            reg = (m >> 3) & 7
            rm = m & 7
            has_rex_r = bool(rex & 0x08)
            has_rex_b = bool(rex & 0x04)
            if has_rex_r: reg |= 8
            if has_rex_b: rm |= 8
            if mod == 3:
                lines.append(f"0x{ip_start:08x}  TEST {R64[rm]}, {R64[reg]}")
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                elif mod == 1:
                    disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
                elif mod == 2:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                lines.append(f"0x{ip_start:08x}  TEST [{R64[rm & 7]}+{disp}], {R64[reg]}")
        elif b0 == 0xc7:
            # MOV r/m64, imm32
            m = read_modrm()
            mod = (m >> 6) & 3
            reg = (m >> 3) & 7
            rm = m & 7
            has_rex_b = bool(rex & 0x04)
            if has_rex_b: rm |= 8
            imm32 = read_imm32()
            disp = 0
            if mod == 0 and rm == 5:
                disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
            elif mod == 1:
                disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
            elif mod == 2:
                disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
            if mod == 3:
                lines.append(f"0x{ip_start:08x}  MOV {R64[rm]}, {imm32}")
            else:
                lines.append(f"0x{ip_start:08x}  MOV [{R64[rm & 7]}+{disp}], {imm32}")
        elif b0 == 0x63:
            # MOVSXD r64, r/m32
            m = read_modrm()
            mod = (m >> 6) & 3
            reg = (m >> 3) & 7
            rm = m & 7
            has_rex_r = bool(rex & 0x08)
            has_rex_b = bool(rex & 0x04)
            if has_rex_r: reg |= 8
            if has_rex_b: rm |= 8
            if mod == 3:
                lines.append(f"0x{ip_start:08x}  MOVSXD {R64[reg]}, {R64[rm]}")
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                elif mod == 1:
                    disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
                elif mod == 2:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                lines.append(f"0x{ip_start:08x}  MOVSXD {R64[reg]}, [{R64[rm & 7]}+{disp}]")
        elif b0 == 0x0f:
            # Already handled above
            pass
        elif b0 == 0x39:
            # CMP r/m32, r32
            m = read_modrm()
            mod = (m >> 6) & 3
            reg = (m >> 3) & 7
            rm = m & 7
            has_rex_r = bool(rex & 0x08)
            has_rex_b = bool(rex & 0x04)
            if has_rex_r: reg |= 8
            if has_rex_b: rm |= 8
            if mod == 3:
                lines.append(f"0x{ip_start:08x}  CMP {R64[rm]}, {R64[reg]}")
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                elif mod == 1:
                    disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
                elif mod == 2:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                lines.append(f"0x{ip_start:08x}  CMP [{R64[rm & 7]}+{disp}], {R64[reg]}")
        elif b0 == 0xaf:
            # IMUL r/m64
            m = read_modrm()
            mod = (m >> 6) & 3
            rm = m & 7
            has_rex_b = bool(rex & 0x04)
            if has_rex_b: rm |= 8
            if mod == 3:
                lines.append(f"0x{ip_start:08x}  IMUL {R64[rm]}")
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                elif mod == 1:
                    disp = struct.unpack_from('<b', data, pos)[0]; pos += 1
                elif mod == 2:
                    disp = struct.unpack_from('<i', data, pos)[0]; pos += 4
                lines.append(f"0x{ip_start:08x}  IMUL [{R64[rm & 7]}+{disp}]")
        elif b0 == 0x63:
            # Already handled
            pass
        elif 0x40 <= b0 <= 0x4F:
            # Already handled as REX
            pass
        else:
            # Unknown
            inst_bytes = data[start:pos]
            hex_str = ' '.join(f'{b:02x}' for b in inst_bytes)
            lines.append(f"0x{ip_start:08x}  {hex_str}  ; UNKNOWN")
        
        ip = pos
    
    return lines

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 quick_disasm.py <elf_file>")
        sys.exit(1)
    
    text_data, text_addr = extract_text(sys.argv[1])
    print(f"\n=== .text section: {len(text_data)} bytes at 0x{text_addr:08x} ===\n")
    
    # Print hex dump
    for i in range(0, len(text_data), 16):
        addr = text_addr + i
        chunk = text_data[i:i+16]
        hex_part = ' '.join(f'{b:02x}' for b in chunk)
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"  0x{addr:08x}: {hex_part:<48s} ; {ascii_part}")
    
    print(f"\n=== Disassembly ===\n")
    lines = disasm(text_data, text_addr)
    for line in lines:
        print(line)

if __name__ == '__main__':
    main()
