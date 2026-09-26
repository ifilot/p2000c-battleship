/* SPDX-License-Identifier: GPL-3.0-only */
/* cpu.h -- the computer's gunnery. */
#ifndef CPU_H
#define CPU_H

extern unsigned char cpu_level;             /* 1 random, 2 hunt and target, 3 probability */

/* Forgets everything about the human fleet (new game). */
extern void cpu_reset(void);

/* The cell the computer fires at next (never one fired at before). */
extern unsigned char cpu_choose(void);

/* Takes in the result of a shot at cell (RES_*, and the ship when sunk). */
extern void cpu_learn(unsigned char cell, unsigned char result, unsigned char ship);

#endif
