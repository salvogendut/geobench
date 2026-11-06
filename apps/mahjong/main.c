/*
 * MAHJONG.APP - compact Kana Mahjong solitaire for GEOBENCH.
 *
 * This is an original C implementation for the shared CPC/MSX2/PCW app ABI.
 * It uses a classic 144-position Turtle layout and selectable Katakana and
 * Hiragana faces generated from the source atlases in assets/. Every visible
 * pair has the same Kana. A precomputed legal removal order receives a freshly
 * shuffled face list, so every new deal is solvable.
 *
 * Tiles are canonical Mode-1 bitmaps. The normal white surface is recoloured
 * red when selected and blue for a hint; MSX2/PCW builds translate each tile to
 * native screen bytes immediately before blitting. No kernel changes or direct
 * screen writes are required.
 */
#include "gb.h"
#include "kana.h"

#define NO_TILE       0xFF
#define TILE_W_PIX    16
#define TILE_HALF_Y    9
#define LEVEL_X        1
#define LEVEL_Y        2
#define BOARD_W       60
#define BOARD_H      144
#define CONTENT_Y     24
#define STATUS_Y      27
#define BOARD_AREA_Y  38
#define BOARD_Y       (BOARD_AREA_Y + (GB_LINES - BOARD_AREA_Y - 2 - BOARD_H) / 2)
#define PREVIEW_X     (GB_COLS - 17)
#define PREVIEW_Y     112
#define PREVIEW_W     16
#define PREVIEW_TEXT_Y 188

#define TILE_NORMAL    0
#define TILE_SELECTED  1
#define TILE_HINT      2
#define MATCH_HOLD    10

static unsigned char board_x, board_y;
static unsigned char face[MJ_TILE_COUNT];
static unsigned char active[MJ_TILE_COUNT];
static unsigned char open_tile[MJ_TILE_COUNT];
static unsigned char undo_a[72], undo_b[72];
static unsigned char undo_count;
static unsigned char pair_face[72];
static unsigned char move_first[MJ_FACE_COUNT_MAX];
static unsigned char selected, match_second, match_wait, hint_a, hint_b;
static unsigned char remaining;
static unsigned char tileset;
static unsigned char want_menu;
static unsigned char defer_draw;
static unsigned int rng;
static unsigned char tilebuf[MJ_TILE_BYTES];
static char count_text[4];
static const char *status_text;

static const unsigned char menu_def[] = {
    2,
    10, 'G','a','m','e',0,0,0,0,
    17, 'T','i','l','e','s',0,0,0
};
static const char *const game_items[] = {
    "New Game", "Undo", "Hint", "Quit"
};
static const char *const tile_items[] = {
    "Katakana", "Hiragana"
};

static unsigned int rnd(void)
{
    rng = (unsigned int)(rng * 25173u + 13849u);
    return rng;
}
#define rr(n) ((unsigned char)(((rnd() >> 8) * (unsigned int)(n)) >> 8))

static unsigned char rows_overlap(unsigned char a, unsigned char b)
{
    return (unsigned char)(a < (unsigned char)(b + 2) && b < (unsigned char)(a + 2));
}

static unsigned char tile_is_open(unsigned char i)
{
    unsigned char j, left = 0, right = 0, covered = 0;
    unsigned char row = mj_row[i], col = mj_col[i], level = mj_level[i];
    if (!active[i]) return 0;
    for (j = 0; j < MJ_TILE_COUNT; j++) {
        unsigned char orow, ocol, olevel;
        if (j == i || !active[j]) continue;
        orow = mj_row[j];
        if (!rows_overlap(row, orow)) continue;
        ocol = mj_col[j]; olevel = mj_level[j];
        if (olevel > level && col < (unsigned char)(ocol + 2) &&
            ocol < (unsigned char)(col + 2)) covered = 1;
        if (olevel == level) {
            if ((unsigned char)(ocol + 2) == col) left = 1;
            if ((unsigned char)(col + 2) == ocol) right = 1;
        }
    }
    return (unsigned char)(!covered && (!left || !right));
}

static void refresh_open(void)
{
    unsigned char i;
    for (i = 0; i < MJ_TILE_COUNT; i++) open_tile[i] = tile_is_open(i);
}

static unsigned char find_move(unsigned char *a, unsigned char *b)
{
    unsigned char i, f;
    for (f = 0; f < mj_face_count[tileset]; f++) move_first[f] = NO_TILE;
    for (i = 0; i < MJ_TILE_COUNT; i++) {
        if (!active[i] || !open_tile[i]) continue;
        f = face[i];
        if (move_first[f] != NO_TILE) {
            *a = move_first[f]; *b = i;
            return 1;
        }
        move_first[f] = i;
    }
    return 0;
}

static unsigned char tile_x(unsigned char i)
{
    return (unsigned char)(board_x + mj_col[i] * 2 + mj_level[i] * LEVEL_X);
}

static unsigned char tile_y(unsigned char i)
{
    return (unsigned char)(board_y + mj_row[i] * TILE_HALF_Y - mj_level[i] * LEVEL_Y);
}

static unsigned char state_byte(unsigned char value, unsigned char state)
{
    if (state == TILE_SELECTED) return (unsigned char)(value | (value >> 4));
    if (state == TILE_HINT) return (unsigned char)(value & 0x0F);
    return value;
}

#if defined(GB_MSX2) || defined(GB_PCW)
static unsigned char native_byte(unsigned char byte)
{
    unsigned char pixel, pen, out = 0;
    for (pixel = 0; pixel < 4; pixel++) {
        pen = (unsigned char)(((byte >> (7 - pixel)) & 1) |
              (((byte >> (3 - pixel)) & 1) << 1));
        out |= (unsigned char)(pen << (6 - 2 * pixel));
    }
#ifdef GB_PCW
    out = (unsigned char)(((out & 0x55) << 1) | (((out ^ 0xFF) & 0xAA) >> 1));
#endif
    return out;
}
#endif

static void draw_tile(unsigned char i)
{
    const unsigned char *src;
    unsigned int offset;
    unsigned char state, k;
    if (!active[i]) return;
    state = (selected == i || match_second == i) ? TILE_SELECTED :
            ((hint_a == i || hint_b == i) ? TILE_HINT : TILE_NORMAL);
    offset = mj_art_offset[tileset] + (unsigned int)face[i] * MJ_TILE_BYTES;
    src = mj_tile_art + offset;
#if defined(GB_MSX2) || defined(GB_PCW)
    for (k = 0; k < MJ_TILE_BYTES; k++)
        tilebuf[k] = native_byte(state_byte(src[k], state));
    gb_restorerect(tile_x(i), tile_y(i), MJ_TILE_WB, MJ_TILE_H, tilebuf);
#else
    if (state == TILE_NORMAL) {
        gb_restorerect(tile_x(i), tile_y(i), MJ_TILE_WB, MJ_TILE_H, src);
    } else {
        for (k = 0; k < MJ_TILE_BYTES; k++) tilebuf[k] = state_byte(src[k], state);
        gb_restorerect(tile_x(i), tile_y(i), MJ_TILE_WB, MJ_TILE_H, tilebuf);
    }
#endif
}

static unsigned char source_pen(const unsigned char *src,
                                unsigned char x, unsigned char y)
{
    unsigned char value = src[(unsigned int)y * MJ_TILE_WB + (x >> 2)];
    unsigned char pixel = (unsigned char)(x & 3);
    return (unsigned char)(((value >> (7 - pixel)) & 1) |
           (((value >> (3 - pixel)) & 1) << 1));
}

static void draw_preview(void)
{
    const unsigned char *src;
    const char *label;
    unsigned int offset;
    unsigned char i, x, y, start, pen, next, len;

    gb_fill(PREVIEW_X, (unsigned char)(PREVIEW_Y - 4),
            (unsigned char)(GB_COLS - PREVIEW_X),
            (unsigned char)(GB_LINES - PREVIEW_Y + 4), 0);
    i = selected != NO_TILE ? selected : match_second;
    if (i == NO_TILE) return;

    offset = mj_art_offset[tileset] + (unsigned int)face[i] * MJ_TILE_BYTES;
    src = mj_tile_art + offset;
    for (y = 0; y < MJ_TILE_H; y++) {
        x = 0;
        while (x < 16) {
            start = x;
            pen = source_pen(src, x, y);
            do {
                x++;
                if (x == 16) break;
                next = source_pen(src, x, y);
            } while (next == pen);
            gb_fill((unsigned char)(PREVIEW_X + start),
                    (unsigned char)(PREVIEW_Y + y * 4),
                    (unsigned char)(x - start), 4, pen);
        }
    }

    label = mj_pronunciation[face[i]];
    len = 0;
    while (label[len]) len++;
    gb_text((unsigned char)(PREVIEW_X + (PREVIEW_W - len) / 2),
            PREVIEW_TEXT_Y, label);
}

static void format_count(void)
{
    unsigned char n = remaining;
    if (n >= 100) {
        count_text[0] = (char)('0' + n / 100);
        n %= 100;
        count_text[1] = (char)('0' + n / 10);
        count_text[2] = (char)('0' + n % 10);
        count_text[3] = 0;
    } else if (n >= 10) {
        count_text[0] = (char)('0' + n / 10);
        count_text[1] = (char)('0' + n % 10);
        count_text[2] = 0;
    } else {
        count_text[0] = (char)('0' + n);
        count_text[1] = 0;
    }
}

static void draw_status(void)
{
    gb_fill(1, CONTENT_Y, (unsigned char)(GB_COLS - 2), 12, 0);
    gb_text(2, STATUS_Y, status_text);
    format_count();
    gb_text((unsigned char)(GB_COLS - 14), STATUS_Y, "Tiles:");
    gb_text((unsigned char)(GB_COLS - 6), STATUS_Y, count_text);
}

static void draw_board(void)
{
    unsigned char i;
    gb_fill(board_x, board_y, BOARD_W, BOARD_H, 0);
    for (i = 0; i < MJ_TILE_COUNT; i++) draw_tile(i);
}

static void redraw_hole(unsigned char hole)
{
    unsigned char i, k;
    unsigned char hx = tile_x(hole), hy = tile_y(hole);

    for (k = 0; k < MJ_TILE_BYTES; k++) tilebuf[k] = 0;
    for (i = 0; i < MJ_TILE_COUNT; i++) {
        const unsigned char *src;
        unsigned char tx, ty, left, right, top, bottom, x, y;
        unsigned int offset;

        if (!active[i]) continue;
        tx = tile_x(i); ty = tile_y(i);
        if (tx >= (unsigned char)(hx + MJ_TILE_WB) ||
            hx >= (unsigned char)(tx + MJ_TILE_WB) ||
            ty >= (unsigned char)(hy + MJ_TILE_H) ||
            hy >= (unsigned char)(ty + MJ_TILE_H)) continue;

        left = tx > hx ? tx : hx;
        right = (unsigned char)(tx + MJ_TILE_WB);
        if (right > (unsigned char)(hx + MJ_TILE_WB))
            right = (unsigned char)(hx + MJ_TILE_WB);
        top = ty > hy ? ty : hy;
        bottom = (unsigned char)(ty + MJ_TILE_H);
        if (bottom > (unsigned char)(hy + MJ_TILE_H))
            bottom = (unsigned char)(hy + MJ_TILE_H);

        offset = mj_art_offset[tileset] +
                 (unsigned int)face[i] * MJ_TILE_BYTES;
        src = mj_tile_art + offset;
        for (y = top; y < bottom; y++) {
            for (x = left; x < right; x++) {
                tilebuf[(unsigned char)((y - hy) * MJ_TILE_WB + x - hx)] =
                    src[(unsigned char)((y - ty) * MJ_TILE_WB + x - tx)];
            }
        }
    }
#if defined(GB_MSX2) || defined(GB_PCW)
    for (k = 0; k < MJ_TILE_BYTES; k++) tilebuf[k] = native_byte(tilebuf[k]);
#endif
    gb_restorerect(hx, hy, MJ_TILE_WB, MJ_TILE_H, tilebuf);
}

static void redraw_removed_pair(unsigned char a, unsigned char b)
{
    /* #439: compose each hole without touching adjacent tile pixels. */
    redraw_hole(a);
    redraw_hole(b);
}

static void draw_all(void)
{
    gb_curhide();
    gb_fill(0, CONTENT_Y, GB_COLS, (unsigned char)(GB_LINES - CONTENT_Y), 0);
    draw_status();
    draw_board();
    draw_preview();
    gb_curshow();
}

static void redraw_game(void)
{
    gb_curhide();
    draw_status();
    draw_board();
    draw_preview();
    gb_curshow();
}

static void redraw_status(void)
{
    gb_curhide(); draw_status(); gb_curshow();
}

static void new_game(void)
{
    unsigned char i = 0, p, j, tmp;
    for (p = 0; p < 72; p++) {
        pair_face[p] = i;
        if (++i == mj_face_count[tileset]) i = 0;
    }
    for (i = 71; i; i--) {
        j = rr((unsigned char)(i + 1));
        tmp = pair_face[i]; pair_face[i] = pair_face[j]; pair_face[j] = tmp;
    }
    for (p = 0; p < 72; p++) {
        face[mj_solution[(unsigned char)(p * 2)]] = pair_face[p];
        face[mj_solution[(unsigned char)(p * 2 + 1)]] = pair_face[p];
    }
    for (i = 0; i < MJ_TILE_COUNT; i++) active[i] = 1;
    selected = match_second = hint_a = hint_b = NO_TILE;
    match_wait = 0;
    undo_count = 0; remaining = MJ_TILE_COUNT;
    refresh_open();
    status_text = "New game";
    if (!defer_draw) redraw_game();
}

static void undo_move(void)
{
    unsigned char a, b;
    if (!undo_count) {
        status_text = "Nothing to undo";
        if (!defer_draw) redraw_status();
        return;
    }
    selected = match_second = hint_a = hint_b = NO_TILE;
    match_wait = 0;
    undo_count--;
    a = undo_a[undo_count]; b = undo_b[undo_count];
    active[a] = active[b] = 1;
    remaining = (unsigned char)(remaining + 2);
    refresh_open();
    status_text = "Move restored";
    if (!defer_draw) redraw_game();
}

static void show_hint(void)
{
    unsigned char old, a, b;
    old = selected; selected = NO_TILE;
    hint_a = hint_b = NO_TILE;
    if (find_move(&a, &b)) {
        hint_a = a; hint_b = b;
        status_text = "Matching pair";
    } else {
        status_text = remaining ? "No moves available" : "Board cleared";
    }
    if (defer_draw) return;
    gb_curhide();
    if (old != NO_TILE) draw_tile(old);
    if (hint_a != NO_TILE) { draw_tile(hint_a); draw_tile(hint_b); }
    draw_preview();
    draw_status();
    gb_curshow();
}

static unsigned char hit_tile(void)
{
    unsigned char i = MJ_TILE_COUNT, x;
    unsigned int mx = gb_mxp();
    unsigned char my = gb_my(), y;
    while (i) {
        i--;
        if (!active[i]) continue;
        x = tile_x(i); y = tile_y(i);
        if (mx >= (unsigned int)x * 4 && mx < (unsigned int)x * 4 + TILE_W_PIX &&
            my >= y && my < (unsigned char)(y + MJ_TILE_H)) return i;
    }
    return NO_TILE;
}

static void clear_hint_tiles(void)
{
    unsigned char a = hint_a, b = hint_b;
    hint_a = hint_b = NO_TILE;
    if (a != NO_TILE && active[a]) draw_tile(a);
    if (b != NO_TILE && b != a && active[b]) draw_tile(b);
}

static void finish_match(void)
{
    unsigned char a = selected, b = match_second;

    active[a] = active[b] = 0;
    undo_a[undo_count] = a; undo_b[undo_count] = b; undo_count++;
    remaining = (unsigned char)(remaining - 2);
    selected = match_second = NO_TILE;
    refresh_open();
    if (!remaining) status_text = "Board cleared";
    else {
        unsigned char next_a, next_b;
        status_text = find_move(&next_a, &next_b) ?
                      "Pair removed" : "No moves available";
    }
    gb_curhide();
    draw_status();
    draw_preview();
    redraw_removed_pair(a, b);
    gb_curshow();
}

static void click_board(void)
{
    unsigned char i = hit_tile(), old;
    if (i == NO_TILE || match_wait) return;
    gb_curhide();
    clear_hint_tiles();
    if (!open_tile[i]) {
        status_text = "Tile is blocked";
        draw_status(); gb_curshow(); return;
    }
    old = selected;
    if (old == i) {
        selected = NO_TILE;
        draw_tile(i);
        status_text = "Selection cleared";
    } else if (old == NO_TILE || face[old] != face[i]) {
        selected = i;
        if (old != NO_TILE) draw_tile(old);
        draw_tile(i);
        status_text = old == NO_TILE ? "Tile selected" : "Selection moved";
    } else {
        match_second = i;
        match_wait = MATCH_HOLD;
        draw_tile(i);
        status_text = "Pair matched";
        draw_preview();
        draw_status(); gb_curshow(); return;
    }
    draw_preview();
    draw_status();
    gb_curshow();
}

static unsigned char run_menu(void)
{
    unsigned char choice, menu;
    if (!want_menu) return 0;
    menu = want_menu;
    want_menu = 0;
    if (menu == 1) choice = gb_popup(10, 8, game_items, 4);
    else choice = gb_popup(17, 8, tile_items, MJ_TILESET_COUNT);
    defer_draw = 1;
    if (menu == 1) {
        if (choice == 0) new_game();
        else if (choice == 1) undo_move();
        else if (choice == 2) show_hint();
        else if (choice == 3) { defer_draw = 0; gb_wm_close(); return 1; }
    } else if (choice < MJ_TILESET_COUNT) {
        tileset = choice;
        new_game();
        status_text = tileset ? "Hiragana tiles" : "Katakana tiles";
    }
    defer_draw = 0;
    if (choice != 0xFF) gb_restore_parent();
    return 0;
}

static void handle_keys(void)
{
    unsigned char key;
    while ((key = gb_getkey()) != 0) {
        if (key == 'n' || key == 'N') { new_game(); return; }
        if (key == 'u' || key == 'U') { undo_move(); return; }
        if (key == 'h' || key == 'H') { show_hint(); return; }
        if (key == 'q' || key == 'Q') { gb_wm_close(); return; }
    }
}

static void mahjong_proc(void)
{
    unsigned char type = gb_msg.type;
    if (type == GB_MSG_DRAW) draw_all();
    else if (type == GB_MSG_CLICK) click_board();
    else if (type == GB_MSG_FRAME) {
        if (match_wait) {
            if (!--match_wait) finish_match();
        } else if (!run_menu()) handle_keys();
    } else if (type == GB_MSG_MENU) {
        if (!gb_modal()) {
            if (gb_msg.p0 >= 10 && gb_msg.p0 < 17) want_menu = 1;
            else if (gb_msg.p0 >= 17 && gb_msg.p0 < 25) want_menu = 2;
        }
    } else if (type == GB_MSG_CLOSE) gb_wm_close();
}

static const gb_mwin_t mahjong_window = {
    0, 8, GB_COLS, (GB_LINES - 8), 0, 0, mahjong_proc, "Kana Mahjong", 0
};

void main(void)
{
    unsigned char n;
    gb_time();
    rng = (unsigned int)((gb_sec << 8) ^ (gb_min << 3) ^ gb_hour ^ 0x4D4Au);
    board_x = (unsigned char)((PREVIEW_X - BOARD_W) / 2);
    board_y = BOARD_Y;
    tileset = 0;
    status_text = "Starting game";
    selected = match_second = hint_a = hint_b = NO_TILE;
    match_wait = 0;
    gb_wm_managed(&mahjong_window);
    gb_menu(menu_def);
    for (n = 64; n; n--) if (!gb_getkey()) break;
    defer_draw = 1;
    new_game();
    defer_draw = 0;
    gb_restore_parent();
}
