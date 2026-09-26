/* SPDX-License-Identifier: GPL-3.0-only */
/* game.h -- game state shared by the modules, and the game flow. */
#ifndef GAME_H
#define GAME_H

#define PHASE_PLACE  0                      /* the human places the fleet */
#define PHASE_BATTLE 1
#define PHASE_OVER   2

#define NO_CURSOR 0xFF

extern unsigned char phase;
extern unsigned char cursor;                /* aim on the enemy sea, or NO_CURSOR */
extern unsigned char winner;                /* HUMAN or COMPUTER once the game is over */

/* The ship being placed (NO_SHIP when the fleet is complete), its top-left
 * cell and orientation; place_ok when it may lie there. */
extern unsigned char place_ship, place_cell, place_vertical, place_ok;

/* One game against the computer at cpu_level; returns 1 to go back to the
 * start screen (N), 0 to leave the program (Q confirmed). */
extern unsigned char play(void);

/* Restores the game screen after a text-mode interlude (help, screen saver). */
extern void redraw_game_screen(void);

#endif
