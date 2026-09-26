/* SPDX-License-Identifier: GPL-3.0-only */
/* fleet.c -- fleets, placement rules, shots and random numbers. */
#include "video.h"
#include "fleet.h"

struct fleet fleets[2];

const unsigned char cell_row[CELLS] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9
};
const unsigned char cell_col[CELLS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9
};

const unsigned char ship_len[NSHIPS] = { 5, 4, 3, 3, 2 };
const char *const ship_name[NSHIPS] = {
    "vliegdekschip", "slagschip", "kruiser", "onderzeeboot", "torpedojager"
};

static unsigned int rng_state = 0xACE1;

void rng_stir(void)
{
    rng_state ^= (unsigned int)entropy() << 7;
    rng_state += entropy();
    if (!rng_state)
        rng_state = 0xACE1;
}

static unsigned int rng_next(void)
{
    unsigned int x = rng_state;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    rng_state = x;
    return x;
}

unsigned char random_below(unsigned char n)
{
    return (unsigned char)(rng_next() % n);
}

void fleet_clear(struct fleet *f)
{
    unsigned char i;
    for (i = 0; i < CELLS; i++)
        f->grid[i] = 0;
    for (i = 0; i < NSHIPS; i++) {
        f->ship[i].cell = NO_SHIP;
        f->ship[i].vertical = 0;
        f->ship[i].hits = 0;
    }
    f->afloat = 0;
    f->shots = 0;
    f->hits = 0;
}

unsigned char fleet_fits(const struct fleet *f, unsigned char s, unsigned char cell, unsigned char vertical)
{
    unsigned char n = ship_len[s], col = COL(cell), row = ROW(cell);
    signed char c0, c1, r0, r1, r, c;
    if (vertical ? row + n > SIZE : col + n > SIZE)
        return 0;
    /* the ship's cells and their ring must hold no other ship */
    c0 = (signed char)col - 1;
    r0 = (signed char)row - 1;
    c1 = (signed char)col + (vertical ? 1 : n);
    r1 = (signed char)row + (vertical ? n : 1);
    for (r = r0; r <= r1; r++) {
        if (r < 0 || r >= SIZE)
            continue;
        for (c = c0; c <= c1; c++) {
            if (c < 0 || c >= SIZE)
                continue;
            if (f->grid[r * SIZE + c] & SHIP_MASK)
                return 0;
        }
    }
    return 1;
}

void fleet_place(struct fleet *f, unsigned char s, unsigned char cell, unsigned char vertical)
{
    unsigned char k, step = vertical ? SIZE : 1;
    f->ship[s].cell = cell;
    f->ship[s].vertical = vertical;
    f->ship[s].hits = 0;
    for (k = 0; k < ship_len[s]; k++, cell += step)
        f->grid[cell] = s + 1;
    f->afloat++;
}

void fleet_remove(struct fleet *f, unsigned char s)
{
    unsigned char k, cell = f->ship[s].cell, step = f->ship[s].vertical ? SIZE : 1;
    if (cell == NO_SHIP)
        return;
    for (k = 0; k < ship_len[s]; k++, cell += step)
        f->grid[cell] = 0;
    f->ship[s].cell = NO_SHIP;
    f->afloat--;
}

/* Largest ship first; a dead end (possible in principle, rare in practice)
 * starts over. */
void fleet_random(struct fleet *f)
{
    unsigned char s, tries, cell, vertical;
restart:
    fleet_clear(f);
    for (s = 0; s < NSHIPS; s++) {
        for (tries = 0; ; tries++) {
            if (tries == 200)
                goto restart;
            vertical = random_below(2);
            cell = random_below(CELLS);
            if (fleet_fits(f, s, cell, vertical))
                break;
        }
        fleet_place(f, s, cell, vertical);
    }
}

unsigned char ship_segment(const struct fleet *f, unsigned char s, unsigned char cell)
{
    unsigned char anchor = f->ship[s].cell;
    return f->ship[s].vertical ? ROW(cell) - ROW(anchor) : COL(cell) - COL(anchor);
}

unsigned char fleet_shoot(struct fleet *f, unsigned char cell, unsigned char *ship)
{
    unsigned char s = f->grid[cell] & SHIP_MASK;
    f->grid[cell] |= SHOT;
    f->shots++;
    if (!s) {
        *ship = NO_SHIP;
        return RES_MISS;
    }
    s--;
    *ship = s;
    f->hits++;
    if (++f->ship[s].hits < ship_len[s])
        return RES_HIT;
    f->afloat--;
    return RES_SUNK;
}
