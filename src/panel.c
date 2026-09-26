/* SPDX-License-Identifier: GPL-3.0-only */
/* panel.c -- the texts beside and under the big grid.
 *
 * Panel lines are padded to the panel width so a shorter text overwrites a
 * longer one; column 63 is never written (on the last row that would
 * scroll the text plane). The status line is written last after every
 * update, so it doubles as a display-complete marker for the tests.
 */
#include "video.h"
#include "fleet.h"
#include "cpu.h"
#include "game.h"
#include "panel.h"
#include "clock.h"

#define CLOCK_MAX 35999u                    /* 9:59:59 */
#define COL_MINE  54                        /* right edge of the human's numbers */
#define COL_THEIRS 61                       /* ... and of the computer's */

/* Prints s (then more, if any) at (row, col), padded with spaces to width. */
static void put3(unsigned char row, unsigned char col, const char *s, const char *more, const char *tail,
                 unsigned char width)
{
    con_at(ROWCOL(row, col));
    while (*s && width) { conout(*s++); width--; }
    if (more)
        while (*more && width) { conout(*more++); width--; }
    if (tail)
        while (*tail && width) { conout(*tail++); width--; }
    while (width--)
        conout(' ');
}

static void put(unsigned char row, const char *s)
{
    put3(row, PANEL_COL, s, 0, 0, PANEL_WIDTH);
}

static void put_wide(unsigned char row, const char *s)
{
    put3(row, 0, s, 0, 0, WIDE_WIDTH);
}

/* Right-aligned number (0..255) ending at column col. */
static void put_number(unsigned char row, unsigned char col, unsigned char n)
{
    con_at(ROWCOL(row, col - 2));
    conout(n >= 100 ? '0' + n / 100 : ' ');
    conout(n >= 10 ? '0' + n / 10 % 10 : ' ');
    conout('0' + n % 10);
}

/* "E5", "J10": at most three characters, padded to three. */
static void put_cell(unsigned char cell)
{
    unsigned char row = ROW(cell) + 1;
    conout('A' + COL(cell));
    if (row == 10) {
        conout('1');
        conout('0');
    } else {
        conout('0' + row);
        conout(' ');
    }
}

static void put_capitalised(const char *name)
{
    conout(*name - 'a' + 'A');
    con_puts(name + 1);
}

/* --- placing the fleet ----------------------------------------------------------- */

void show_placing(void)
{
    unsigned char s;
    for (s = 0; s < NSHIPS; s++) {
        con_at(ROWCOL(3 + s, PANEL_COL));
        conout(s == place_ship ? '>' : fleets[HUMAN].ship[s].cell != NO_SHIP ? '+' : ' ');
        conout(' ');
        put_capitalised(ship_name[s]);
        con_at(ROWCOL(3 + s, PANEL_COL + 17));
        conout('0' + ship_len[s]);
    }
    if (place_ship == NO_SHIP)
        put(ROW_AIM, "RETURN: beginnen");
    else {
        put(ROW_AIM, "Positie");
        con_at(ROWCOL(ROW_AIM, PANEL_COL + 15));
        put_cell(place_cell);
    }
}

/* --- battle ------------------------------------------------------------------------ */

void show_aim(void)
{
    if (cursor == NO_CURSOR) {
        put(ROW_AIM, "");
        return;
    }
    put(ROW_AIM, "Doel");
    con_at(ROWCOL(ROW_AIM, PANEL_COL + 15));
    put_cell(cursor);
}

static unsigned char ratio(const struct fleet *f)
{
    return f->shots ? (unsigned char)((unsigned int)f->hits * 100 / f->shots) : 0;
}

/* Numbers of the human (the shots at the computer's fleet) and the computer. */
void show_stats(void)
{
    const struct fleet *mine = &fleets[COMPUTER], *theirs = &fleets[HUMAN];
    put_number(16, COL_MINE, mine->shots);    put_number(16, COL_THEIRS, theirs->shots);
    put_number(17, COL_MINE, mine->hits);     put_number(17, COL_THEIRS, theirs->hits);
    put_number(18, COL_MINE, ratio(mine));    put_number(18, COL_THEIRS, ratio(theirs));
    put_number(19, COL_MINE, fleets[HUMAN].afloat);
    put_number(19, COL_THEIRS, fleets[COMPUTER].afloat);
}

/* "Tijd  0:12:34": hours, minutes and seconds, capped at 9:59:59. */
void show_clock(void)
{
    unsigned int s = clock_seconds(), h, m;
    if (!clock_available || phase == PHASE_PLACE)
        return;
    if (s > CLOCK_MAX)
        s = CLOCK_MAX;
    h = s / 3600;
    m = (s / 60) % 60;
    s %= 60;
    con_at(ROWCOL(ROW_CLOCK, PANEL_COL + 6));
    conout('0' + h); conout(':');
    conout('0' + m / 10); conout('0' + m % 10); conout(':');
    conout('0' + s / 10); conout('0' + s % 10);
}

static const char *const RESULT[3] = { "mis    ", "raak   ", "zinkt! " };

/* The last shot of each side and the event line, kept so the panel can be
 * rebuilt after the help screen or the screen saver. */
static unsigned char last_cell[2] = { NO_CURSOR, NO_CURSOR };
static unsigned char last_result[2];
static const char *event[3];

void panel_reset(void)
{
    last_cell[HUMAN] = last_cell[COMPUTER] = NO_CURSOR;
    event[0] = "";
    event[1] = event[2] = 0;
}

/* "U       E5  raak" / "P2000C  C3  mis" */
void show_shot(unsigned char who, unsigned char cell, unsigned char result)
{
    unsigned char row = who == HUMAN ? ROW_MINE : ROW_THEIRS;
    last_cell[who] = cell;
    last_result[who] = result;
    put(row, who == HUMAN ? "U" : "P2000C");
    con_at(ROWCOL(row, PANEL_COL + 8));
    put_cell(cell);
    con_at(ROWCOL(row, PANEL_COL + 12));
    con_puts(RESULT[result]);
}

void show_event(const char *text, const char *name, const char *tail)
{
    event[0] = text;
    event[1] = name;
    event[2] = tail;
    put3(ROW_EVENT, 0, text, name, tail, WIDE_WIDTH);
}

void show_status(const char *status)
{
    put(ROW_STATUS, status);
}

void announce_turn(void)
{
    if (phase == PHASE_PLACE)
        show_status(place_ship == NO_SHIP ? "Vloot gereed!" : place_ok ? "Kies een plek" : "Past hier niet");
    else
        show_status("U bent aan zet");
}

void announce_result(void)
{
    put_wide(ROW_KEYS, "N nieuw spel   Q stoppen   H hulp");
    put_wide(ROW_KEYS2, "");
    if (winner == HUMAN) {
        show_event("De hele vloot van de P2000C is gezonken!", 0, 0);
        show_status("U hebt gewonnen!");
    } else {
        show_event("Uw hele vloot is gezonken.", 0, 0);
        show_status("De P2000C wint");
    }
}

/* --- static texts ------------------------------------------------------------------- */

static const char *const LEVEL[3] = { "Niveau 1  licht", "Niveau 2  normaal", "Niveau 3  zwaar" };

void draw_panel(void)
{
    put(0, "Z E E S L A G");
    put(1, "Philips P2000C");
    if (phase == PHASE_PLACE) {
        put(2, "Plaats uw vloot:");
        put(9, "Schepen mogen");
        put(10, "elkaar niet raken.");
        put(15, LEVEL[cpu_level - 1]);
        put_wide(ROW_EVENT, "Pijltjes/WASD verplaatsen   R draaien");
        put_wide(ROW_KEYS, "RETURN plaatsen   Z willekeurig   U terug");
        put_wide(ROW_KEYS2, "H hulp   N nieuw spel   Q stoppen");
        show_placing();
        return;
    }
    put(2, "Uw vloot");
    put(15, "");
    con_at(ROWCOL(15, COL_MINE));   conout('U');
    con_at(ROWCOL(15, COL_THEIRS - 5)); con_puts("P2000C");
    put(16, "Schoten");
    put(17, "Raak");
    put(18, "Raak %");
    put(19, "Vloot");
    if (clock_available)
        put(ROW_CLOCK, "Tijd");
    show_stats();
    show_clock();
    if (last_cell[HUMAN] != NO_CURSOR)
        show_shot(HUMAN, last_cell[HUMAN], last_result[HUMAN]);
    if (last_cell[COMPUTER] != NO_CURSOR)
        show_shot(COMPUTER, last_cell[COMPUTER], last_result[COMPUTER]);
    if (phase == PHASE_OVER)
        return;
    show_event(event[0], event[1], event[2]);
    put_wide(ROW_KEYS, "Pijltjes/WASD richten   RETURN vuren");
    put_wide(ROW_KEYS2, "H hulp   N nieuw spel   Q stoppen");
    show_aim();
}
