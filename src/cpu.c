/* SPDX-License-Identifier: GPL-3.0-only */
/* cpu.c -- the computer's gunnery.
 *
 * The computer keeps its own chart of the human's sea. Besides the shots
 * themselves it notes what the rules give away: ships are straight and never
 * touch, so the diagonal neighbours of a hit are water, and so is the whole
 * ring around a sunk ship.
 *
 *   level 1  fires at random at any cell not yet fired upon
 *   level 2  hunts at random among the open cells; after a hit it tries the
 *            neighbours, and once two hits line up it follows the line
 *   level 3  counts, for every open cell, the ways in which the ships still
 *            afloat can lie across it (a placement over an unsunk hit counts
 *            much more), and fires at the likeliest cell
 */
#include "fleet.h"
#include "cpu.h"

#define K_OPEN  0                           /* nothing known */
#define K_MISS  1
#define K_WATER 2                           /* deduced: cannot hold a ship */
#define K_HIT   3                           /* part of a ship still afloat */
#define K_SUNK  4

#define HIT_WEIGHT 40                       /* level 3: a placement over a hit */

unsigned char cpu_level = 2;

static unsigned char know[CELLS];
static unsigned char alive[NSHIPS];
static unsigned char open_hits;             /* cells marked K_HIT */
static unsigned char pick[CELLS];           /* candidate list */
static unsigned int score[CELLS];

void cpu_reset(void)
{
    unsigned char i;
    for (i = 0; i < CELLS; i++)
        know[i] = K_OPEN;
    for (i = 0; i < NSHIPS; i++)
        alive[i] = 1;
    open_hits = 0;
}

/* know[] at (row, col), or K_WATER off the board. */
static unsigned char at(signed char row, signed char col)
{
    if (row < 0 || row >= SIZE || col < 0 || col >= SIZE)
        return K_WATER;
    return know[row * SIZE + col];
}

static void mark_water(signed char row, signed char col)
{
    if (at(row, col) == K_OPEN)
        know[row * SIZE + col] = K_WATER;
}

/* Marks the orthogonally connected hits around cell as sunk, and their
 * surroundings as water. */
static void sink_from(unsigned char cell)
{
    signed char row = ROW(cell), col = COL(cell), dr, dc;
    if (know[cell] != K_HIT)
        return;
    know[cell] = K_SUNK;
    open_hits--;
    for (dr = -1; dr <= 1; dr++)
        for (dc = -1; dc <= 1; dc++)
            mark_water(row + dr, col + dc);
    if (row > 0)        sink_from(cell - SIZE);
    if (row < SIZE - 1) sink_from(cell + SIZE);
    if (col > 0)        sink_from(cell - 1);
    if (col < SIZE - 1) sink_from(cell + 1);
}

void cpu_learn(unsigned char cell, unsigned char result, unsigned char ship)
{
    signed char row = ROW(cell), col = COL(cell);
    if (result == RES_MISS) {
        know[cell] = K_MISS;
        return;
    }
    know[cell] = K_HIT;
    open_hits++;
    mark_water(row - 1, col - 1);
    mark_water(row - 1, col + 1);
    mark_water(row + 1, col - 1);
    mark_water(row + 1, col + 1);
    if (result == RES_SUNK) {
        alive[ship] = 0;
        sink_from(cell);
    }
}

static unsigned char choose_from(unsigned char count)
{
    return pick[random_below(count)];
}

/* Level 1: any cell not fired upon. */
static unsigned char level_random(void)
{
    unsigned char i, n = 0;
    for (i = 0; i < CELLS; i++)
        if (!(fleets[HUMAN].grid[i] & SHOT))
            pick[n++] = i;
    return choose_from(n);
}

/* Any open cell (also the fallback of the other levels). */
static unsigned char level_hunt(void)
{
    unsigned char i, n = 0;
    for (i = 0; i < CELLS; i++)
        if (know[i] == K_OPEN)
            pick[n++] = i;
    return n ? choose_from(n) : level_random();
}

static const signed char DR[4] = { -1, 1, 0, 0 };
static const signed char DC[4] = { 0, 0, -1, 1 };

/* Level 2: next to a hit, preferring cells that extend a line of hits. */
static unsigned char level_target(void)
{
    unsigned char i, d, n = 0, lined = 0, cell;
    signed char row, col;
    if (!open_hits)
        return level_hunt();
    for (i = 0; i < CELLS; i++) {
        if (know[i] != K_HIT)
            continue;
        row = ROW(i);
        col = COL(i);
        for (d = 0; d < 4; d++) {
            if (at(row + DR[d], col + DC[d]) != K_OPEN)
                continue;
            cell = (unsigned char)((row + DR[d]) * SIZE + col + DC[d]);
            if (at(row - DR[d], col - DC[d]) == K_HIT) {
                if (!lined)
                    n = 0;                  /* lined-up candidates replace the others */
                lined = 1;
                pick[n++] = cell;
            } else if (!lined)
                pick[n++] = cell;
        }
    }
    return n ? choose_from(n) : level_hunt();
}

/* Could a ship of length len lie at (row, col)? Returns 0 if not, else
 * 1 + the number of open hits it covers. With open hits about, a placement
 * that touches one without covering it is impossible (ships never touch). */
static unsigned char placement(unsigned char row, unsigned char col, unsigned char len, unsigned char vertical)
{
    unsigned char k, hits = 0, kn;
    signed char r, c, r1, c1;
    unsigned char cell = row * SIZE + col, step = vertical ? SIZE : 1;
    for (k = 0; k < len; k++, cell += step) {
        kn = know[cell];
        if (kn == K_HIT)
            hits++;
        else if (kn != K_OPEN)
            return 0;
    }
    if (open_hits) {
        r1 = (signed char)row + (vertical ? len : 1);
        c1 = (signed char)col + (vertical ? 1 : len);
        for (r = (signed char)row - 1; r <= r1; r++)
            for (c = (signed char)col - 1; c <= c1; c++) {
                if (at(r, c) != K_HIT)
                    continue;
                if (vertical ? (c == (signed char)col && r >= (signed char)row && r < (signed char)(row + len))
                             : (r == (signed char)row && c >= (signed char)col && c < (signed char)(col + len)))
                    continue;
                return 0;
            }
    }
    return hits + 1;
}

/* Level 3: probability density over all placements of the ships afloat. */
static unsigned char level_density(void)
{
    unsigned char s, v, row, col, len, k, cell, step, n = 0, fits;
    unsigned int w, best = 0;
    for (cell = 0; cell < CELLS; cell++)
        score[cell] = 0;
    for (s = 0; s < NSHIPS; s++) {
        if (!alive[s])
            continue;
        len = ship_len[s];
        for (v = 0; v < 2; v++) {
            step = v ? SIZE : 1;
            for (row = 0; row < (v ? SIZE + 1 - len : SIZE); row++)
                for (col = 0; col < (v ? SIZE : SIZE + 1 - len); col++) {
                    fits = placement(row, col, len, v);
                    if (!fits)
                        continue;
                    w = fits > 1 ? 1 + (fits - 1) * HIT_WEIGHT : 1;
                    cell = row * SIZE + col;
                    for (k = 0; k < len; k++, cell += step)
                        score[cell] += w;
                }
        }
    }
    for (cell = 0; cell < CELLS; cell++) {
        if (know[cell] != K_OPEN || score[cell] < best || !score[cell])
            continue;
        if (score[cell] > best) {
            best = score[cell];
            n = 0;
        }
        pick[n++] = cell;
    }
    return n ? choose_from(n) : level_target();
}

unsigned char cpu_choose(void)
{
    if (cpu_level == 1)
        return level_random();
    if (cpu_level == 2)
        return level_target();
    return level_density();
}
