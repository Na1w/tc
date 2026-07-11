#include <stdio.h>
#include <stdint.h>

// Current buggy version
uint8_t rex_w_buggy(uint8_t reg_bit, uint8_t rm_bit) {
    return (uint8_t)(0x48 | (reg_bit << 3) | (rm_bit << 1));
}

// Fixed version
uint8_t rex_w_fixed(uint8_t reg_bit, uint8_t rm_bit) {
    return (uint8_t)(0x48 | (reg_bit << 2) | rm_bit);
}

int main() {
    printf("REX prefix analysis:\n");
    printf("Format: 0100 W R X B\n");
    printf("  W=bit3(64-bit), R=bit2(reg ext), X=bit1(SIB idx), B=bit0(r/m ext)\n\n");

    printf("Bug analysis:\n");
    printf("  Buggy: 0x48 | (reg_bit << 3) | (rm_bit << 1)\n");
    printf("  Fixed: 0x48 | (reg_bit << 2) | rm_bit\n\n");

    printf("  When reg_bit=1 (register >= 8):\n");
    printf("    Buggy sets bit 3 (W bit) instead of bit 2 (R bit)\n");
    printf("    This means the REX prefix doesn't extend the reg field!\n");
    printf("  When rm_bit=1 (r/m register >= 8):\n");
    printf("    Buggy sets bit 1 (X bit) instead of bit 0 (B bit)\n");
    printf("    This means the REX prefix doesn't extend the r/m field!\n\n");

    printf("Example: mov r8, r9 (dst=8, src=9)\n");
    printf("  dst & 7 = 0, src & 7 = 1\n");
    printf("  Buggy REX: rex_w(sb=1, db=1) = 0x%02X\n", rex_w_buggy(1, 1));
    printf("  Correct REX: should be 0x%02X\n", rex_w_fixed(1, 1));

    return 0;
}
