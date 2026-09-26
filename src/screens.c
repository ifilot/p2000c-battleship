/* SPDX-License-Identifier: GPL-3.0-only */
/* screens.c -- the text-mode screens and the title picture.
 *
 * The start and help screens use the plain 80x24 text mode (instant); the
 * title picture is a 512x252 bitmap kept run-length encoded in the binary
 * (splash.h), unpacked into the framebuffer and uploaded sparsely.
 */
#include "video.h"
#include "fleet.h"
#include "cpu.h"
#include "game.h"
#include "screens.h"
#include "saver.h"
#include "splash.h"
#include "version.h"

/* Character-ROM glyphs used by the text-mode screens. */
#define CH_BLOCK 0x9F                       /* full 8x12 block */
#define CH_H     0xD0                       /* box drawing: single lines */
#define CH_V     0xFA
#define CH_TL    0xA9
#define CH_TR    0xB9
#define CH_BL    0xAA
#define CH_BR    0xBA
#define ESC      27

/* --- help ------------------------------------------------------------------------ */

static const char *const HELP[] = {
    "ZEESLAG v" VERSION " voor de Philips P2000C" "                   gecompileerd " BUILD_DATE,
    REPO_URL,
    "",
    "SPELREGELS",
    "  U en de P2000C hebben elk een vloot van vijf schepen op een zee van 10 x 10",
    "  vakken: een vliegdekschip (5 vakken), een slagschip (4), een kruiser (3), een",
    "  onderzeeboot (3) en een torpedojager (2). Schepen liggen recht, horizontaal",
    "  of verticaal, en mogen elkaar niet raken, ook niet schuin.",
    "  Om de beurt vuren u en de P2000C een schot af op een vak van de ander: mis,",
    "  raak, of gezonken als alle vakken van een schip geraakt zijn. Wie als eerste",
    "  de hele vloot van de ander tot zinken brengt, wint.",
    "",
    "TOETSEN",
    "  Pijltjes of W A S D   verplaatsen          RETURN of spatie   plaatsen, vuren",
    "  R   schip draaien     Z   willekeurige vloot     U   schip terugnemen",
    "  H   dit hulpscherm    N   nieuw spel             Q   stoppen, na bevestiging",
    "",
    "NIVEAUS",
    "  1  licht     de P2000C schiet lukraak",
    "  2  normaal   na een treffer maakt de P2000C het schip af",
    "  3  zwaar     de P2000C berekent waar uw schepen het waarschijnlijkst liggen",
    "",
    "Druk op een toets om terug te keren.",
};

/* Clears the 80x24 text screen and hides the blinking cursor. */
void text_clear(void)
{
    con_at(ROWCOL(0, 0));
    conout(ESC); conout('k');
    conout(ESC); conout('c');
}

/* Prints the rules on the (already selected) text screen. */
static void draw_help_page(void)
{
    unsigned char row;
    text_clear();
    for (row = 0; row < sizeof HELP / sizeof HELP[0]; row++) {
        con_at(ROWCOL(row, 0));
        con_puts(HELP[row]);
    }
}

static void help_page(void)
{
    draw_help_page();
    wait_key(draw_help_page);
}

/* Shows the rules from the game, then restores the game screen. Leaving
 * graphics mode clears the terminal's picture, but the framebuffer in RAM
 * is intact, so the return costs one upload. */
void help_screen(void)
{
    video_text();
    help_page();
    redraw_game_screen();
}

/* --- title picture ----------------------------------------------------------- */

/* Unpacks the run-length encoded title bitmap into the framebuffer. */
static void unpack_splash(void)
{
    const unsigned char *in = splash_rle;
    unsigned char *out = framebuffer;
    unsigned int left = SPLASH_RLE_SIZE;
    unsigned char n;
    while (left) {
        n = in[0];
        while (n--)
            *out++ = in[1];
        in += 2;
        left -= 2;
    }
}

/* Uploads only the lit parts of the framebuffer: one ESC r per run of
 * non-zero bytes in a line (short gaps are bridged, a header costs 7 bytes). */
static void flush_lit(void)
{
    unsigned char line, first, last, x;
    const unsigned char *row;
    for (line = 0; line < FB_LINES; line++) {
        row = framebuffer + line * FB_LINE;
        x = 0;
        while (x < FB_LINE) {
            while (x < FB_LINE && row[x] == 0)
                x++;
            if (x == FB_LINE)
                break;
            first = last = x;
            while (x < FB_LINE) {
                if (row[x] != 0)
                    last = x;
                else if (x - last >= 7)
                    break;
                x++;
            }
            video_flush_rect(COLROW(first, line), WH(last - first + 1, 1));
        }
    }
}

static void draw_splash(void)
{
    unpack_splash();
    video_graphics();
    flush_lit();
}

/* Title picture in graphics mode; returns after any key. */
void splash_screen(void)
{
    draw_splash();
    wait_key(draw_splash);
    rng_stir();
    video_text();
}

/* --- start screen -------------------------------------------------------------- */

/* ZEESLAG in a five-row block font, 34 pixels wide; every pixel becomes two
 * block characters, which is close to square on the CRT. */
static const char *const TITLE[5] = {
    "#### #### ####  ### #     ##   ###",
    "   # #    #    #    #    #  # #   ",
    "  #  ###  ###   ##  #    #### # ##",
    " #   #    #       # #    #  # #  #",
    "#### #### #### ###  #### #  #  ###",
};

static void put_repeat(unsigned char ch, unsigned char count)
{
    while (count--)
        conout(ch);
}

/* A single-line box from (row, col), width and height in cells. */
static void draw_box(unsigned char row, unsigned char col, unsigned char width, unsigned char height)
{
    unsigned char r;
    con_at(ROWCOL(row, col));
    conout(CH_TL); put_repeat(CH_H, width - 2); conout(CH_TR);
    for (r = row + 1; r < row + height - 1; r++) {
        con_at(ROWCOL(r, col)); conout(CH_V);
        con_at(ROWCOL(r, col + width - 1)); conout(CH_V);
    }
    con_at(ROWCOL(row + height - 1, col));
    conout(CH_BL); put_repeat(CH_H, width - 2); conout(CH_BR);
}

static void draw_start_screen(void)
{
    unsigned char row;
    const char *pixel;

    text_clear();
    draw_box(0, 0, 80, 23);                  /* row 23 stays empty: writing its last cell scrolls */
    for (row = 0; row < 5; row++) {
        con_at(ROWCOL(2 + row, 5));
        for (pixel = TITLE[row]; *pixel; pixel++) {
            conout(*pixel == '#' ? CH_BLOCK : ' ');
            conout(*pixel == '#' ? CH_BLOCK : ' ');
        }
    }
    con_at(ROWCOL(8, 29));  con_puts("voor de Philips P2000C");
    con_at(ROWCOL(9, 11));  con_puts("versie " VERSION "   -   " REPO_URL);

    draw_box(11, 15, 50, 7);
    con_at(ROWCOL(12, 19)); con_puts("Kies de sterkte van de computer:");
    con_at(ROWCOL(14, 19)); con_puts("1  Licht     schiet lukraak");
    con_at(ROWCOL(15, 19)); con_puts("2  Normaal   maakt geraakte schepen af");
    con_at(ROWCOL(16, 19)); con_puts("3  Zwaar     berekent de kansen");

    con_at(ROWCOL(19, 5));  con_puts("U plaatst uw vloot, daarna vuren u en de P2000C om de beurt. U begint.");
    con_at(ROWCOL(21, 13)); con_puts("1, 2 of 3: spelen    H: spelregels    Q: terug naar CP/M");
}

/* Text-mode start screen; returns the chosen level, or 0 to leave the program. */
unsigned char start_screen(void)
{
    unsigned char key;
    draw_start_screen();
    for (;;) {
        key = wait_key(draw_start_screen);
        rng_stir();
        if (key >= '1' && key <= '3')
            return key - '0';
        if (key == 'q' || key == 'Q')
            return 0;
        if (key == 'h' || key == 'H') {
            help_page();
            draw_start_screen();
        }
    }
}
