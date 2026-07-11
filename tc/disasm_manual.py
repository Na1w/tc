#!/usr/bin/env python3
"""Manual x86-64 disassembler for analyzing compiler output."""

import struct
import sys

# x86-64 opcode map (subset for our needs)
OPCODES = {
    0x48: "REX.W",  # REX.W prefix (64-bit)
    0x41: "REX.R",  # REX prefix with R bit
    0x44: "REX.RW", # REX prefix with R+W
    0x45: "REX.RW+",# REX prefix with R+W+...
    0x4a: "REX.RW+",# REX prefix
    0x4b: "REX.RW+",# REX prefix
    0x4f: "REX.B",  # REX prefix with B
    0x55: "PUSH RBP",
    0x51: "PUSH RCX",
    0x52: "PUSH RDX",
    0x56: "PUSH RSI",
    0x57: "PUSH RDI",
    0x5d: "POP RBP",
    0x5f: "POP RDI",
    0x5e: "POP RSI",
    0x5a: "POP RDX",
    0x59: "POP RCX",
    0xc3: "RET",
    0xc9: "LEAVE",
    0xe8: "CALL rel32",
    0xe9: "JMP rel32",
    0x0f: "2BYTE_ESCAPE",
}

# ModR/M register encoding
REG_NAMES = ["AL/AX/EAX/RAX", "CL/CX/ECX/RCX", "DL/DX/EDX/RDX", "BL/BX/EBX/RBX",
             "AH/SIL/DIL/RSI", "CH/DIL/DI/rdi", "BH/PL/BPL/R8", "SP/BPL/BPL/R9"]
REG_NAMES64 = ["RAX", "RCX", "RDX", "RBX", "RSP", "RSI", "RDI", "RBP",
               "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"]

SIB_BASE = ["RAX", "RCX", "RDX", "RBX", "RSP", "RSI", "RDI", "RBP"]
SIB_INDEX = ["none", "RAX", "RCX", "RDX", "RBX", "RSP", "RSI", "RBP"]

def decode_sib(sib_byte, base_reg, scale, idx, disp_size):
    """Decode SIB byte."""
    scale = (sib_byte >> 6) & 3
    idx = (sib_byte >> 3) & 7
    base = sib_byte & 7
    scales = [1, 2, 4, 8]
    return f"[{SIB_BASE[base]} + {SIB_INDEX[idx]}*{scales[scale]}]"

def disassemble_bytes(data, base_addr=0x401000):
    """Disassemble x86-64 bytes."""
    ip = base_addr
    pos = 0
    instructions = []
    
    while pos < len(data):
        inst_start = pos
        inst_ip = ip
        opcode = data[pos]
        pos += 1
        
        # Detect REX prefix (0x40-0x4F)
        rex = 0
        if 0x40 <= opcode <= 0x4F:
            rex = opcode
            opcode = data[pos]
            pos += 1
        
        # 2-byte opcode escape
        is_two_byte = False
        if opcode == 0x0F:
            is_two_byte = True
            opcode2 = data[pos]
            pos += 1
        
        # Determine instruction and operand size
        mnemonic = ""
        operand_bytes = 0
        
        if opcode == 0x55:
            mnemonic = "PUSH RBP"
        elif opcode == 0x51:
            mnemonic = "PUSH RCX"
        elif opcode == 0x52:
            mnemonic = "PUSH RDX"
        elif opcode == 0x56:
            mnemonic = "PUSH RSI"
        elif opcode == 0x57:
            mnemonic = "PUSH RDI"
        elif opcode == 0x5d:
            mnemonic = "POP RBP"
        elif opcode == 0x5f:
            mnemonic = "POP RDI"
        elif opcode == 0x5e:
            mnemonic = "POP RSI"
        elif opcode == 0x5a:
            mnemonic = "POP RDX"
        elif opcode == 0x59:
            mnemonic = "POP RCX"
        elif opcode == 0x5b:
            mnemonic = "POP RBX"
        elif opcode == 0xc3:
            mnemonic = "RET"
        elif opcode == 0xc9:
            mnemonic = "LEAVE"
        elif opcode == 0xe8:
            # CALL rel32
            rel = struct.unpack('<i', data[pos:pos+4])[0]
            target = inst_ip + 5 + rel
            mnemonic = f"CALL 0x{target:08x}  ; rel={rel:+d}"
            pos += 4
        elif opcode == 0xe9:
            # JMP rel32
            rel = struct.unpack('<i', data[pos:pos+4])[0]
            target = inst_ip + 5 + rel
            mnemonic = f"JMP 0x{target:08x}  ; rel={rel:+d}"
            pos += 4
        elif is_two_byte:
            if opcode2 == 0x84:
                # JAE/JNB rel32
                modrm = data[pos]; pos += 1
                rel = struct.unpack('<i', data[pos:pos+4])[0]
                target = inst_ip + (pos - inst_ip) + rel
                mnemonic = f"JAE/JNB 0x{target:08x}  ; rel={rel:+d}"
            elif opcode2 == 0x85:
                # JNE/JNZ rel32
                modrm = data[pos]; pos += 1
                rel = struct.unpack('<i', data[pos:pos+4])[0]
                target = inst_ip + (pos - inst_ip) + rel
                mnemonic = f"JNE/JNZ 0x{target:08x}  ; rel={rel:+d}"
            elif opcode2 == 0x85:
                # JNZ
                modrm = data[pos]; pos += 1
                rel = struct.unpack('<i', data[pos:pos+4])[0]
                target = inst_ip + (pos - inst_ip) + rel
                mnemonic = f"JNZ 0x{target:08x}  ; rel={rel:+d}"
            elif opcode2 == 0xb6:
                # MOVZX r64, r/m8 (with REX.W)
                modrm = data[pos]; pos += 1
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                reg = (modrm >> 3) & 7
                if rex & 0x04: reg |= 8  # REX.B
                if rex & 0x08: rm |= 8   # REX.R (swap for modrm)
                # Actually REX.R affects reg field, REX.B affects rm field
                reg_bits = (modrm >> 3) & 7
                rm_bits = modrm & 7
                if rex & 0x08: reg_bits |= 8
                if rex & 0x04: rm_bits |= 8
                if mod == 3:
                    mnemonic = f"MOVZX {REG_NAMES64[reg_bits]}, {REG_NAMES64[rm_bits]}"
                else:
                    # Memory operand
                    disp = 0
                    if mod == 0 and rm == 5:
                        disp = struct.unpack('<i', data[pos:pos+4])[0]
                        pos += 4
                    elif mod == 1:
                        disp = struct.unpack('<b', data[pos:pos+1])[0]
                        pos += 1
                    elif mod == 2:
                        disp = struct.unpack('<i', data[pos:pos+4])[0]
                        pos += 4
                    mnemonic = f"MOVZX {REG_NAMES64[reg_bits]}, byte [{SIB_BASE[rm_bits]}+{disp}]"
            elif opcode2 == 0xb7:
                # MOVZX r64, r/m16 (with REX.W)
                modrm = data[pos]; pos += 1
                reg_bits = (modrm >> 3) & 7
                rm_bits = modrm & 7
                if rex & 0x08: reg_bits |= 8
                if rex & 0x04: rm_bits |= 8
                mod = (modrm >> 6) & 3
                if mod == 3:
                    mnemonic = f"MOVZX {REG_NAMES64[reg_bits]}, {REG_NAMES64[rm_bits]}"
                else:
                    disp = 0
                    if mod == 0 and rm == 5:
                        disp = struct.unpack('<i', data[pos:pos+4])[0]
                        pos += 4
                    elif mod == 1:
                        disp = struct.unpack('<b', data[pos:pos+1])[0]
                        pos += 1
                    elif mod == 2:
                        disp = struct.unpack('<i', data[pos:pos+4])[0]
                        pos += 4
                    mnemonic = f"MOVZX {REG_NAMES64[reg_bits]}, word [{SIB_BASE[rm_bits]}+{disp}]"
            elif opcode2 == 0x9f:
                # SETG/SETNLE AL
                modrm = data[pos]; pos += 1
                reg_bits = (modrm >> 3) & 7
                rm_bits = modrm & 7
                if rex & 0x08: reg_bits |= 8
                if rex & 0x04: rm_bits |= 8
                mnemonic = f"SETG/SETNLE r/m8"
            elif opcode2 == 0x89:
                # CMP rel32 (shouldn't happen here, handled above)
                pass
            else:
                mnemonic = f"2BYTE_0x{opcode2:02x}"
                # Skip modrm if needed
                modrm = data[pos]; pos += 1
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod != 3:
                    if (mod == 0 and rm == 5) or mod == 2:
                        pos += 4
                    elif mod == 1:
                        pos += 1
                    # Check for SIB
                    if mod != 0 or rm != 4:
                        pass
        elif opcode == 0x8b:
            # MOV r64, r/m64 (with REX.W)
            modrm = data[pos]; pos += 1
            reg_bits = (modrm >> 3) & 7
            rm_bits = modrm & 7
            if rex & 0x08: reg_bits |= 8
            if rex & 0x04: rm_bits |= 8
            mod = (modrm >> 6) & 3
            if mod == 3:
                mnemonic = f"MOV {REG_NAMES64[reg_bits]}, {REG_NAMES64[rm_bits]}"
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[pos:pos+4])[0]
                    pos += 4
                    mnemonic = f"MOV {REG_NAMES64[reg_bits]}, [{0x401000 + disp}]"
                elif mod == 1:
                    disp = struct.unpack('<b', data[pos:pos+1])[0]
                    pos += 1
                    mnemonic = f"MOV {REG_NAMES64[reg_bits]}, [{SIB_BASE[rm_bits]}+{disp}]"
                elif mod == 2:
                    disp = struct.unpack('<i', data[pos:pos+4])[0]
                    pos += 4
                    mnemonic = f"MOV {REG_NAMES64[reg_bits]}, [{SIB_BASE[rm_bits]}+{disp}]"
                elif mod == 0:
                    # Check for SIB
                    if rm == 4:
                        sib = data[pos]; pos += 1
                        mnemonic = f"MOV {REG_NAMES64[reg_bits]}, [SIB]"
                    else:
                        disp = struct.unpack('<i', data[pos:pos+4])[0]
                        pos += 4
                        mnemonic = f"MOV {REG_NAMES64[reg_bits]}, [{disp}]"
        elif opcode == 0x89:
            # MOV r/m64, r64 (with REX.W)
            modrm = data[pos]; pos += 1
            reg_bits = (modrm >> 3) & 7
            rm_bits = modrm & 7
            if rex & 0x08: reg_bits |= 8
            if rex & 0x04: rm_bits |= 8
            mod = (modrm >> 6) & 3
            if mod == 3:
                mnemonic = f"MOV {REG_NAMES64[rm_bits]}, {REG_NAMES64[reg_bits]}"
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[pos:pos+4])[0]
                    pos += 4
                elif mod == 1:
                    disp = struct.unpack('<b', data[pos:pos+1])[0]
                    pos += 1
                elif mod == 2:
                    disp = struct.unpack('<i', data[pos:pos+4])[0]
                    pos += 4
                if mod == 0 and rm == 4:
                    sib = data[pos]; pos += 1
                mnemonic = f"MOV [{SIB_BASE[rm_bits]}+{disp}], {REG_NAMES64[reg_bits]}"
        elif opcode == 0x8d:
            # LEA r64, r/m64
            modrm = data[pos]; pos += 1
            reg_bits = (modrm >> 3) & 7
            rm_bits = modrm & 7
            if rex & 0x08: reg_bits |= 8
            if rex & 0x04: rm_bits |= 8
            mod = (modrm >> 6) & 3
            disp = 0
            if mod == 0 and rm == 5:
                disp = struct.unpack('<i', data[pos:pos+4])[0]
                pos += 4
            elif mod == 1:
                disp = struct.unpack('<b', data[pos:pos+1])[0]
                pos += 1
            elif mod == 2:
                disp = struct.unpack('<i', data[pos:pos+4])[0]
                pos += 4
            if mod == 0 and rm != 5:
                if rm == 4:
                    sib = data[pos]; pos += 1
            mnemonic = f"LEA {REG_NAMES64[reg_bits]}, [{SIB_BASE[rm_bits]}+{disp}]"
        elif opcode == 0x83:
            # ADD/SUB/... r/m32, imm8
            modrm = data[pos]; pos += 1
            imm8 = data[pos]; pos += 1
            reg_op = (modrm >> 3) & 7
            rm_bits = modrm & 7
            if rex & 0x04: rm_bits |= 8
            mod = (modrm >> 6) & 3
            ops = ["ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"]
            disp = 0
            if mod == 0 and rm == 5:
                disp = struct.unpack('<i', data[pos:pos+4])[0]
                pos += 4
            elif mod == 1:
                disp = struct.unpack('<b', data[pos:pos+1])[0]
                pos += 1
            elif mod == 2:
                disp = struct.unpack('<i', data[pos:pos+4])[0]
                pos += 4
            mnemonic = f"{ops[reg_op]} [{SIB_BASE[rm_bits]}+{disp}], {imm8}"
        elif opcode == 0x85:
            # TEST r/m32, r32
            modrm = data[pos]; pos += 1
            reg_bits = (modrm >> 3) & 7
            rm_bits = modrm & 7
            if rex & 0x08: reg_bits |= 8
            if rex & 0x04: rm_bits |= 8
            mod = (modrm >> 6) & 3
            if mod == 3:
                mnemonic = f"TEST {REG_NAMES64[rm_bits]}, {REG_NAMES64[reg_bits]}"
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[pos:pos+4])[0]
                    pos += 4
                elif mod == 1:
                    disp = struct.unpack('<b', data[pos:pos+1])[0]
                    pos += 1
                elif mod == 2:
                    disp = struct.unpack('<i', data[pos:pos+4])[0]
                    pos += 4
                mnemonic = f"TEST [{SIB_BASE[rm_bits]}+{disp}], {REG_NAMES64[reg_bits]}"
        elif opcode == 0xc7:
            # MOV r/m64, imm32 (with REX.W)
            modrm = data[pos]; pos += 1
            imm32 = struct.unpack('<I', data[pos:pos+4])[0]
            pos += 4
            reg_bits = (modrm >> 3) & 7
            rm_bits = modrm & 7
            if rex & 0x04: rm_bits |= 8
            mod = (modrm >> 6) & 3
            disp = 0
            if mod == 0 and rm == 5:
                disp = struct.unpack('<i', data[pos:pos+4])[0]
                pos += 4
            elif mod == 1:
                disp = struct.unpack('<b', data[pos:pos+1])[0]
                pos += 1
            elif mod == 2:
                disp = struct.unpack('<i', data[pos:pos+4])[0]
                pos += 4
            if mod == 3:
                mnemonic = f"MOV {REG_NAMES64[rm_bits]}, {imm32}"
            else:
                mnemonic = f"MOV [{SIB_BASE[rm_bits]}+{disp}], {imm32}"
        elif opcode == 0xc6:
            # MOV r/m8, imm8
            modrm = data[pos]; pos += 1
            imm8 = data[pos]; pos += 1
            rm_bits = modrm & 7
            if rex & 0x04: rm_bits |= 8
            mod = (modrm >> 6) & 3
            if mod == 3:
                mnemonic = f"MOV {REG_NAMES64[rm_bits]}, {imm8}"
            else:
                disp = 0
                if mod == 0 and rm == 5:
                    disp = struct.unpack('<i', data[pos:pos+4])[0]
                    pos += 4
                elif mod == 1:
                    disp = struct.unpack('<b', data[pos:pos+1])[0]
                    pos += 1
                elif mod == 2:
                    disp = struct.unpack('<i', data[pos:pos+4])[0]
                    pos += 4
                mnemonic = f"MOV [{SIB_BASE[rm_bits]}+{disp}], {imm8}"
        elif opcode == 0xc7:
            # Already handled above
            pass
        elif opcode == 0xb8:
            # MOV RAX, imm32 (with REX.W)
            imm32 = struct.unpack('<I', data[pos:pos+4])[0]
            pos += 4
            mnemonic = f"MOV RAX, {imm32}"
        elif opcode == 0xb9:
            imm32 = struct.unpack('<I', data[pos:pos+4])[0]
            pos += 4
            mnemonic = f"MOV RCX, {imm32}"
        elif opcode == 0xba:
            imm32 = struct.unpack('<I', data[pos:pos+4])[0]
            pos += 4
            mnemonic = f"MOV RDX, {imm32}"
        elif opcode == 0xbb:
            imm32 = struct.unpack('<I', data[pos:pos+4])[0]
            pos += 4
            mnemonic = f"MOV RBX, {imm32}"
        elif opcode == 0xc0:
            # Shift/rotate with imm8
            modrm = data[pos]; pos += 1
            imm8 = data[pos]; pos += 1
            mnemonic = f"SHIFT {modrm} {imm8}"
        elif opcode == 0xb0:
            imm8 = data[pos]; pos += 1
            mnemonic = f"MOV AL, {imm8}"
        elif opcode == 0xb1:
            imm8 = data[pos]; pos += 1
            mnemonic = f"MOV CL, {imm8}"
        elif opcode == 0xb2:
            imm8 = data[pos]; pos += 1
            mnemonic = f"MOV DL, {imm8}"
        elif opcode == 0xb3:
            imm8 = data[pos]; pos += 1
            mnemonic = f"MOV BL, {imm8}"
        elif opcode == 0x8b:
            # Already handled
            pass
        elif opcode == 0x89:
            # Already handled
            pass
        elif opcode == 0x8d:
            # Already handled
            pass
        elif opcode == 0x83:
            # Already handled
            pass
        elif opcode == 0x85:
            # Already handled
            pass
        elif opcode == 0x0f:
            # Already handled (2-byte)
            pass
        else:
            # Unknown - try to decode with modrm
            if pos < len(data):
                modrm = data[pos]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                pos += 1
                if mod != 3:
                    if mod == 0 and rm == 5:
                        pos += 4
                    elif mod == 1:
                        pos += 1
                    elif mod == 2:
                        pos += 4
                    if mod != 0 or rm != 4:
                        if (mod == 0 and rm == 4) or (mod != 0 and rm == 4):
                            pos += 1  # SIB byte
            mnemonic = f"??? 0x{data[inst_start]:02x}"
        
        inst_bytes = data[inst_start:pos]
        hex_str = ' '.join(f'{b:02x}' for b in inst_bytes)
        instructions.append(f"  0x{inst_ip:08x}: {hex_str:<40s} {mnemonic}")
        ip = pos
    
    return instructions

def extract_text_section(filename):
    """Extract .text section from ELF."""
    with open(filename, 'rb') as f:
        magic = f.read(4)
        if magic != b'\x7fELF':
            print(f"ERROR: Not an ELF file: {filename}")
            return None
        
        # Read ELF header
        f.seek(0)
        ehdr = f.read(64)
        ei_class = ehdr[4]  # 1=32bit, 2=64bit
        ei_data = ehdr[5]   # 1=LE, 2=BE
        
        if ei_class == 2:  # 64-bit
            e_shoff = struct.unpack_from('<Q', ehdr, 40)[0]
            e_shentsize = struct.unpack_from('<H', ehdr, 58)[0]
            e_shnum = struct.unpack_from('<H', ehdr, 60)[0]
            e_shstrndx = struct.unpack_from('<H', ehdr, 62)[0]
        else:
            print("ERROR: 32-bit ELF not supported")
            return None
        
        # Read section headers
        f.seek(e_shoff)
        sections = []
        for i in range(e_shnum):
            shdr = f.read(e_shentsize)
            sh_name_off = struct.unpack_from('<I', shdr, 0)[0]
            sh_type = struct.unpack_from('<I', shdr, 4)[0]
            sh_addr = struct.unpack_from('<Q', shdr, 24)[0]
            sh_offset = struct.unpack_from('<Q', shdr, 32)[0]
            sh_size = struct.unpack_from('<Q', shdr, 40)[0]
            sections.append({
                'name_off': sh_name_off,
                'type': sh_type,
                'addr': sh_addr,
                'offset': sh_offset,
                'size': sh_size,
            })
        
        # Read section name string table
        strtab = sections[e_shstrndx]
        f.seek(strtab['offset'])
        str_data = f.read(strtab['size'])
        
        # Find .text section
        for sec in sections:
            name = str_data[sec['name_off']:].split(b'\x00')[0].decode('ascii', errors='replace')
            sec['name'] = name
            if name == '.text':
                f.seek(sec['offset'])
                text_data = f.read(sec['size'])
                return text_data, sec['addr']
        
        print("ERROR: No .text section found")
        return None

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 disasm_manual.py <elf_file>")
        sys.exit(1)
    
    result = extract_text_section(sys.argv[1])
    if result is None:
        sys.exit(1)
    
    text_data, text_addr = result
    print(f"\n=== Disassembly of .text section ({len(text_data)} bytes at 0x{text_addr:08x}) ===")
    
    instructions = disassemble_bytes(text_data, text_addr)
    for line in instructions:
        print(line)
    
    print(f"=== End of disassembly ===")

if __name__ == '__main__':
    main()
