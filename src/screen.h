/* SPDX-License-Identifier: GPL-3.0-only */
/* screen.h -- the battle picture: big grid, mini map, uploads and animations.
 *
 * Geometry in dots and lines. Dots have a 3:5 pitch on the CRT, so a
 * 32x19-dot cell is square and a 14x8-dot mini-map cell nearly so.
 *
 *   big grid   10 x 10 cells from x = 16, line 22 (grid lines included),
 *              framed two dots outside; letters above, numbers on the left
 *   mini map   10 x 10 cells from x = 362, line 38, in the panel on the right
 *   text       the 64x21 text plane: panel from column 44, three rows
 *              (18-20) under the big grid
 */
#ifndef SCREEN_H
#define SCREEN_H

#define GRID_BYTE   2                       /* byte column of the left grid line */
#define GRID_X      16
#define GRID_TOP    22
#define CELL_BYTES  4
#define CELL_LINES  19
#define CELL_DOTS   32
#define FRAME_GAP   2

#define MINI_X      362
#define MINI_TOP    38
#define MINI_W      14
#define MINI_H      8

#define VIEW_PLACE  0                       /* big grid: own sea while placing */
#define VIEW_BATTLE 1                       /* big grid: enemy sea; mini map: own fleet */

extern unsigned char view;

/* Composes the whole picture for the current view in RAM. */
extern void screen_compose(void);

/* Sends the composed picture after ESC 3: lines as vectors, the rest as
 * row uploads of what differs from them. */
extern void screen_flush(void);

/* Redraws and re-sends every cell of the big grid and the mini map whose
 * appearance changed. */
extern void screen_sync(void);

/* The human's shot at cell of the enemy sea: gun sight, explosion or
 * plume, then the result; a sunk ship flashes before it shows as a wreck.
 * Call after the shot was resolved. */
extern void screen_shot(unsigned char cell, unsigned char result, unsigned char ship);

/* The computer's aim at the human's cell: a flashing box on the mini map.
 * Call before the shot is resolved, and screen_sync() after. */
extern void screen_cpu_shot(unsigned char cell);

#endif
