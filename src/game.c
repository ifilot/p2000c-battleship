/* SPDX-License-Identifier: GPL-3.0-only */
/* game.c -- game state and flow: placing the fleet, the battle, the end.
 *
 * The human places the fleet on the big grid (the computer's fleet is
 * placed at random), then the big grid turns to the enemy's sea and the
 * human's own fleet moves to the mini map. The human fires first; the
 * sides alternate one shot at a time until one fleet is sunk. After every
 * change the picture is brought up to date through the screen module and
 * the texts through the panel module, the status line last.
 */
#include "video.h"
#include "fleet.h"
#include "cpu.h"
#include "game.h"
#include "screen.h"
#include "panel.h"
#include "screens.h"
#include "saver.h"
#include "clock.h"

unsigned char phase;
unsigned char cursor;
unsigned char winner;
unsigned char place_ship, place_cell, place_vertical, place_ok;

#define GO_ON    2                          /* play loops: next phase */
#define START_CELL 44                       /* E5 */

/* Cursor keys. The P2000C keyboard's cursor quadrant emits the WordStar
 * diamond (^S ^D ^E ^X, as P2EDIT and SuperCalc expect); the graphical
 * emulator sends the terminal's own cursor-control bytes instead, so both
 * sets are accepted. */
#define KEY_LEFT   0x13                     /* ^S */
#define KEY_RIGHT  0x04                     /* ^D */
#define KEY_UP     0x05                     /* ^E */
#define KEY_DOWN   0x18                     /* ^X */
#define KEY_LEFT2  0x15
#define KEY_RIGHT2 0x06
#define KEY_UP2    0x1A
#define KEY_DOWN2  0x0A
#define KEY_CR     0x0D
#define KEY_BS     0x08
#define KEY_DEL    0x7F
#define BEL        0x07

/* --- keys ------------------------------------------------------------------------ */

/* Idle hook while waiting for the player: keeps the clock display current. */
static void tick_clock(void)
{
    if (clock_update())
        show_clock();
}

/* Next key, lower case; every key press stirs the random numbers. */
static unsigned char get_key(void)
{
    unsigned char key = wait_key_idle(redraw_game_screen, tick_clock);
    rng_stir();
    if (key >= 'A' && key <= 'Z')
        key += 'a' - 'A';
    return key;
}

/* Direction of a cursor key as (dcol, drow) packed in *dc, *dr; 0 if none. */
static unsigned char direction(unsigned char key, signed char *dc, signed char *dr)
{
    *dc = *dr = 0;
    switch (key) {
    case KEY_LEFT:  case KEY_LEFT2:  case 'a': *dc = -1; return 1;
    case KEY_RIGHT: case KEY_RIGHT2: case 'd': *dc = 1;  return 1;
    case KEY_UP:    case KEY_UP2:    case 'w': *dr = -1; return 1;
    case KEY_DOWN:  case KEY_DOWN2:  case 's': *dr = 1;  return 1;
    }
    return 0;
}

/* "Stoppen? (J/N)": returns 1 when the player confirms. */
static unsigned char confirm_quit(void)
{
    unsigned char key;
    show_status("Stoppen? (J/N)");
    key = conin();
    if (key == 'j' || key == 'J' || key == 'y' || key == 'Y')
        return 1;
    if (phase == PHASE_OVER)
        announce_result();
    else
        announce_turn();
    return 0;
}

/* H, N and Q, common to all phases: returns GO_ON to carry on, else the
 * value play() should return. */
static unsigned char common_key(unsigned char key)
{
    if (key == 'h')
        help_screen();
    else if (key == 'n')
        return 1;
    else if (key == 'q' && confirm_quit())
        return 0;
    return GO_ON;
}

/* --- screen ---------------------------------------------------------------------- */

/* Composes and sends the whole screen of the current phase. */
static void show_screen(void)
{
    view = phase == PHASE_PLACE ? VIEW_PLACE : VIEW_BATTLE;
    screen_compose();
    video_graphics();
    draw_panel();
    screen_flush();
    if (phase == PHASE_OVER)
        announce_result();
    else
        announce_turn();
}

/* Rebuilds the game screen after the help page or the screen saver (both
 * leave graphics mode). The framebuffer in RAM is intact. */
void redraw_game_screen(void)
{
    video_graphics();
    draw_panel();
    screen_flush();
    if (phase == PHASE_OVER)
        announce_result();
    else
        announce_turn();
}

/* --- placing the fleet ------------------------------------------------------------- */

/* Keeps the ship being placed on the board and notes whether it may lie there. */
static void clamp_placement(void)
{
    unsigned char n = ship_len[place_ship], col = COL(place_cell), row = ROW(place_cell);
    if (place_vertical && row > SIZE - n)
        row = SIZE - n;
    if (!place_vertical && col > SIZE - n)
        col = SIZE - n;
    place_cell = row * SIZE + col;
    place_ok = fleet_fits(&fleets[HUMAN], place_ship, place_cell, place_vertical);
}

static void refresh_placing(void)
{
    if (place_ship != NO_SHIP)
        clamp_placement();
    screen_sync();
    show_placing();
    announce_turn();
}

/* The next ship still to be placed, or NO_SHIP. */
static unsigned char next_unplaced(void)
{
    unsigned char s;
    for (s = 0; s < NSHIPS; s++)
        if (fleets[HUMAN].ship[s].cell == NO_SHIP)
            return s;
    return NO_SHIP;
}

/* Takes back the last placed ship; it is picked up where it lay. */
static void undo_placement(void)
{
    signed char s;
    for (s = NSHIPS - 1; s >= 0; s--)
        if (fleets[HUMAN].ship[s].cell != NO_SHIP)
            break;
    if (s < 0) {
        conout(BEL);
        return;
    }
    place_ship = (unsigned char)s;
    place_cell = fleets[HUMAN].ship[s].cell;
    place_vertical = fleets[HUMAN].ship[s].vertical;
    fleet_remove(&fleets[HUMAN], place_ship);
}

/* Returns GO_ON when the fleet is complete and RETURN was pressed. */
static unsigned char place_fleet(void)
{
    unsigned char key, r;
    signed char dc, dr, col, row;
    for (;;) {
        key = get_key();
        if (direction(key, &dc, &dr)) {
            if (place_ship == NO_SHIP)
                continue;
            col = (signed char)COL(place_cell) + dc;
            row = (signed char)ROW(place_cell) + dr;
            if (col < 0 || row < 0 || col >= SIZE || row >= SIZE)
                continue;
            place_cell = row * SIZE + col;
            refresh_placing();
            continue;
        }
        switch (key) {
        case 'r':
            if (place_ship == NO_SHIP)
                break;
            place_vertical ^= 1;
            refresh_placing();
            break;
        case KEY_CR: case ' ':
            if (place_ship == NO_SHIP)
                return GO_ON;
            if (!place_ok) {
                conout(BEL);
                break;
            }
            fleet_place(&fleets[HUMAN], place_ship, place_cell, place_vertical);
            place_ship = next_unplaced();
            refresh_placing();
            break;
        case 'z':
            fleet_random(&fleets[HUMAN]);
            place_ship = NO_SHIP;
            refresh_placing();
            break;
        case 'u': case KEY_BS: case KEY_DEL:
            undo_placement();
            refresh_placing();
            break;
        default:
            r = common_key(key);
            if (r != GO_ON)
                return r;
        }
    }
}

/* --- battle ------------------------------------------------------------------------- */

static void game_over(unsigned char who_won)
{
    winner = who_won;
    phase = PHASE_OVER;
    cursor = NO_CURSOR;
    clock_freeze();
    screen_sync();                          /* reveals the ships the computer still has */
    show_aim();
    show_clock();
    announce_result();
}

/* The computer fires once. */
static void cpu_turn(void)
{
    unsigned char cell, ship, result;
    show_status("P2000C richt...");
    cell = cpu_choose();
    screen_cpu_shot(cell);
    result = fleet_shoot(&fleets[HUMAN], cell, &ship);
    cpu_learn(cell, result, ship);
    screen_sync();
    show_shot(COMPUTER, cell, result);
    show_stats();
    if (result == RES_SUNK)
        show_event("Uw ", ship_name[ship], " is gezonken!");
    if (!fleets[HUMAN].afloat)
        game_over(COMPUTER);
}

/* The human fires at the cursor; 0 if that cell was fired at before. */
static unsigned char fire(void)
{
    unsigned char cell = cursor, ship, result;
    if (fleets[COMPUTER].grid[cell] & SHOT)
        return 0;
    show_status("Vuur!");
    show_event("", 0, 0);
    result = fleet_shoot(&fleets[COMPUTER], cell, &ship);
    screen_shot(cell, result, ship);
    show_shot(HUMAN, cell, result);
    show_stats();
    if (result == RES_SUNK)
        show_event("Het ", ship_name[ship], " van de P2000C is gezonken!");
    if (!fleets[COMPUTER].afloat) {
        game_over(HUMAN);
        return 1;
    }
    cpu_turn();
    if (phase != PHASE_OVER)
        announce_turn();
    return 1;
}

static unsigned char battle(void)
{
    unsigned char key, r;
    signed char dc, dr, col, row;
    for (;;) {
        key = get_key();
        if (direction(key, &dc, &dr)) {
            if (phase != PHASE_BATTLE)
                continue;
            col = (signed char)COL(cursor) + dc;
            row = (signed char)ROW(cursor) + dr;
            if (col < 0 || row < 0 || col >= SIZE || row >= SIZE)
                continue;
            cursor = row * SIZE + col;
            screen_sync();
            show_aim();
            continue;
        }
        if (key == KEY_CR || key == ' ') {
            if (phase != PHASE_BATTLE || !fire())
                conout(BEL);
            continue;
        }
        r = common_key(key);
        if (r != GO_ON)
            return r;
    }
}

/* --- one game ------------------------------------------------------------------------- */

unsigned char play(void)
{
    unsigned char r;

    fleet_clear(&fleets[HUMAN]);
    fleet_random(&fleets[COMPUTER]);
    cpu_reset();
    panel_reset();
    phase = PHASE_PLACE;
    cursor = NO_CURSOR;
    place_ship = 0;
    place_cell = START_CELL;
    place_vertical = 0;
    clamp_placement();
    show_screen();
    r = place_fleet();
    if (r != GO_ON)
        return r;

    phase = PHASE_BATTLE;
    place_ship = NO_SHIP;
    cursor = START_CELL;
    clock_reset();
    show_screen();
    return battle();
}
