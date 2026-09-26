/* SPDX-License-Identifier: GPL-3.0-only */
/* panel.h -- the texts on the 64x21 text plane: the panel right of the big
 * grid (columns 44-62) and three rows under it (columns 0-42). */
#ifndef PANEL_H
#define PANEL_H

#define PANEL_COL   44
#define PANEL_WIDTH 19                      /* column 63 of row 20 would scroll */
#define ROW_STATUS  11
#define ROW_AIM     12
#define ROW_MINE    13                      /* the human's last shot */
#define ROW_THEIRS  14                      /* the computer's last shot */
#define ROW_EVENT   18                      /* under the big grid */
#define ROW_KEYS    19
#define ROW_KEYS2   20
#define ROW_CLOCK   20                      /* in the panel */
#define WIDE_WIDTH  43                      /* columns 0-42 under the grid */

extern void panel_reset(void);              /* new game: forget shots and events */
extern void draw_panel(void);               /* the texts of the current phase, but not the status */
extern void show_placing(void);             /* fleet list and cursor while placing */
extern void show_aim(void);                 /* "Doel      E5" */
extern void show_stats(void);               /* shots, hits, ratio, ships afloat */
extern void show_clock(void);               /* elapsed game time, if a clock exists */
extern void show_shot(unsigned char who, unsigned char cell, unsigned char result);
extern void show_event(const char *text, const char *name, const char *tail);   /* row 18 */
extern void show_status(const char *status);   /* row 11, written last */
extern void announce_turn(void);            /* "U bent aan zet" / placement prompt */
extern void announce_result(void);          /* after the last ship sank */

#endif
