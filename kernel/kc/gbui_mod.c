/* GBUI - the paged dialog renderer (#142, step 1b).
 *
 * The kernel's GB_UI call pages this module into PAGE_DATA at #6000 and CALLs it. It
 * reads a marshalled request from the low-RAM UI block (filled by the app-side stubs in
 * gbui_stub.c), renders the dialog by calling the SHARED dialog code (gbdlg/gbprompt/
 * gbpick, compiled in here), and writes the result back to the UI block. Living here -
 * loaded once into the kernel's data page rather than linked into every app - is what
 * frees the ~800 bytes/app that let the data-heavy apps (paint/xaos/iconed) fit.
 *
 * The render uses resident kernel calls (gb_poll/fill/text/dir1 via gblib, which the
 * module links) and the font (in PAGE_DATA alongside us). The app's bank is swapped out
 * while we run, so the request carries everything we need; UI_MODAL (set by the kernel)
 * keeps gb_poll from dispatching top-bar clicks into the absent app.
 */
#include "gb.h"

#define UI_OP    (*(volatile unsigned char *)0x1700)
#define UI_COL   (*(volatile unsigned char *)0x1701)
#define UI_LINE  (*(volatile unsigned char *)0x1702)
#define UI_N     (*(volatile unsigned char *)0x1703)
#define UI_RES   (*(volatile unsigned char *)0x1704)
#define UI_NAME  ((char *)0x1708)            /* OUT: pickfile/prompt result (16 bytes) */
#define UI_TEXT  ((char *)0x1718)            /* IN: packed labels / caption / exts      */

#define UI_OP_POPUP    1
#define UI_OP_PROMPT   2
#define UI_OP_PICKFILE 3
#define UI_OP_PICKDIR  4
#define UI_OP_BROWSER  5
#define UI_OP_BSAVE_AS 8
#define UI_OP_ABOUT    24
#define UI_OP_SIZE     25

#define UI_WIDTH  (*(volatile unsigned int *)0x1708)
#define UI_HEIGHT (*(volatile unsigned int *)0x170A)
#define KCFG_MEMSTR ((const char *)0x121A)

#define ABOUT_W 60
#define ABOUT_H 62

#ifndef GB_VERSION
#define GB_VERSION "unknown"
#endif
#ifndef GB_GIT
#define GB_GIT "unknown"
#endif

/* Browser's low-RAM transfer block. The non-visual source/config operations
 * live in GBWEB.MOD; this dialog module only needs their status and proxy text. */
#define BUI_NPAGES     (*(volatile unsigned char *)0x3904)
#define BUI_FLAGS      (*(volatile unsigned char *)0x3909)
#define BUI_PROXY      ((char *)0x3920)
#define BUI_SAVE_NAME  ((char *)0x39C2)
#define BUI_PROXY_MAX  95
#define BUI_SOURCE_FULL 0x01

#define BUI_ACT_NONE   0
#define BUI_ACT_LOAD   1
#define BUI_ACT_SAVE   2
#define BUI_ACT_PROXY  3
#define BUI_ACT_SAVETO 4

static const char *const browser_file[] = { "Load", "Save" };
static const char *const browser_settings[] = { "Proxy...", "Direct" };
static const char *const html_exts[] = { "HTM", 0 };
static const char about_title[] = "GEOBENCH (C) salvogendut 2026";
static const char about_build[] = "Version : " GB_VERSION " Git: " GB_GIT;
static const char *const about_buttons[] = { "  OK  " };

#define SIZE_X 21
#define SIZE_Y 70
#define SIZE_W 38
#define SIZE_H 50
#define SIZE_FY (SIZE_Y + 18)
#define SIZE_FH 15
#define SIZE_FW 8
#define SIZE_WX (SIZE_X + 3)
#define SIZE_HX (SIZE_X + 22)
#define SIZE_OKX (SIZE_X + 15)
#define SIZE_OKY (SIZE_Y + 37)

static void size_digits(char *text, unsigned int value)
{
    unsigned char digit;
    if (value > 999) value = 999;
    digit = 0;
    while (value >= 100) { value -= 100; digit++; }
    text[0] = (char)('0' + digit);
    digit = 0;
    while (value >= 10) { value -= 10; digit++; }
    text[1] = (char)('0' + digit);
    text[2] = (char)('0' + (unsigned char)value);
    text[3] = 0;
}

static unsigned int size_value(const char *text)
{
    unsigned int value = 0;
    while (*text) {
        value = (unsigned int)(value * 10U + (unsigned char)(*text - '0'));
        text++;
    }
    return value;
}

static void size_field(unsigned char x, const char *text, unsigned char focused)
{
    gb_fill(x, SIZE_FY, SIZE_FW, SIZE_FH, 1);
    gb_frame(x, SIZE_FY, SIZE_FW, SIZE_FH, focused ? 3 : 2);
    gb_textbw((unsigned char)(x + 1), (unsigned char)(SIZE_FY + 3), text);
}

static void size_draw(char *text, unsigned char focused)
{
    gb_fill(SIZE_X, SIZE_Y, SIZE_W, SIZE_H, 1);
    gb_frame(SIZE_X, SIZE_Y, SIZE_W, SIZE_H, 2);
    gb_textbw((unsigned char)(SIZE_X + 2), (unsigned char)(SIZE_Y + 4),
              "New picture");
    size_field(SIZE_WX, text, (unsigned char)(focused == 0));
    gb_textbw((unsigned char)(SIZE_X + 16), (unsigned char)(SIZE_FY + 3), "by");
    size_field(SIZE_HX, text + 4, focused);
    gb_fill(SIZE_OKX, SIZE_OKY, 8, 10, 1);
    gb_frame(SIZE_OKX, SIZE_OKY, 8, 10, 2);
    gb_textbw((unsigned char)(SIZE_OKX + 2), (unsigned char)(SIZE_OKY + 1), "OK");
}

static void size_dialog(void)
{
    char text[8];
    unsigned char length[2] = { 3, 3 };
    unsigned char focused = 0, replace = 1, done = 0, accepted = 0;
    unsigned char flags = 0, key, *len;
    char *field;

    size_digits(text, UI_WIDTH);
    size_digits(text + 4, UI_HEIGHT);
    while (gb_poll() & GB_FIRE) while (gb_getkey()) ;
    while (gb_getkey()) ;
    gb_curhide(); size_draw(text, focused); gb_curshow();

    while (!done) {
        flags = gb_poll();
        if (flags & GB_QUIT) break;
        if (flags & GB_CLICK) {
            if (gb_my() >= SIZE_FY && gb_my() < SIZE_FY + SIZE_FH) {
                if (gb_mx() >= SIZE_WX && gb_mx() < SIZE_WX + SIZE_FW)
                    focused = 0;
                else if (gb_mx() >= SIZE_HX && gb_mx() < SIZE_HX + SIZE_FW)
                    focused = 1;
                else continue;
                replace = 1;
                gb_curhide(); size_draw(text, focused); gb_curshow();
            } else if (gb_mx() >= SIZE_OKX && gb_mx() < SIZE_OKX + 8 &&
                       gb_my() >= SIZE_OKY && gb_my() < SIZE_OKY + 10) {
                accepted = (unsigned char)(length[0] && length[1]);
                done = accepted;
            }
        }
        while ((key = gb_getkey()) != 0) {
            if (key == 9) {
                focused ^= 1;
                replace = 1;
            } else if (key == 13) {
                if (!focused) { focused = 1; replace = 1; }
                else if (length[0] && length[1]) { accepted = 1; done = 1; }
            } else {
                field = text + (focused ? 4 : 0);
                len = length + focused;
                if ((key == 8 || key == 0x7F) && *len) {
                    replace = 0;
                    field[--*len] = 0;
                } else if (key >= '0' && key <= '9') {
                    if (replace) { replace = 0; *len = 0; field[0] = 0; }
                    if (*len < 3) {
                        field[*len] = (char)key;
                        (*len)++;
                        field[*len] = 0;
                    }
                } else continue;
            }
            gb_curhide(); size_draw(text, focused); gb_curshow();
            if (done) break;
        }
    }
    if (accepted) {
        UI_WIDTH = size_value(text);
        UI_HEIGHT = size_value(text + 4);
    }
    UI_RES = accepted;
    if (flags & GB_QUIT) while (gb_poll() & GB_QUIT) ;
    while (gb_poll() & GB_CLICK) ;
    gb_curhide(); gb_fill(SIZE_X, SIZE_Y, SIZE_W, SIZE_H, 0); gb_curshow();
}

/* about_dialog: a compact modal notice with one real button. UI_COL/UI_LINE are
 * supplied by the target-specific desktop so the shared GBUI.MOD remains centred
 * on the 320-, 360-, and 512-pixel desktops. The desktop repaints after GB_UI
 * returns, so this transient box does not need another save-under allocation. */
static void about_dialog(void)
{
    unsigned char x = UI_COL, y = UI_LINE;

    gb_curhide();
    gb_fill(x, y, ABOUT_W, ABOUT_H, 1);
    gb_frame(x, y, ABOUT_W, ABOUT_H, 2);
    gb_textbw((unsigned char)(x + 3), (unsigned char)(y + 5), about_title);
    gb_textbw((unsigned char)(x + 3), (unsigned char)(y + 17), about_build);
    gb_textbw((unsigned char)(x + 3), (unsigned char)(y + 29), "RAM:");
    gb_textbw((unsigned char)(x + 10), (unsigned char)(y + 29), KCFG_MEMSTR);
    gb_curshow();
    gb_popup((unsigned char)(x + 23), (unsigned char)(y + 42), about_buttons, 1);
}

static void browser_to_83(const char *src)
{
    unsigned char i = 0, j;
    for (j = 0; j < 11; j++) UI_NAME[j] = ' ';
    for (j = 0; j < 8 && src[i] && src[i] != '.'; j++) UI_NAME[j] = src[i++];
    UI_NAME[8] = 'H'; UI_NAME[9] = 'T'; UI_NAME[10] = 'M';
}

static void browser_menu(void)
{
    unsigned char sel;
    UI_RES = BUI_ACT_NONE;
    if (UI_N == 1) {
        sel = gb_popup(10, 8, browser_file, 2);
        if (sel == 0 && gb_pickfile(UI_NAME, html_exts)) UI_RES = BUI_ACT_LOAD;
        else if (sel == 1) UI_RES = BUI_ACT_SAVE;
    } else {
        sel = gb_popup(17, 8, browser_settings, 2);
        if (sel == 0) {
            if (gb_prompt("Proxy host:port:", BUI_PROXY, 0x80 | BUI_PROXY_MAX))
                UI_RES = BUI_ACT_PROXY;
        } else if (sel == 1) {
            BUI_PROXY[0] = 0;
            UI_RES = BUI_ACT_PROXY;
        }
    }
}

static void browser_save_as(void)
{
    char *name = (char *)0x1708;
    unsigned char i;
    UI_RES = BUI_ACT_NONE;
    if (!BUI_NPAGES || (BUI_FLAGS & BUI_SOURCE_FULL)) return;
    if (!gb_pickdir(html_exts)) return;
    if (!gb_prompt("Save HTML as:", name, 12)) return;
    browser_to_83(name);
    for (i = 0; i < 11; i++) BUI_SAVE_NAME[i] = UI_NAME[i];
    UI_RES = BUI_ACT_SAVETO;
}

void main(void)
{
    unsigned char i;
    char *p = UI_TEXT;

    if (UI_OP == UI_OP_POPUP) {
        const char *labels[16];                  /* rebuild the pointer array into UI_TEXT */
        unsigned char n = UI_N;
        if (n > 16) n = 16;
        for (i = 0; i < n; i++) { labels[i] = p; while (*p) p++; p++; }
        UI_RES = gb_popup(UI_COL, UI_LINE, labels, n);

    } else if (UI_OP == UI_OP_PROMPT) {
        char buf[16];
        UI_RES = gb_prompt(UI_TEXT, buf, UI_N);  /* caption in UI_TEXT, result -> buf */
        if (UI_RES) { for (i = 0; i < 16; i++) UI_NAME[i] = buf[i]; }

    } else if (UI_OP == UI_OP_PICKFILE || UI_OP == UI_OP_PICKDIR) {
        const char *exts[8];                     /* rebuild the ext list from UI_TEXT */
        unsigned char ne = 0;
        while (*p && ne < 7) { exts[ne++] = p; while (*p) p++; p++; }
        exts[ne] = 0;
        if (UI_OP == UI_OP_PICKFILE) UI_RES = gb_pickfile(UI_NAME, exts);   /* name -> UI_NAME */
        else                         UI_RES = gb_pickdir(exts);
    } else if (UI_OP == UI_OP_BROWSER) {
        browser_menu();
    } else if (UI_OP == UI_OP_BSAVE_AS) {
        browser_save_as();
    } else if (UI_OP == UI_OP_ABOUT) {
        about_dialog();
    } else if (UI_OP == UI_OP_SIZE) {
        size_dialog();
    }
}
