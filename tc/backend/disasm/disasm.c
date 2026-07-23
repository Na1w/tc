/*
 * disasm.c -- Capstone disassembler wrapper for the tc compiler project.
 *
 * When compiled with HAVE_CAPSTONE, this uses libcapstone to produce
 * human-readable x86-64 disassembly.  Without it, it falls back to a
 * simple hex dump so the compiler still builds cleanly.
 */

#include "disasm.h"

#ifdef HAVE_CAPSTONE
#include <capstone/capstone.h>
#endif

/* ==================================================================
 * Capstone path
 * ================================================================== */

#ifdef HAVE_CAPSTONE

void disasm_x86_64(const uint8_t *code, size_t len, uint64_t addr, FILE *out) {
    /* Guard against degenerate input. */
    if (!code || len == 0 || !out) {
        return;
    }

    csh handle;
    cs_insn *insn;

    if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK) {
        fprintf(out, "tc: disasm: failed to initialize Capstone\n");
        return;
    }

    /* Request detailed instruction details if available. */
    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

    size_t count = cs_disasm(handle, code, len, addr, 0, &insn);
    if (count > 0) {
        for (size_t i = 0; i < count; i++) {
            fprintf(out, "  0x%08" PRIx64 ":  %s  %s\n",
                    insn[i].address,
                    insn[i].mnemonic,
                    insn[i].op_str);
        }
        cs_free(insn, count);
    } else {
        /* cs_disasm returned 0 -- could mean the bytes are not valid
         * instructions or the buffer is too short.  Fall through to a
         * hex dump as a safety net. */
        fprintf(out, "  (no instructions decoded from %zu bytes)\n", len);
    }

    cs_close(&handle);
}

#else /* !HAVE_CAPSTONE */

/* ==================================================================
 * Fallback: plain hex dump
 * ================================================================== */

void disasm_x86_64(const uint8_t *code, size_t len, uint64_t addr, FILE *out) {
    if (!code || len == 0 || !out) {
        return;
    }

    fprintf(out, "  ; Capstone not available -- hex dump of %zu bytes at 0x%08" PRIx64 "\n",
            len, addr);

    for (size_t i = 0; i < len; i++) {
        if (i % 16 == 0) {
            fprintf(out, "  0x%08" PRIx64 ": ", (uint64_t)(addr + i));
        }
        fprintf(out, "%02x ", code[i]);
        if (i % 16 == 15) {
            fprintf(out, "\n");
        }
    }
    if (len % 16 != 0) {
        fprintf(out, "\n");
    }
}

#endif /* HAVE_CAPSTONE */
