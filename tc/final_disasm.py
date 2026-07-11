#!/usr/bin/env python3
"""Final clean disassembler."""
import struct

R64 = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi',
       'r8','r9','r10','r11','r12','r13','r14','r15']
R32 = ['eax','ecx','edx','ebx','esp','ebp','esi','edi',
       'r8d','r9d','r10d','r11d','r12d','r13d','r14d','r15d']
R8  = ['al','cl','dl','bl','spl','bpl','sil','dil']

def R(n, w64=False):
    n = n & 0xF
    return R64[n] if w64 else R32[n]

def disasm(data, base=0x401000):
    ip = 0
    lines = []
    while ip < len(data):
        start = ip
        addr = base + ip
        rex = 0; rex_w = False
        if 0x40 <= data[ip] <= 0x4F and (data[ip] & 0x38):
            rex = data[ip]; rex_w = bool(rex & 8); ip += 1
        if ip >= len(data):
            lines.append((addr,'','<EOF>','','')); break

        op = data[ip]; mn=''; ops=''; bad=False

        def get_mr():
            nonlocal ip
            mr = data[ip]; ip += 1
            mod=(mr>>6)&3; reg=((mr>>3)&7)|((rex&2)>>1)*8; rm=(mr&7)|((rex&4)>>2)*8
            return mod, reg&0xF, rm&0xF

        def get_d(mod, rm):
            nonlocal ip
            if mod==0 and rm==5:
                if ip+4>len(data): return 0xBADBAD
                d=struct.unpack_from('<i',data,ip)[0]; ip+=4; return d
            if mod==1:
                if ip+1>len(data): return 0xBADBAD
                d=struct.unpack_from('<b',data,ip)[0]; ip+=1; return d
            if mod==2:
                if ip+4>len(data): return 0xBADBAD
                d=struct.unpack_from('<i',data,ip)[0]; ip+=4; return d
            return 0

        def as_(mod, rm, d):
            if mod==3: return R(rm,rex_w)
            if mod==0 and rm==5: return f'[{d:+d}]'
            return f'[{R64[rm]}+{d:+d}]'

        if 0x50<=op<=0x5F:
            rr=(op&7)|((rex&4)>>2)*8; rr&=0xF
            mn=('push' if op<0x58 else 'pop')+' '+R64[rr]; ip+=1
        elif op==0xc3: mn='ret'; ip+=1
        elif op==0xc9: mn='leave'; ip+=1
        elif op==0x90: mn='nop'; ip+=1
        elif op==0xe8:
            if ip+5>len(data): mn='call?'; bad=True; ip+=1
            else:
                rel=struct.unpack_from('<i',data,ip+1)[0]; ip+=5
                mn='call'; ops=f'{hex(addr+5+rel)} (rel {rel:+d})'
        elif op==0xe9:
            if ip+5>len(data): mn='jmp?'; bad=True; ip+=1
            else:
                rel=struct.unpack_from('<i',data,ip+1)[0]; ip+=5
                mn='jmp'; ops=f'{hex(addr+5+rel)} (rel {rel:+d})'
        elif op==0x83:
            if ip+2>len(data): mn='0x83?'; bad=True; ip+=1
            else:
                mod,reg,rm=get_mr(); imm8=struct.unpack_from('<b',data,ip)[0]; ip+=1
                om={0:'add',1:'or',2:'adc',3:'sbc',4:'and',5:'sub',6:'xor',7:'cmp'}
                mn=om.get(reg,'op?'); d=get_d(mod,rm)
                ops=f'{R(rm,rex_w)}, {imm8}' if mod==3 else f'{as_(mod,rm,d)}, {imm8}'
        elif op==0x81:
            if ip+5>len(data): mn='0x81?'; bad=True; ip+=1
            else:
                mod,reg,rm=get_mr(); imm32=struct.unpack_from('<i',data,ip)[0]; ip+=4
                om={0:'add',1:'or',2:'adc',3:'sbc',4:'and',5:'sub',6:'xor',7:'cmp'}
                mn=om.get(reg,'op?'); d=get_d(mod,rm)
                ops=f'{R(rm,rex_w)}, {imm32}' if mod==3 else f'{as_(mod,rm,d)}, {imm32}'
        elif op==0x85:
            if ip+1>len(data): mn='0x85?'; bad=True; ip+=1
            else:
                mod,reg,rm=get_mr(); mn='test'; d=get_d(mod,rm)
                ops=f'{R(rm,rex_w)}, {R(reg,rex_w)}' if mod==3 else f'{as_(mod,rm,d)}, {R(reg,rex_w)}'
        elif op==0x89:
            if ip+1>len(data): mn='0x89?'; bad=True; ip+=1
            else:
                mod,reg,rm=get_mr(); mn='mov'; d=get_d(mod,rm)
                ops=f'{R(reg,rex_w)}, {R(rm,rex_w)}' if mod==3 else f'{R(reg,rex_w)}, {as_(mod,rm,d)}'
        elif op==0x8b:
            if ip+1>len(data): mn='0x8b?'; bad=True; ip+=1
            else:
                mod,reg,rm=get_mr(); mn='mov'; d=get_d(mod,rm)
                ops=f'{R(reg,rex_w)}, {R(rm,rex_w)}' if mod==3 else f'{R(reg,rex_w)}, {as_(mod,rm,d)}'
        elif op==0x8d:
            if ip+1>len(data): mn='0x8d?'; bad=True; ip+=1
            else:
                mod,reg,rm=get_mr(); mn='lea'; d=get_d(mod,rm)
                ops=f'{R(reg,rex_w)}, {as_(mod,rm,d)}'
        elif op==0xc7:
            if ip+5>len(data): mn='0xc7?'; bad=True; ip+=1
            else:
                mod,reg,rm=get_mr(); imm32=struct.unpack_from('<I',data,ip)[0]; ip+=4
                mn='mov'; d=get_d(mod,rm)
                ops=f'{R(rm,rex_w)}, {imm32}' if mod==3 else f'{as_(mod,rm,d)}, {imm32}'
        elif op==0x63:
            if ip+1>len(data): mn='0x63?'; bad=True; ip+=1
            else:
                mod,reg,rm=get_mr(); mn='movsxd'; d=get_d(mod,rm)
                ops=f'{R32[reg]}, {as_(mod,rm,d)}'
        elif op==0x66:
            ip+=1
            if ip<len(data) and data[ip]==0xaf:
                ip+=1; mod,reg,rm=get_mr(); imm8=data[ip]; ip+=1
                mn='imul'; ops=f'{R(rm,rex_w)}, {R(reg,rex_w)}, {imm8}'
            else:
                mn='66h_prefix'; bad=True
        elif op==0x0f:
            ip+=1; op2=data[ip]; ip+=1
            if 0x80<=op2<=0x8f:
                rel=struct.unpack_from('<i',data,ip)[0]; ip+=4
                jn={0x80:'jo',0x81:'jno',0x82:'jc',0x83:'jnc',0x84:'je',0x85:'jne',
                    0x86:'jl',0x87:'jge',0x88:'jle',0x89:'jg',0x8a:'jp',0x8b:'jnp',
                    0x8c:'js',0x8d:'jns',0x8e:'jpe',0x8f:'jpo'}
                mn=jn.get(op2,'j??'); ops=f'{hex(addr+ip-start+rel)} (rel {rel:+d})'
            elif 0x90<=op2<=0x9f:
                mod,reg,rm=get_mr()
                sn={0x90:'seto',0x91:'setno',0x92:'setc',0x93:'setnc',0x94:'sete',0x95:'setne',
                    0x96:'setl',0x97:'setge',0x98:'setle',0x99:'setg',0x9a:'setp',0x9b:'setnp',
                    0x9c:'sets',0x9d:'setns',0x9e:'setpe',0x9f:'setpo'}
                mn=sn.get(op2,'set??')
                ops=R8[rm] if mod==3 else f'byte [{R64[rm]}+{get_d(mod,rm):+d}]'
            elif op2==0xb6:
                mod,reg,rm=get_mr(); mn='movzx'; d=get_d(mod,rm)
                ops=f'{R(reg,rex_w)}, byte {as_(mod,rm,d)}'
            elif op2==0xb7:
                mod,reg,rm=get_mr(); mn='movzx'; d=get_d(mod,rm)
                ops=f'{R(reg,rex_w)}, word {as_(mod,rm,d)}'
            elif op2==0xaf:
                mod,reg,rm=get_mr(); mn='imul'; d=get_d(mod,rm)
                ops=f'{R(rm,rex_w)}, {R(reg,rex_w)}' if mod==3 else f'{as_(mod,rm,d)}, {R(reg,rex_w)}'
            else:
                mn=f'0F {op2:02X}'; bad=True
        else:
            mn=f'DB 0x{op:02X}'; bad=True; ip+=1

        hb=' '.join(f'{b:02X}' for b in data[start:ip])
        os_=f' {ops}' if ops else ''; bs=' ***BAD' if bad else ''
        lines.append((addr,hb,mn,os_,bs))
    return lines

def read_text(path):
    with open(path,'rb') as f: data=f.read()
    e_shoff=struct.unpack_from('<Q',data,40)[0]
    e_shentsize=struct.unpack_from('<H',data,58)[0]
    sh_off=e_shoff+e_shentsize*1
    off=struct.unpack_from('<Q',data,sh_off+24)[0]
    sz=struct.unpack_from('<Q',data,sh_off+32)[0]
    va=struct.unpack_from('<Q',data,sh_off+16)[0]
    return data[off:off+sz], va

cb,cba=read_text('/tmp/cb_out')
fc,fca=read_text('/tmp/fc_out')

for data,base,label in [(cb,cba,'compare_branch.c'),(fc,fca,'function_call.c')]:
    print()
    print('='*90)
    print(f'  DISASSEMBLY: {label}  ({len(data)} bytes at 0x{base:08X})')
    print('='*90)
    lines=disasm(data,base)
    for addr,hb,mn,ops,bad in lines:
        print(f'  {hex(addr):>10s}: {hb:<42s} {mn}{ops}{bad}')
    print(f'\n  --- JUMP/CALL ANALYSIS ---')
    for addr,hb,mn,ops,bad in lines:
        if mn in ('call','jmp','je','jne','jl','jg','jle','jge','js','jns','jo','jno','jc','jnc','jp','jnp','jpe','jpo'):
            target=ops.split()[0] if ops else '?'
            rel=ops.split('rel ')[1].rstrip(')') if 'rel ' in ops else '?'
            try:
                t=int(target,16)
                in_range=base<=t<base+len(data)
                status='OK (in .text)' if in_range else '!! OUTSIDE .text !!'
            except: status='??'
            print(f'    0x{addr:08X}: {mn:8s} -> {target} (rel {rel}) [{status}]')
