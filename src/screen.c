/* SPDX-License-Identifier: GPL-3.0-only */
/* screen.c -- the battle picture.
 *
 * Everything is composed in the 16 KiB framebuffer. Every straight line of
 * the picture (grid, frames) is also kept in a list of vectors: a fresh
 * picture goes over the 19200-baud link as those vectors, which the terminal
 * draws itself, plus row uploads of only the bytes that differ from what the
 * vectors drew. Each big-grid cell and each mini-map cell keeps a record of
 * the appearance last sent, so afterwards only changed cells are uploaded.
 *
 * Big-grid cells are whole 4x19-byte tiles (their top and left grid line
 * included, so a ship can paint over the grid between its cells), with
 * overlays: the wreck's shell hole, the cursor, the gun sight, inverse video
 * for a ship that cannot lie where it is being placed.
 */
#include "video.h"
#include "fleet.h"
#include "game.h"
#include "screen.h"
#include "clock.h"
#include "sprites.h"

unsigned char view;

/* --- appearance codes ------------------------------------------------------------ */

#define T_WATER   0                         /* + wave layout 0..2 */
#define T_MISS    3
#define T_FIRE    4
#define T_BOOM    5
#define T_SPLASH  6
#define T_SHIP    8                         /* + index into tile_ship */
#define L_TILE    0x00FF
#define L_CRATER  0x0100
#define L_CURSOR  0x0200
#define L_INVERT  0x0400
#define L_AIM     0x0800
#define L_NONE    0xFFFF                    /* forces a redraw */

#define M_MISS    0x01
#define M_SHIP    0x02
#define M_HIT     0x04
#define M_SUNK    0x08
#define M_AIM     0x10
#define M_NONE    0xFF

#define CURSOR_ROW0 2                       /* rows of the cursor brackets in a tile */
#define CURSOR_ROW1 14
#define CURSOR_ROWS 4

static unsigned int shown[CELLS];           /* big grid: appearance last sent */
static unsigned char mini_shown[CELLS];

/* Animation overrides: one big-grid cell with a fixed look, a ship that
 * flashes in inverse video, the computer's aim on the mini map. */
static unsigned char anim_cell = NO_CURSOR;
static unsigned int anim_look;
static unsigned char flash_ship = NO_SHIP;
static unsigned char mini_aim = NO_CURSOR;

#define WAIT_FRAME 9                        /* ticks per animation frame (60 Hz) */

/* --- vectors ------------------------------------------------------------------------ */

#define MAX_VECTORS 32

struct vector {
    unsigned int x0, x1;
    unsigned char line0, line1;
};

static struct vector vectors[MAX_VECTORS];
static unsigned char nvectors;

static void plot(unsigned int x, unsigned char line)
{
    framebuffer[line * FB_LINE + (x >> 3)] |= 0x80 >> (x & 7);
}

static void draw_line(unsigned int x0, unsigned int line0, unsigned int x1, unsigned int line1)
{
    unsigned int x, line;
    for (line = line0; line <= line1; line++)
        for (x = x0; x <= x1; x++)
            plot(x, line);
}

/* An axis-aligned line: drawn into the framebuffer and remembered. */
static void add_line(unsigned int x0, unsigned int line0, unsigned int x1, unsigned int line1)
{
    struct vector *v = &vectors[nvectors++];
    v->x0 = x0; v->x1 = x1; v->line0 = line0; v->line1 = line1;
    draw_line(x0, line0, x1, line1);
}

static void send_vector(const struct vector *v)
{
    conout(27); conout('m'); conout(v->x0 & 0xFF); conout(v->x0 >> 8); conout(251 - v->line0);
    conout(27); conout('M'); conout(v->x1 & 0xFF); conout(v->x1 >> 8); conout(251 - v->line1);
}

/* What the vectors put on one line, as 64 bytes. */
static unsigned char pattern[FB_LINE];

static void vector_pattern(unsigned char line)
{
    unsigned char i;
    unsigned int x;
    const struct vector *v;
    for (i = 0; i < FB_LINE; i++)
        pattern[i] = 0;
    for (i = 0, v = vectors; i < nvectors; i++, v++) {
        if (line < v->line0 || line > v->line1)
            continue;
        for (x = v->x0; x <= v->x1; x++)
            pattern[x >> 3] |= 0x80 >> (x & 7);
    }
}

/* --- big grid ---------------------------------------------------------------------- */

static unsigned int cell_offset(unsigned char cell)
{
    return (GRID_TOP + ROW(cell) * CELL_LINES) * FB_LINE + GRID_BYTE + COL(cell) * CELL_BYTES;
}

static unsigned char water(unsigned char cell)
{
    return T_WATER + wave_layout[cell];
}

/* Tile of ship s of fleet f at cell. */
static unsigned char ship_tile(const struct fleet *f, unsigned char s, unsigned char cell)
{
    return T_SHIP + ship_tile_first[s] + (f->ship[s].vertical ? ship_len[s] : 0) + ship_segment(f, s, cell);
}

/* Is cell part of the ship being placed? */
static unsigned char in_preview(unsigned char cell)
{
    unsigned char n = ship_len[place_ship];
    if (place_vertical)
        return COL(cell) == COL(place_cell) && ROW(cell) >= ROW(place_cell) && ROW(cell) < ROW(place_cell) + n;
    return ROW(cell) == ROW(place_cell) && COL(cell) >= COL(place_cell) && COL(cell) < COL(place_cell) + n;
}

/* What a big-grid cell should look like right now. */
static unsigned int big_look(unsigned char cell)
{
    const struct fleet *f;
    unsigned int look;
    unsigned char g, s, reveal;

    if (cell == anim_cell)
        return anim_look;
    if (view == VIEW_PLACE) {
        f = &fleets[HUMAN];
        if (place_ship != NO_SHIP && in_preview(cell)) {
            look = T_SHIP + ship_tile_first[place_ship] + (place_vertical ? ship_len[place_ship] : 0)
                 + (place_vertical ? ROW(cell) - ROW(place_cell) : COL(cell) - COL(place_cell));
            return place_ok ? look : look | L_INVERT;
        }
        s = f->grid[cell] & SHIP_MASK;
        return s ? ship_tile(f, s - 1, cell) : water(cell);
    }
    f = &fleets[COMPUTER];
    g = f->grid[cell];
    s = g & SHIP_MASK;
    reveal = phase == PHASE_OVER;
    if (!s)
        look = (g & SHOT) ? T_MISS : water(cell);
    else {
        s--;
        if (f->ship[s].hits == ship_len[s])
            look = ship_tile(f, s, cell) | L_CRATER;
        else if (g & SHOT)
            look = reveal ? ship_tile(f, s, cell) | L_CRATER : T_FIRE;
        else
            look = reveal ? ship_tile(f, s, cell) : water(cell);
        if (s == flash_ship)
            look |= L_INVERT;
    }
    if (cell == cursor)
        look |= L_CURSOR;
    return look;
}

static void draw_cell(unsigned char cell, unsigned int look)
{
    unsigned int offset = cell_offset(cell);
    unsigned char t = look & L_TILE;
    const unsigned char *tile;
    if (t < T_MISS)
        tile = tile_water[t];
    else if (t == T_MISS)
        tile = tile_miss;
    else if (t == T_FIRE)
        tile = tile_fire;
    else if (t == T_BOOM)
        tile = tile_boom;
    else if (t == T_SPLASH)
        tile = tile_splash;
    else
        tile = tile_ship[t - T_SHIP];
    video_copy(tile, offset, WH(CELL_BYTES, CELL_LINES));
    if (look & L_CRATER) {
        video_mask(over_crater_mask, offset, WH(CELL_BYTES, CELL_LINES));
        video_blit(over_crater_ink, offset, WH(CELL_BYTES, CELL_LINES));
    }
    if (look & L_INVERT)
        video_xor(over_invert, offset, WH(CELL_BYTES, CELL_LINES));
    if (look & L_AIM)
        video_xor(over_aim, offset, WH(CELL_BYTES, CELL_LINES));
    if (look & L_CURSOR)
        video_xor(over_cursor, offset, WH(CELL_BYTES, CELL_LINES));
    shown[cell] = look;
}

/* Sends rows [first, first + rows) of the cells first_col..last_col of a grid row. */
static void flush_cells(unsigned char row, unsigned char first_col, unsigned char last_col,
                        unsigned char first, unsigned char rows)
{
    video_flush_rect(COLROW(GRID_BYTE + first_col * CELL_BYTES, GRID_TOP + row * CELL_LINES + first),
                     WH((last_col - first_col + 1) * CELL_BYTES, rows));
}

/* Redraws the changed cells; per grid row the changed span goes out as one
 * upload per line. When only the cursor moved, only its bracket rows go. */
static void sync_big(void)
{
    unsigned char row, col, cell, first, last = 0, full;
    unsigned int look, diff;
    for (row = 0, cell = 0; row < SIZE; row++) {
        first = SIZE;
        full = 0;
        for (col = 0; col < SIZE; col++, cell++) {
            look = big_look(cell);
            diff = look ^ shown[cell];
            if (!diff)
                continue;
            if (shown[cell] == L_NONE || (diff & ~L_CURSOR))
                full = 1;
            draw_cell(cell, look);
            if (first == SIZE)
                first = col;
            last = col;
        }
        if (first == SIZE)
            continue;
        if (full)
            flush_cells(row, first, last, 0, CELL_LINES);
        else {
            flush_cells(row, first, last, CURSOR_ROW0, CURSOR_ROWS);
            flush_cells(row, first, last, CURSOR_ROW1, CURSOR_ROWS);
        }
    }
}

/* Grid, frame and labels, plus every cell. */
static void compose_big(void)
{
    unsigned char i, cell;
    unsigned int x;
    for (i = 0; i <= SIZE; i++) {
        add_line(GRID_X, GRID_TOP + i * CELL_LINES, GRID_X + SIZE * CELL_DOTS, GRID_TOP + i * CELL_LINES);
        x = GRID_X + i * CELL_DOTS;
        add_line(x, GRID_TOP, x, GRID_TOP + SIZE * CELL_LINES);
    }
    add_line(GRID_X - FRAME_GAP, GRID_TOP - FRAME_GAP, GRID_X + SIZE * CELL_DOTS + FRAME_GAP, GRID_TOP - FRAME_GAP);
    add_line(GRID_X - FRAME_GAP, GRID_TOP + SIZE * CELL_LINES + FRAME_GAP,
             GRID_X + SIZE * CELL_DOTS + FRAME_GAP, GRID_TOP + SIZE * CELL_LINES + FRAME_GAP);
    add_line(GRID_X - FRAME_GAP, GRID_TOP - FRAME_GAP, GRID_X - FRAME_GAP, GRID_TOP + SIZE * CELL_LINES + FRAME_GAP);
    add_line(GRID_X + SIZE * CELL_DOTS + FRAME_GAP, GRID_TOP - FRAME_GAP,
             GRID_X + SIZE * CELL_DOTS + FRAME_GAP, GRID_TOP + SIZE * CELL_LINES + FRAME_GAP);
    for (i = 0; i < SIZE; i++) {
        video_shift_or(glyph_letter[i], GRID_X + i * CELL_DOTS + 12, GRID_TOP - 16, WH(1, 12));
        video_blit(glyph_number[i], (GRID_TOP + i * CELL_LINES + 4) * FB_LINE, WH(2, 12));
    }
    for (cell = 0; cell < CELLS; cell++)
        draw_cell(cell, big_look(cell));
}

/* --- mini map ----------------------------------------------------------------------- */

#define MINI_BYTE0  ((MINI_X - 2) >> 3)     /* bytes 45..62 hold map and frame */
#define MINI_BYTES  (((MINI_X + SIZE * MINI_W + 1) >> 3) - MINI_BYTE0 + 1)
#define MINI_LINE0  (MINI_TOP - 2)
#define MINI_LINES  (SIZE * MINI_H + 4)

static unsigned char mini_look(unsigned char cell)
{
    unsigned char g = fleets[HUMAN].grid[cell], s = g & SHIP_MASK, look = 0;
    if (s) {
        look = M_SHIP;
        if (g & SHOT)
            look |= M_HIT;
        if (fleets[HUMAN].ship[s - 1].hits == ship_len[s - 1])
            look |= M_SUNK;
    } else if (g & SHOT)
        look = M_MISS;
    if (cell == mini_aim)
        look |= M_AIM;
    return look;
}

/* XORs a box around the inside of a mini-map cell. */
static void mini_box(unsigned int x, unsigned char line)
{
    static const unsigned char top[2] = { 0xFF, 0xFC };           /* 14 dots */
    static const unsigned char side[2] = { 0x80, 0x04 };
    unsigned char k;
    video_shift_xor(top, x, line, WH(2, 1));
    video_shift_xor(top, x, line + MINI_H - 1, WH(2, 1));
    for (k = 1; k < MINI_H - 1; k++)
        video_shift_xor(side, x, line + k, WH(2, 1));
}

/* The mini map's frame; recorded as vectors only when the picture is composed. */
static void mini_frame(unsigned char record)
{
    unsigned int x0 = MINI_X - 2, x1 = MINI_X + SIZE * MINI_W + 1;
    unsigned char l0 = MINI_TOP - 2, l1 = MINI_TOP + SIZE * MINI_H + 1;
    void (*line)(unsigned int, unsigned int, unsigned int, unsigned int) = record ? add_line : draw_line;
    line(x0, l0, x1, l0);
    line(x0, l1, x1, l1);
    line(x0, l0, x0, l1);
    line(x1, l0, x1, l1);
}

/* Recomposes the whole mini map, frame included. */
static void compose_mini(void)
{
    unsigned char s, r, c, cell, look, index;
    unsigned int x, offset;
    const struct fleet *f = &fleets[HUMAN];
    for (r = 0, offset = MINI_LINE0 * FB_LINE + MINI_BYTE0; r < MINI_LINES; r++, offset += FB_LINE)
        video_fill(offset, 0, MINI_BYTES);
    mini_frame(0);
    for (r = 0; r <= SIZE; r++)                             /* a dot at every cell corner */
        for (c = 0; c <= SIZE; c++)
            plot(MINI_X - 1 + c * MINI_W, MINI_TOP - 1 + r * MINI_H);
    for (s = 0; s < NSHIPS; s++) {
        cell = f->ship[s].cell;
        if (cell == NO_SHIP)
            continue;
        index = s * 2 + f->ship[s].vertical;
        video_shift_or(mini_ship_data + mini_ship_offset[index]
                       + (f->ship[s].hits == ship_len[s] ? (mini_ship_wh[index] & 0xFF) * (mini_ship_wh[index] >> 8) : 0),
                       MINI_X + COL(cell) * MINI_W, MINI_TOP + ROW(cell) * MINI_H, mini_ship_wh[index]);
    }
    for (cell = 0; cell < CELLS; cell++) {
        look = mini_look(cell);
        mini_shown[cell] = look;
        x = MINI_X + COL(cell) * MINI_W;
        r = MINI_TOP + ROW(cell) * MINI_H;
        if ((look & (M_HIT | M_SUNK)) == M_HIT)
            video_shift_xor(mini_hit, x + 4, r + 2, WH(1, 3));
        if (look & M_MISS)
            video_shift_or(mini_miss, x + 4, r + 2, WH(1, 3));
        if (look & M_AIM)
            mini_box(x, r);
    }
}

/* Recomposes the mini map if any cell changed and sends the bounding box of
 * the changed cells. */
static void sync_mini(void)
{
    unsigned char cell, c0 = SIZE, c1 = 0, r0 = SIZE, r1 = 0;
    unsigned int x0, x1;
    if (view != VIEW_BATTLE)
        return;
    for (cell = 0; cell < CELLS; cell++) {
        if (mini_look(cell) == mini_shown[cell])
            continue;
        if (COL(cell) < c0) c0 = COL(cell);
        if (COL(cell) > c1) c1 = COL(cell);
        if (ROW(cell) < r0) r0 = ROW(cell);
        if (ROW(cell) > r1) r1 = ROW(cell);
    }
    if (c0 == SIZE)
        return;
    compose_mini();
    x0 = MINI_X + c0 * MINI_W;
    x1 = MINI_X + c1 * MINI_W + MINI_W - 1;
    video_flush_rect(COLROW(x0 >> 3, MINI_TOP + r0 * MINI_H),
                     WH((x1 >> 3) - (x0 >> 3) + 1, (r1 - r0 + 1) * MINI_H));
}

/* --- whole picture ---------------------------------------------------------------- */

void screen_compose(void)
{
    unsigned char cell;
    for (cell = 0; cell < CELLS; cell++)
        mini_shown[cell] = M_NONE;
    nvectors = 0;
    video_clear();
    compose_big();
    if (view == VIEW_BATTLE) {
        mini_frame(1);
        compose_mini();
    }
}

/* Uploads per line the runs of bytes that differ from the vector pattern
 * (runs closer than a header's 7 bytes are joined). The pattern is turned
 * into the difference first: comparing the two arrays directly in the
 * scan loop was miscompiled by the peephole optimiser. */
void screen_flush(void)
{
    unsigned char i, line, first, last, x;
    const unsigned char *row;
    for (i = 0; i < nvectors; i++)
        send_vector(&vectors[i]);
    for (line = 0; line < FB_LINES; line++) {
        vector_pattern(line);
        row = framebuffer + line * FB_LINE;
        for (x = 0; x < FB_LINE; x++)
            pattern[x] ^= row[x];
        x = 0;
        while (x < FB_LINE) {
            while (x < FB_LINE && pattern[x] == 0)
                x++;
            if (x == FB_LINE)
                break;
            first = last = x;
            while (x < FB_LINE) {
                if (pattern[x] != 0)
                    last = x;
                else if (x - last >= 7)
                    break;
                x++;
            }
            video_flush_rect(COLROW(first, line), WH(last - first + 1, 1));
        }
    }
}

void screen_sync(void)
{
    sync_big();
    sync_mini();
}

/* --- animations ------------------------------------------------------------------------- */

static void frame(unsigned char cell, unsigned int look, unsigned char ticks)
{
    anim_cell = cell;
    anim_look = look;
    sync_big();
    clock_wait(ticks);
}

void screen_shot(unsigned char cell, unsigned char result, unsigned char ship)
{
    unsigned char k;
    frame(cell, (shown[cell] & ~L_CURSOR) | L_AIM, 10);
    frame(cell, result == RES_MISS ? T_SPLASH : T_BOOM, 12);
    anim_cell = NO_CURSOR;
    if (result == RES_SUNK) {
        for (k = 0; k < 2; k++) {
            flash_ship = ship;
            sync_big();
            clock_wait(WAIT_FRAME);
            flash_ship = NO_SHIP;
            sync_big();
            clock_wait(WAIT_FRAME);
        }
    }
    sync_big();
}

static void mini_frame_aim(unsigned char cell, unsigned char ticks)
{
    mini_aim = cell;
    sync_mini();
    clock_wait(ticks);
}

void screen_cpu_shot(unsigned char cell)
{
    mini_frame_aim(cell, 12);
    mini_frame_aim(NO_CURSOR, 6);
    mini_frame_aim(cell, 12);
    mini_aim = NO_CURSOR;                    /* the result's sync removes the box */
}
