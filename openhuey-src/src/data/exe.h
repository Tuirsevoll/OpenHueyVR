/* The game's executable (SLUS_210.75; the European disc's SLES_528.77): some of the game's
 * tables live only there (the door table, the room table). They are read from the player's copy
 * at start-up, by address. */
#ifndef EXE_H
#define EXE_H

#include <stddef.h>
#include <stdint.h>

/* the two discs' executables, by size: the USA disc's, and the European disc's (a little larger).
 * platform/library.c checks both by size and CRC when it imports; exe_take() tells them apart by
 * their size for a disc image or folder read as it is. */
#define EXE_SIZE_USA 3650608L
#define EXE_SIZE_EUROPE 3660080L

typedef struct Exe {
    uint8_t *data;
    size_t size;
    int european;   /* SLES_528.77: its tables at their own addresses (exetables.c) - exe_take's, by size */
} Exe;

/* take the file's bytes (malloc'd: freed with it, or now if it isn't an ELF); 0 if it isn't */
int exe_take(Exe *e, uint8_t *data, size_t size);
void exe_free(Exe *e);
/* n bytes at a PS2 address, or NULL if it isn't in the file */
const uint8_t *exe_at(const Exe *e, uint32_t vaddr, size_t n);

#endif
