#!/usr/bin/env python3
"""Clean x86-64 disassembler with bounds checking."""
import struct

R64 = ["rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
       "r8","r9","r10","r11","r12","r13","r14","r15"]
R32 = ["eax","ecx","edx","ebx","esp","ebp","esi","edi",
       "r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d"]
R8  = ["al","cl","dl","bl","spl","bpl","sil","dil"]

def r(n, w64=False):
    return R64[n] if w64 else R32[n]

def disasm(data, base=0x401000):
    ip = 0
    out = []
    while ip < len(data):
        start = ip
        addr = base + ip
        rex = 0; rex_w = False

        # REX prefix: 0100WRXB
        if 0x40 <= data[ip] <= 0x4F and (data[ip] & 0x38):
            rex = data[ip]
            rex_w = bool(rex & 0x08)
            ip += 1

        if ip >= len(data):
            out.append((addr, "<EOF>")); break

        op = data[ip]
        mn = ""; ops = ""; bad = False

        def get_modrm():
            nonlocal ip
            if ip >= len(data): return None, None, None
            mr = data[ip]; ip += 1
            mod = (mr >> 6) & 3
            reg = ((mr >> 3) & 7) | ((rex & 0x02) >> 1) * 8
            rm  = (mr & 7) | ((rex & 0x04) >> 2) * 8
            return mod, reg, rm

        def get_disp(mod, rm):
            nonlocal ip
            if mod == 0 and rm == 5:
                if ip+4 > len(data): return 0xBAD
                d = struct.unpack_from('<i', data, ip)[0]; ip += 4; return d
            if mod == 1:
                if ip+1 > len(data): return 0xBAD
                d = struct.unpack_from('<b', data, ip)[0]; ip += 1; return d
            if mod == 2:
                if ip+4 > len(data): return 0xBAD
                d = struct.unpack_from('<i', data, ip)[0]; ip += 4; return d
            return 0

        def get_sib():
            nonlocal ip
            if ip >= len(data): return 0, 0, 0
            sb = data[ip]; ip += 1
            b = (sb & 7) | ((rex & 0x01) << 3)
            idx = ((sb >> 3) & 7) | ((rex & 0x04) >> 2) * 8
            sc = (sb >> 6) & 3
            return b, idx, sc

        def addr_str(mod, rm, disp):
            if mod == 3: return r(rm, rex_w)
            if mod == 0 and rm == 5: return f"[{disp:+d}]"
            return f"[{R64[rm]} + {disp:+d}]"

        # ---- Decode ----
        if op == 0x90:
            mn = "nop"; ip += 1
        elif op == 0xc3:
            mn = "ret"; ip += 1
        elif op == 0xc9:
            mn = "leave"; ip += 1
        elif 0x50 <= op <= 0x5F:
            rr = (op & 7) | ((rex & 0x04) >> 2) * 8
            mn = f"{'push' if op < 0x58 else 'pop'} {R64[rr]}"; ip += 1
        elif op == 0xe8:
            if ip+5 > len(data): mn="call?"; bad=True; ip+=1
            else:
                rel = struct.unpack_from('<i', data, ip+1)[0]; ip += 5
                mn = "call"; ops = f"{hex(addr+5+rel)} (rel {rel:+d})"
        elif op == 0xe9:
            if ip+5 > len(data): mn="jmp?"; bad=True; ip+=1
            else:
                rel = struct.unpack_from('<i', data, ip+1)[0]; ip += 5
                mn = "jmp"; ops = f"{hex(addr+5+rel)} (rel {rel:+d})"
        elif op == 0x0f:
            ip += 1
            if ip >= len(data): mn = "0F?"; bad = True
            else:
                op2 = data[ip]; ip += 1
                if 0x80 <= op2 <= 0x8f:
                    if ip+4 > len(data): mn=f"j??"; bad=True
                    else:
                        rel = struct.unpack_from('<i', data, ip)[0]; ip += 4
                        jn = {0x80:"jo",0x81:"jno",0x82:"jc",0x83:"jnc",0x84:"je",0x85:"jne",
                              0x86:"jl",0x87:"jge",0x88:"jle",0x89:"jg",0x8a:"jp",0x8b:"jnp",
                              0x8c:"js",0x8d:"jns",0x8e:"jpe",0x8f:"jpo"}
                        mn = jn.get(op2, f"j??{op2:02x}")
                        ops = f"{hex(addr+ip-start+rel)} (rel {rel:+d})"
                elif 0x90 <= op2 <= 0x9f:
                    mod, reg, rm = get_modrm()
                    sn = {0x90:"seto",0x91:"setno",0x92:"setc",0x93:"setnc",0x94:"sete",0x95:"setne",
                          0x96:"setl",0x97:"setge",0x98:"setle",0x99:"setg",0x9a:"setp",0x9b:"setnp",
                          0x9c:"sets",0x9d:"setns",0x9e:"setpe",0x9f:"setpo"}
                    mn = sn.get(op2, f"set??{op2:02x}")
                    if mod == 3:
                        ops = R8[rm]
                    else:
                        disp = get_disp(mod, rm)
                        ops = f"byte [{R64[rm]} + {disp:+d}]"
                elif op2 == 0xb6:
                    mod, reg, rm = get_modrm()
                    mn = "movzx"
                    disp = get_disp(mod, rm)
                    ops = f"{r(reg,rex_w)}, byte {addr_str(mod,rm,disp)}"
                elif op2 == 0xb7:
                    mod, reg, rm = get_modrm()
                    mn = "movzx"
                    disp = get_disp(mod, rm)
                    ops = f"{r(reg,rex_w)}, word {addr_str(mod,rm,disp)}"
                elif op2 == 0xaf:
                    mod, reg, rm = get_modrm()
                    mn = "imul"
                    disp = get_disp(mod, rm)
                    ops = f"{r(rm,rex_w)}, {r(reg,rex_w)}" if mod==3 else f"{addr_str(mod,rm,disp)}, {r(reg,rex_w)}"
                else:
                    mn = f"0F {op2:02X}"; bad = True
        elif op == 0x83:
            mod, reg, rm = get_modrm()
            imm8 = struct.unpack_from('<b', data, ip)[0]; ip += 1
            om = {0:"add",1:"or",2:"adc",3:"sbc",4:"and",5:"sub",6:"xor",7:"cmp"}
            mn = om.get(reg, f"op{reg}")
            disp = get_disp(mod, rm)
            ops = f"{r(rm,rex_w)}, {imm8}" if mod==3 else f"{addr_str(mod,rm,disp)}, {imm8}"
        elif op == 0x81:
            mod, reg, rm = get_modrm()
            imm32 = struct.unpack_from('<i', data, ip)[0]; ip += 4
            om = {0:"add",1:"or",2:"adc",3:"sbc",4:"and",5:"sub",6:"xor",7:"cmp"}
            mn = om.get(reg, f"op{reg}")
            disp = get_disp(mod, rm)
            ops = f"{r(rm,rex_w)}, {imm32}" if mod==3 else f"{addr_str(mod,rm,disp)}, {imm32}"
        elif op == 0x85:
            mod, reg, rm = get_modrm()
            mn = "test"
            disp = get_disp(mod, rm)
            ops = f"{r(rm,rex_w)}, {r(reg,rex_w)}" if mod==3 else f"{addr_str(mod,rm,disp)}, {r(reg,rex_w)}"
        elif op == 0x89:
            mod, reg, rm = get_modrm()
            mn = "mov"
            if mod == 0 and rm == 4:
                b, idx, sc = get_sib()
                disp = get_disp(mod, b)
                ops = f"{r(reg,rex_w)}, [{R64[b]} + {disp:+d}]"
            else:
                disp = get_disp(mod, rm)
                ops = f"{r(reg,rex_w)}, {addr_str(mod,rm,disp)}"
        elif op == 0x8b:
            mod, reg, rm = get_modrm()
            mn = "mov"
            if mod == 0 and rm == 4:
                b, idx, sc = get_sib()
                disp = get_disp(mod, b)
                ops = f"{r(reg,rex_w)}, [{R64[b]} + {disp:+d}]"
            else:
                disp = get_disp(mod, rm)
                ops = f"{r(reg,rex_w)}, {addr_str(mod,rm,disp)}"
        elif op == 0x8d:
            mod, reg, rm = get_modrm()
            mn = "lea"
            if mod == 0 and rm == 4:
                b, idx, sc = get_sib()
                disp = get_disp(mod, b)
                ops = f"{r(reg,rex_w)}, [{R64[b]} + {disp:+d}]"
            else:
                disp = get_disp(mod, rm)
                ops = f"{r(reg,rex_w)}, {addr_str(mod,rm,disp)}"
        elif op == 0xc7:
            mod, reg, rm = get_modrm()
            imm32 = struct.unpack_from('<I', data, ip)[0]; ip += 4
            mn = "mov"
            disp = get_disp(mod, rm)
            ops = f"{r(rm,rex_w)}, {imm32}" if mod==3 else f"{addr_str(mod,rm,disp)}, {imm32}"
        elif op == 0x63:
            mod, reg, rm = get_modrm()
            mn = "movsxd"
            disp = get_disp(mod, rm)
            ops = f"{R32[reg]}, {addr_str(mod,rm,disp)}"
        elif op == 0x66:
            ip += 1
            if ip < len(data) and data[ip] == 0xaf:
                ip += 1
                mod, reg, rm = get_modrm()
                imm8 = data[ip]; ip += 1
                mn = "imul"
                ops = f"{r(rm,rex_w)}, {r(reg,rex_w)}, {imm8}"
            else:
                mn = "66h_prefix"; bad = True
        elif op == 0x64:
            mn = "FS_seg_override"; ip += 1
        elif op == 0x65:
            mn = "GS_seg_override"; ip += 1
        elif op == 0x67:
            mn = "addr_size_override"; ip += 1
        else:
            mn = f"DB 0x{op:02X}"; bad = True; ip += 1

        hex_b = " ".join(f"{b:02X}" for b in data[start:ip])
        op_s = f" {ops}" if ops else ""
        bad_s = " *** BAD" if bad else ""
        out.append((addr, hex_b, mn, op_s, bad_s))
    return out

def read_text(path):
    with open(path,'rb') as f: data=f.read()
    e_shoff = struct.unpack_from('<Q', data, 40)[0]
    e_shentsize = struct.unpack_from('<H', data, 58)[0]
    sh_off = e_shoff + e_shentsize * 1
    off = struct.unpack_from('<Q', data, sh_off+24)[0]
    sz = struct.unpack_from('<Q', data, sh_off+32)[0]
    va = struct.unpack_from('<Q', data, sh_off+16)[0]
    return data[off:off+sz], va

cb, cba = read_text('/tmp/cb_out')
fc, fca = read_text('/tmp/fc_out')

for data, base, label in [(cb, cba, "compare_branch.c"), (fc, fca, "function_call.c")]:
    print(f"\n{'='*90}")
    print(f"  DISASSEMBLY: {label}  ({len(data)} bytes at 0x{base:08X})")
    print(f"{'='*90}")
    lines = disasm(data, base)
    for addr, hex_b, mn, ops, bad in lines:
        print(f"  {hex(addr):>10s}: {hex_b:<42s} {mn}{ops}{bad}")

    # Jump analysis
    print(f"\n  --- JUMP/CALL ANALYSIS ---")
    for addr, hex_b, mn, ops, bad in lines:
        if mn in ("call","jmp","je","jne","jl","jg","jle","jge","js","jns","jo","jno","jc","jnc","jp","jnp","jpe","jpo"):
            target = ops.split()[0] if ops else "?"
            rel = ops.split("rel ")[1].rstrip(")") if "rel " in ops else "?"
            try:
                t = int(target, 16)
                in_range = base <= t < base + len(data)
                status = "OK (in .text)" if in_range else "!! OUTSIDE .text !!"
            except:
                status = "??"
            print(f"    0x{addr:08X}: {mn:8s} -> {target} (rel {rel}) [{status}]")
