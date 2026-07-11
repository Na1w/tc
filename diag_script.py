#!/usr/bin/env python3
"""
Phase 1 Diagnostic Script - Disassemble and analyze failing test binaries.
Uses Capstone for x86-64 disassembly.
"""

import struct
import subprocess
import sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

def parse_elf_minimal(path):
    """Parse minimal ELF64 header to find entry point and code sections."""
    with open(path, 'rb') as f:
        data = f.read()
    
    # Check ELF magic
    if data[:4] != b'\x7fELF':
        return None, None, None, data
    
    # Parse header
    e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHI III I HHHHHH', data, 16)
    
    # Parse program headers
    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from('<IIQQQQQQ', data, off)
        segments.append({
            'type': p_type, 'flags': p_flags, 'offset': p_offset,
            'vaddr': p_vaddr, 'filesz': p_filesz, 'memsz': p_memsz
        })
    
    # Parse section headers
    sections = []
    if e_shoff > 0 and e_shnum > 0:
        # First read section header string table
        shstr_off = 0
        if e_shstrndx < e_shnum:
            shstr_off = struct.unpack_from('<Q', data, e_shoff + e_shstrndx * e_shentsize + 24)[0]
        shstr = data[shstr_off:]
        
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = struct.unpack_from('<IIQQQQIIQQ', data, off)
            name = shstr[sh_name:].split(b'\x00')[0].decode('ascii', errors='replace')
            sections.append({
                'name': name, 'type': sh_type, 'flags': sh_flags,
                'addr': sh_addr, 'offset': sh_offset, 'size': sh_size,
                'align': sh_addralign
            })
    
    return e_entry, segments, sections, data

def disassemble_bytes(md, code_bytes, base_addr):
    """Disassemble bytes starting at base_addr."""
    lines = []
    for insn in md.disasm(code_bytes, base_addr):
        lines.append(f"  {insn.address:016x}: {insn.mnemonic:<8s} {insn.op_str}")
    return lines

def find_code_region(data, segments, sections):
    """Find the .text section or code region."""
    # Try sections first
    for sec in sections:
        if '.text' in sec['name']:
            return sec['offset'], sec['addr'], sec['size']
    
    # Fall back: find non-zero data after ELF header
    # For minimal ELF without sections, code is typically at file offset 0x1000
    # Search for recognizable instruction patterns
    for offset in range(0x100, len(data) - 16, 16):
        # Look for push rbp (55 48 89 e5) or similar
        b = data[offset:offset+16]
        if b[0:1] == b'\x55' and b[1:4] == b'\x48\x89\xe5':
            return offset, 0x400000 + offset, len(data) - offset
    
    # Last resort: return entry offset
    entry_offset = 0x1000  # typical
    if entry_offset < len(data):
        return entry_offset, 0x401000, len(data) - entry_offset
    
    return 0, 0, len(data)

def run_binary_and_capture(path):
    """Run binary and capture crash info."""
    try:
        result = subprocess.run(
            [path], capture_output=True, text=True, timeout=5
        )
        return {
            'exit_code': result.returncode,
            'stdout': result.stdout,
            'stderr': result.stderr,
            'signal': None,
            'crash': False
        }
    except subprocess.TimeoutExpired:
        return {
            'exit_code': -1,
            'stdout': '',
            'stderr': 'TIMEOUT',
            'signal': 'SIGXCPU (timeout)',
            'crash': True
        }
    except Exception as e:
        return {
            'exit_code': -1,
            'stdout': '',
            'stderr': str(e),
            'signal': str(e),
            'crash': True
        }

def analyze_instructions(lines, base_addr):
    """Analyze disassembled instructions for common bugs."""
    issues = []
    
    for line in lines:
        addr_str = line.split(':')[0].strip()
        addr = int(addr_str, 16)
        rest = line.split(':', 1)[1].strip()
        mnemonic = rest.split()[0] if rest.split() else ''
        operands = rest[len(mnemonic):].strip() if len(rest) > len(mnemonic) else ''
        
        # Check for RSP/RBP usage outside prologue/epilogue
        if mnemonic not in ('push', 'pop', 'mov', 'sub', 'add', 'and', 'lea'):
            if 'rsp' in operands.lower() or 'rbp' in operands.lower():
                # Check if it's in a prologue/epilogue pattern
                if not (addr % 2 == 0 and mnemonic in ('push', 'pop', 'mov', 'sub', 'add')):
                    pass  # Not necessarily wrong
        
        # Check RSP as source operand (very suspicious)
        if 'rsp' in operands.lower():
            if mnemonic not in ('push', 'pop', 'sub', 'add', 'and', 'lea', 'mov'):
                issues.append(f"  WARN: RSP used as operand in '{mnemonic} {operands}' at 0x{addr:x}")
            elif mnemonic == 'mov':
                # mov reg, [rsp+offset] or mov [rsp+offset], reg could be valid
                pass
        
        # Check RBP as source/destination outside frame setup
        if 'rbp' in operands.lower():
            if mnemonic not in ('push', 'pop', 'mov', 'sub', 'add', 'lea'):
                issues.append(f"  WARN: RBP used as operand in '{mnemonic} {operands}' at 0x{addr:x}")
        
        # Check for suspicious call targets
        if mnemonic == 'call':
            # Check if target is a reasonable address
            try:
                target = int(operands.split()[0], 16) if operands else 0
                if target < 0x400000 or target > 0x500000:
                    issues.append(f"  WARN: Call to suspicious address 0x{target:x} at 0x{addr:x}")
            except (ValueError, IndexError):
                pass  # Relative call
        
        # Check for suspicious jump targets
        if mnemonic in ('jmp', 'je', 'jne', 'jg', 'jl', 'jge', 'jle', 'ja', 'jb', 'jz', 'jnz'):
            try:
                target = int(operands.split()[0], 16) if operands else 0
                if target < 0x400000 or target > 0x500000:
                    issues.append(f"  WARN: Jump to suspicious address 0x{target:x} at 0x{addr:x}")
            except (ValueError, IndexError):
                pass
    
    return issues

def main():
    binaries = [
        ('compare_branch', '/home/coder/workspace/compare_branch_bin', 'int main() { int x = 5; if (x > 3) { x = 10; } return x; }'),
        ('function_call', '/home/coder/workspace/function_call_bin', 'int double(int x) { return x * 2; } int main() { return double(7); }'),
        ('loop_while', '/home/coder/workspace/loop_while_bin', 'int main() { int i = 0; int s = 0; while (i < 10) { s += i; i++; } return s; }'),
    ]
    
    expected_results = {
        'compare_branch': '10',
        'function_call': '14',
        'loop_while': '45',
    }
    
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    
    report = []
    report.append("=" * 80)
    report.append("PHASE 1 DIAGNOSTIC REPORT")
    report.append("Binary Analysis: compare_branch, function_call, loop_while")
    report.append("=" * 80)
    
    for name, path, src in binaries:
        report.append(f"\n{'='*80}")
        report.append(f"TEST: {name}")
        report.append(f"Source: {src}")
        report.append(f"Expected exit code: {expected_results[name]}")
        report.append(f"{'='*80}")
        
        # Parse ELF
        entry, segments, sections, data = parse_elf_minimal(path)
        
        report.append(f"\n--- ELF HEADER ---")
        report.append(f"  Entry point: 0x{entry:x}")
        report.append(f"  Segments: {len(segments)}")
        report.append(f"  Sections: {len(sections)}")
        
        for seg in segments:
            report.append(f"  Segment: type={seg['type']}, offset=0x{seg['offset']:x}, vaddr=0x{seg['vaddr']:x}, filesz={seg['filesz']}")
        
        for sec in sections:
            report.append(f"  Section: {sec['name']}, type={sec['type']}, offset=0x{sec['offset']:x}, addr=0x{sec['addr']:x}, size={sec['size']}")
        
        # Find code region
        code_off, code_vaddr, code_size = find_code_region(data, segments, sections)
        code_bytes = data[code_off:code_off + code_size]
        
        report.append(f"\n--- DISASSEMBLY (base=0x{code_vaddr:x}, offset=0x{code_off:x}, size={code_size}) ---")
        
        # Disassemble
        lines = disassemble_bytes(md, code_bytes, code_vaddr)
        
        # Find function boundaries
        func_boundaries = []
        for i, line in enumerate(lines):
            if 'push' in line and 'rbp' in line:
                func_boundaries.append((i, line.split(':')[0].strip()))
        
        report.append(f"\n  Function prologues found at: {func_boundaries}")
        
        # Print full disassembly with annotations
        report.append(f"\n  Full disassembly:")
        for line in lines:
            report.append(f"    {line}")
        
        # Analyze for issues
        report.append(f"\n--- STATIC ANALYSIS ---")
        issues = analyze_instructions(lines, code_vaddr)
        
        # Detailed instruction analysis
        report.append(f"\n  Detailed operand analysis:")
        for i, line in enumerate(lines):
            addr_str = line.split(':')[0].strip()
            addr = int(addr_str, 16)
            rest = line.split(':', 1)[1].strip()
            parts = rest.split(None, 1)
            if not parts:
                continue
            mnemonic = parts[0]
            operands = parts[1] if len(parts) > 1 else ''
            
            # Check RSP usage
            if 'rsp' in operands.lower() and mnemonic not in ('push', 'pop', 'sub', 'add', 'and', 'lea', 'mov'):
                report.append(f"    [RSP MISUSE] 0x{addr:x}: {mnemonic} {operands}")
            
            # Check RBP usage outside prologue/epilogue
            if 'rbp' in operands.lower() and mnemonic not in ('push', 'pop', 'mov', 'sub', 'add', 'lea'):
                report.append(f"    [RBP MISUSE] 0x{addr:x}: {mnemonic} {operands}")
            
            # Check stack alignment before calls
            if mnemonic == 'call':
                # Look back at stack operations
                stack_ops = []
                for j in range(max(0, i-10), i):
                    prev_line = lines[j]
                    prev_parts = prev_line.split(':', 1)[1].strip().split(None, 1)
                    if len(prev_parts) >= 2:
                        pm, po = prev_parts
                        if 'rsp' in po.lower():
                            stack_ops.append(f"{pm} {po}")
                if stack_ops:
                    report.append(f"    [STACK BEFORE CALL] 0x{addr:x}: call {operands}, recent stack ops: {stack_ops}")
            
            # Check for invalid memory operands
            if '0x0' in operands or ', 0x0)' in operands:
                report.append(f"    [NULL PTR] 0x{addr:x}: {mnemonic} {operands}")
        
        if issues:
            for issue in issues:
                report.append(f"  {issue}")
        else:
            report.append(f"  No obvious static issues detected in disassembly.")
        
        # Run binary
        report.append(f"\n--- RUNTIME TEST ---")
        result = run_binary_and_capture(path)
        report.append(f"  Exit code: {result['exit_code']}")
        report.append(f"  Expected: {expected_results[name]}")
        report.append(f"  Match: {'YES' if str(result['exit_code']) == expected_results[name] else 'NO - MISMATCH'}")
        if result['stdout']:
            report.append(f"  stdout: {result['stdout'][:200]}")
        if result['stderr']:
            report.append(f"  stderr: {result['stderr'][:200]}")
        if result['signal']:
            report.append(f"  Signal: {result['signal']}")
        
        # Try strace if available
        try:
            strace_result = subprocess.run(
                ['strace', '-e', 'trace=segfault,bus,ill,fpe', path],
                capture_output=True, text=True, timeout=5
            )
            report.append(f"  strace: {strace_result.stderr[-500:] if strace_result.stderr else 'no output'}")
        except FileNotFoundError:
            pass
        except Exception as e:
            report.append(f"  strace error: {e}")
    
    # Summary
    report.append(f"\n{'='*80}")
    report.append("SUMMARY")
    report.append(f"{'='*80}")
    report.append(f"")
    
    return '\n'.join(report)

if __name__ == '__main__':
    report = main()
    print(report)
    
    # Save to file
    with open('/home/coder/workspace/diag_phase1.txt', 'w') as f:
        f.write(report)
    print(f"\nReport saved to /home/coder/workspace/diag_phase1.txt")
