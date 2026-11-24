/* The game's two executables, told apart by their size (src/data/exe.c): the USA disc's
 * SLUS_210.75 and the European disc's SLES_528.77, whose tables are at their own addresses
 * (exetables.c's vaddr_eu). An imported library says which disc it came from (and was checked by
 * size and CRC); a disc image or folder read as it is has no such check, so the size is what
 * tells the engine which addresses its tables are at - read as the USA's, a European one gives
 * the rooms' and doors' tables from the wrong places.
 *
 *   exe-check */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/exe.h"

static int fails;

#define CHECK(what, ok)                                                          \
    do {                                                                         \
        int _ok = (ok);                                                          \
        printf("exe: %s -> %d (%s)\n", what, _ok, _ok ? "pass" : "FAIL");        \
        if (!_ok) {                                                              \
            fails++;                                                             \
        }                                                                        \
    } while (0)

/* a minimal ELF of `size` bytes with one PT_LOAD covering the rest of it (what exe_take and exe_at
 * read; a real executable's own header and program header, less their contents) */
static uint8_t *elf(size_t size) {
    uint8_t *p = calloc(1, size);
    uint32_t phoff = 0x34, ph[6];
    uint16_t phnum = 1;

    if (p == NULL) {
        return NULL;
    }
    memcpy(p, "\x7f" "ELF", 4);
    memcpy(p + 0x1C, &phoff, 4);
    memcpy(p + 0x2C, &phnum, 2);
    ph[0] = 1;                              /* PT_LOAD */
    ph[1] = 0x100;                          /* its offset in the file */
    ph[2] = 0x100000;                       /* where it is loaded */
    ph[3] = 0;
    ph[4] = (uint32_t)(size - 0x100);       /* its size in the file */
    ph[5] = ph[4];
    memcpy(p + phoff, ph, sizeof(ph));
    return p;
}

int main(void) {
    Exe e;
    uint8_t *us = elf((size_t)EXE_SIZE_USA), *eu = elf((size_t)EXE_SIZE_EUROPE), *other = elf(0x1000);

    CHECK("an ELF the USA disc's size is taken", exe_take(&e, us, (size_t)EXE_SIZE_USA));
    CHECK("... and is not European (the USA tables' addresses)", e.european == 0);
    CHECK("... and its bytes are read at a loaded address", exe_at(&e, 0x100010, 4) == e.data + 0x110);
    exe_free(&e);

    CHECK("an ELF the European disc's size is taken", exe_take(&e, eu, (size_t)EXE_SIZE_EUROPE));
    CHECK("... and is European (its tables' own addresses)", e.european == 1);
    exe_free(&e);

    CHECK("an ELF of neither size is taken", exe_take(&e, other, 0x1000));
    CHECK("... and is the USA's (the addresses tried first)", e.european == 0);
    exe_free(&e);

    {
        uint8_t *not = calloc(1, 0x100);

        CHECK("something that is not an ELF is refused", !exe_take(&e, not, 0x100));
    }

    printf("%d failed\n", fails);
    return fails != 0;
}