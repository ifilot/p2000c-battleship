/* SPDX-License-Identifier: GPL-3.0-only */
/* fleet.h -- the two fleets on their 10x10 seas, placement rules and shots.
 *
 * Cells are numbered row * 10 + column (A1 = 0, J1 = 9, A10 = 90). Ships are
 * straight lines of cells and may not touch each other, not even at a
 * corner. A ship is anchored at its top-left cell. */
#ifndef FLEET_H
#define FLEET_H

#define SIZE     10
#define CELLS    100
#define NSHIPS   5
#define NO_SHIP  0xFF

#define HUMAN    0
#define COMPUTER 1

/* grid[] values: ship number + 1 (0 = water), plus SHOT once fired upon */
#define SHOT      0x80
#define SHIP_MASK 0x07

/* shot results */
#define RES_MISS  0
#define RES_HIT   1
#define RES_SUNK  2

struct ship {
    unsigned char cell;                     /* top-left cell, or NO_SHIP when not placed */
    unsigned char vertical;
    unsigned char hits;
};

struct fleet {
    struct ship ship[NSHIPS];
    unsigned char grid[CELLS];
    unsigned char afloat;                   /* ships placed and not yet sunk */
    unsigned char shots;                    /* shots fired at this fleet */
    unsigned char hits;                     /* ... of which hit a ship */
};

extern struct fleet fleets[2];              /* HUMAN, COMPUTER */
extern const unsigned char ship_len[NSHIPS];
extern const char *const ship_name[NSHIPS];

/* Row and column of a cell by table: a division by 10 is a slow library
 * call on the Z80. */
extern const unsigned char cell_row[CELLS], cell_col[CELLS];
#define COL(cell) (cell_col[cell])
#define ROW(cell) (cell_row[cell])

extern void fleet_clear(struct fleet *f);

/* Could ship s lie at cell (top-left) with this orientation? Checks the
 * edges, overlap and the no-touching rule against the ships already placed
 * (ship s itself must not be placed). */
extern unsigned char fleet_fits(const struct fleet *f, unsigned char s, unsigned char cell, unsigned char vertical);
extern void fleet_place(struct fleet *f, unsigned char s, unsigned char cell, unsigned char vertical);
extern void fleet_remove(struct fleet *f, unsigned char s);

/* Places the whole fleet at random (replacing any placed ships). */
extern void fleet_random(struct fleet *f);

/* Segment of ship s at cell (0 = top-left end). */
extern unsigned char ship_segment(const struct fleet *f, unsigned char s, unsigned char cell);

/* Fires at a cell not shot before; returns RES_MISS, RES_HIT or RES_SUNK and
 * sets *ship to the ship hit (NO_SHIP for a miss). */
extern unsigned char fleet_shoot(struct fleet *f, unsigned char cell, unsigned char *ship);

/* Random numbers: xorshift, stirred with the refresh register at key presses. */
extern void rng_stir(void);
extern unsigned char random_below(unsigned char n);     /* 0 .. n-1, n >= 1 */

#endif
