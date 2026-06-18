/* desktop - the GEOBENCH desktop, in C (the last app to leave assembly).
 *
 * Boots into PAGE_APP0. Draws the backdrop (below the kernel's top bar) and the
 * Disk / Clock / Trash icons, lets you DRAG icons (hold fire, a red outline
 * follows, release to drop), and opens an icon on double-click: Disk -> the file
 * manager, Clock -> the C demo.
 *
 * Issue #45: the desktop no longer owns a for(;;) loop - it registers a window
 * (gb_wm_run) and the KERNEL drives the master loop, calling on_frame each frame.
 * It is the single, permanent root window; app windows layer on top of it.
 *
 * A press over an icon both arms a double-click and starts a drag; releasing
 * drops the icon at its new spot (no movement => it was just a click). The
 * backdrop is a solid colour, so the drag outline erases by redrawing in the
 * backdrop pen - no save-under needed. */
#include "gb.h"

#define IC_W    8             /* icon width  (byte cols) = 32 px */
#define IC_H    32            /* icon height (lines)             */
#define BOX_H   44            /* icon + label box (hit-test/lift) */
#define XMAX    (80 - IC_W)   /* drag clamps */
#define YMIN    9
#define YMAX    (200 - BOX_H)
#define DCLICK  75            /* double-click window, frames (gamepad-friendly, #153) */
#define NONE    0xFF
#define DRAGTH  2             /* press must move this far before it lifts (#153) */

/* The desktop icons: drives (C = IDE, A/B = floppies) + Clock + Trash (#65). The
   drive icons appear only when that drive is present (gb_drives poll); Clock and
   Trash are always shown. Positions are mutable (drag updates them). */
#define N_ICONS  5
#define IDX_C    0            /* Disk C = IDE */
#define IDX_A    1            /* Disk A = floppy A */
#define IDX_B    2            /* Disk B = floppy B */
#define IDX_CLOCK 3
#define IDX_TRASH 4

#define ICON_IDE 1            /* IST slots (packicons order): Disk C as IDE ... */
#define ICON_SD  19           /* ... vs Albireo SD/USB card (#104) */
static unsigned char ic_x[N_ICONS]     = {  0,  0,  0, 72, 72 };
static unsigned char ic_y[N_ICONS]     = { 35, 80, 125, 35, 160 };
static unsigned char ic_slot[N_ICONS] = { ICON_IDE, 0, 0, 2, 3 };  /* C,flp,flp,clk,trash */
static const char *const ic_lbl[N_ICONS] = { "Disk C","Disk A","Disk B","Clock","Trash" };
static const unsigned char ic_drive[N_ICONS] = { 1, 1, 1, 0, 0 };  /* opens the file mgr */
static unsigned char ic_present[N_ICONS] = { 1, 0, 0, 1, 1 };      /* drives set by poll */

static unsigned char drag_active, drag_idx, out_x, out_y, grab_dx, grab_dy;
static unsigned char drag_armed, arm_mx, arm_my;   /* pressed on an icon, not yet lifted (#153) */
static unsigned char sel_idx = NONE;               /* selected icon (red frame), NONE = none (#153) */
static unsigned char desk_active;                  /* set by on_frame -> the desktop is focused (#153) */
static unsigned char dc_timer, dc_idx, held_prev;
static unsigned char show_ram;               /* System menu footprint toggle (#74) */
static unsigned char menu_inited;            /* gb_doc/System registered on the 1st frame (#142) */

#define DRIVE_TOP  20         /* drive icons stack down the left column, packed */
#define DRIVE_STEP 46         /* (BOX_H 44 + 2 gap), in detection order */

/* drive_poll: probe the drives, show an icon per present one and pack them down
   the left column in detection order (C, A, B) - no gaps for absent drives (#65). */
static void drive_poll(void)
{
    unsigned char d = gb_drives(), i, n = 0;
    ic_present[IDX_C] = (d & GB_DRV_C) ? 1 : 0;
    ic_present[IDX_A] = (d & GB_DRV_A) ? 1 : 0;
    ic_present[IDX_B] = (d & GB_DRV_B) ? 1 : 0;
    ic_slot[IDX_C] = (d & GB_DRV_C_SD) ? ICON_SD : ICON_IDE;  /* Albireo card vs IDE (#104) */
    for (i = 0; i < 3; i++)               /* the three drive icons are indices 0..2 */
        if (ic_present[i]) {
            ic_x[i] = 0;
            ic_y[i] = DRIVE_TOP + n * DRIVE_STEP;
            n++;
        }
}

static void draw_icon(unsigned char i)
{
    gb_icon(ic_slot[i], ic_x[i], ic_y[i]);
    gb_text(ic_x[i], ic_y[i] + 34, ic_lbl[i]);
}

static void paint(void)
{
    unsigned char i;
    gb_fill(0, 8, 80, 192, 0);                 /* backdrop, below the top bar */
    for (i = 0; i < N_ICONS; i++)
        if (ic_present[i]) draw_icon(i);
    if (sel_idx != NONE && ic_present[sel_idx])    /* red selection frame (#153) */
        gb_frame(ic_x[sel_idx], ic_y[sel_idx], IC_W, IC_H, 3);
    /* #153: do NOT show the cursor here. As the WM's bottom on_repaint, paint()
       runs FIRST in wm_repaint_all - any window drawn on top would overwrite the
       pointer while its save-under still held the backdrop, so a later move left a
       blue hole. The repaint bracket now draws the pointer once, LAST. Standalone
       callers (System menu, boot) gb_curshow themselves. */
}

/* select_icon: move the red selection frame to icon (NONE clears it), erasing the
   previous one in place so we never leave a stray frame behind (#153). */
static void select_icon(unsigned char icon)
{
    if (sel_idx == icon) return;
    gb_curhide();
    if (sel_idx != NONE && ic_present[sel_idx]) {
        gb_frame(ic_x[sel_idx], ic_y[sel_idx], IC_W, IC_H, 0);   /* erase old frame */
        draw_icon(sel_idx);                                       /* restore the glyph edge */
    }
    if (icon != NONE)
        gb_frame(ic_x[icon], ic_y[icon], IC_W, IC_H, 3);
    gb_curshow();
    sel_idx = icon;
}

/* The top bar is now drawn here, not in the kernel (experiment #77). The WM runs
   bar_draw() every frame in our page regardless of focus, so the clock + the focused
   window's menu titles stay live behind any window. Nothing else touches lines 0-7
   (windows start at line 8), so each element is drawn once and only repainted when it
   changes (clock minute, menu def). Kernel state we read: KCFG_MEMSTR (RAM size, set
   by the GBCFG module) and MENU_DEF (the focused window's menu, kept by the WM). */
#define KCFG_MEMSTR ((const char *)0x121A)
#define MENU_DEF    ((volatile unsigned char *)0x1310)
#define WM_FS       ((volatile unsigned char *)0x130A)   /* 1 = a window is fullscreen (kernel) */
#define CLK_COL     68            /* clock column (matches the old kernel bar) */

static unsigned char bar_init, bar_min, bar_msig, bar_wasfs;

static unsigned char bin(unsigned char v)   /* raw RTC reg -> binary (gb_time) */
{
    return gb_binmode ? v : (unsigned char)((v >> 4) * 10 + (v & 15));
}
static void put2(char *p, unsigned char v) { p[0] = '0' + v / 10; p[1] = '0' + v % 10; }

/* bar_menu: the focused window's menu titles (MENU_DEF: count, then {col, 8-byte
   label}*count). Clear the title region first so a previous app's titles are gone. */
static void bar_menu(void)
{
    unsigned char n = MENU_DEF[0], i, j;
    char lbl[9];
    gb_curhide();
    gb_fill(8, 0, 46, 8, 1);                  /* white, cols 8..53 (RAM/footprint/clock kept) */
    for (i = 0; i < n && i < 4; i++) {
        for (j = 0; j < 8; j++) lbl[j] = MENU_DEF[2 + i * 9 + j];
        lbl[8] = 0;
        gb_textbw(MENU_DEF[1 + i * 9], 0, lbl);
    }
    gb_curshow();
}

static void bar_clock(void)
{
    char t[6];
    gb_time();
    put2(t, bin(gb_hour)); t[2] = ':'; put2(t + 3, bin(gb_min)); t[5] = 0;
    gb_curhide(); gb_textbw(CLK_COL, 0, t); gb_curshow();
}

static void bar_draw(void)
{
    unsigned char msig, i;
    if (*WM_FS) { bar_wasfs = 1; return; }    /* fullscreen: the borderless window owns lines 0-7 */
    if (bar_wasfs) { bar_wasfs = 0; bar_init = 0; }   /* just exited -> force a full bar redraw */
    if (!bar_init) {                          /* first frame: white strip + RAM size */
        gb_curhide();
        gb_fill(0, 0, 80, 8, 1);
        gb_textbw(1, 0, KCFG_MEMSTR);
        gb_curshow();
        bar_init = 1; bar_min = 0xFF; bar_msig = 0xFF;
    }
    msig = 0;                                  /* menu titles: redraw when MENU_DEF changes */
    for (i = 0; i < (unsigned char)(MENU_DEF[0] * 9 + 1) && i < 40; i++) msig += MENU_DEF[i];
    if (msig != bar_msig) { bar_msig = msig; bar_menu(); }
    gb_time();                                 /* clock: redraw when the minute changes */
    if (bin(gb_min) != bar_min) { bar_min = bin(gb_min); bar_clock(); }
    /* The desktop loses focus when a window is clicked, and its on_frame stops
       running - but this bar hook runs every frame in the desktop's page regardless
       of focus. on_frame (above) sets desk_active when the desktop is focused; if it
       did NOT run this frame, a window has focus, so clear a left-over selection
       (reset the clip first so the erase isn't clipped to some window's rect, #153). */
    if (!desk_active && sel_idx != NONE) {
        gb_wm_damage(0, 0, 80, 200);
        select_icon(NONE);
    }
    desk_active = 0;                            /* consume the flag; on_frame re-sets it if focused */
}

static unsigned char hit_icon(unsigned char mx, unsigned char my)
{
    unsigned char i;
    for (i = 0; i < N_ICONS; i++)
        if (ic_present[i] &&
            mx >= ic_x[i] && mx < ic_x[i] + IC_W &&
            my >= ic_y[i] && my < ic_y[i] + BOX_H)
            return i;
    return NONE;
}

/* dragstart: lift the icon (paint its box in the backdrop) and show a red
   outline at its position; remember where in the icon we grabbed it. */
static void dragstart(unsigned char idx, unsigned char mx, unsigned char my)
{
    drag_idx = idx;
    out_x = ic_x[idx];
    out_y = ic_y[idx];
    grab_dx = mx - out_x;
    grab_dy = my - out_y;
    drag_active = 1;
    gb_curhide();
    gb_fill(out_x, out_y, IC_W + 2, BOX_H, 0); /* erase icon + label */
    gb_frame(out_x, out_y, IC_W, IC_H, 3);     /* red outline */
    gb_curshow();
}

static void dragmove(unsigned char mx, unsigned char my)
{
    unsigned char nx, ny;
    nx = (mx >= grab_dx) ? (unsigned char)(mx - grab_dx) : 0;
    if (nx > XMAX) nx = XMAX;
    ny = (my >= grab_dy) ? (unsigned char)(my - grab_dy) : 0;
    if (ny < YMIN) ny = YMIN;
    if (ny > YMAX) ny = YMAX;
    if (nx == out_x && ny == out_y) return;    /* nothing moved */
    gb_curhide();
    gb_frame(out_x, out_y, IC_W, IC_H, 0);     /* erase old outline */
    out_x = nx;
    out_y = ny;
    gb_frame(out_x, out_y, IC_W, IC_H, 3);     /* draw new outline */
    gb_curshow();
}

static void drop(void)
{
    ic_x[drag_idx] = out_x;                     /* commit the new position */
    ic_y[drag_idx] = out_y;
    drag_active = 0;
    gb_curhide();
    gb_restore_parent();                        /* repaint desktop + restack any windows
                                                   on top, so they stay (one layer up) and
                                                   aren't erased by our backdrop fill (#65) */
}

/* The "System" menu now rides the shared gb_doc menu system (#142): the desktop
   registers an empty document (no File/Edit/View) and adds one "System" title with
   gb_menu_add, so its dropdown renders with the same framed/reverse-video hover look
   as every app's menus - one menu path for the whole UI. */
#define FP_COL    54          /* Ram-Usage footprint column - left of the clock (CLK_COL 68) */
static const gb_doc_t deskdoc = { 0 };        /* no document -> gb_doc adds no titles */

/* draw_footprint: the resident kernel size on the top bar, left of the clock, as
   "<n>K used", black-on-white like the bar. */
static char fp[12];
static void draw_footprint(void)
{
    unsigned int k = (gb_ksize + 512) / 1024;  /* bytes -> KB, rounded */
    unsigned char n = 0, i = 0;
    char tmp[4];
    if (!k) tmp[n++] = '0';
    while (k) { tmp[n++] = '0' + k % 10; k /= 10; }
    while (n) fp[i++] = tmp[--n];
    fp[i++] = 'K'; fp[i++] = ' ';
    fp[i++] = 'u'; fp[i++] = 's'; fp[i++] = 'e'; fp[i++] = 'd'; fp[i] = 0;
    gb_textbw(FP_COL, 0, fp);
}

/* tidy_icons: snap every icon back to its boot position - drives packed down the
   left column (detection order), Clock/Trash on the right - then repaint (#74). */
static void tidy_icons(void)
{
    unsigned char i, n = 0;
    for (i = 0; i < 3; i++)
        if (ic_present[i]) { ic_x[i] = 0; ic_y[i] = DRIVE_TOP + n * DRIVE_STEP; n++; }
    ic_x[IDX_CLOCK] = 72; ic_y[IDX_CLOCK] = 35;     /* the boot positions (see ic_x/ic_y) */
    ic_x[IDX_TRASH] = 72; ic_y[IDX_TRASH] = 160;
    gb_curhide();
    gb_restore_parent();
}

/* sys_action: the System menu handler, dispatched by the gb_doc framework (#142). */
static const char *const sys_items[4] = {
    "Ram Usage", "Refresh Media", "Tidy Icons", "Exit to DOS"
};
static void sys_action(unsigned char sel)
{
    if (sel == 0) {                            /* Ram Usage: toggle the footprint (it then
                                                  persists - nothing else touches the bar) */
        show_ram ^= 1;
        gb_curhide();
        gb_wm_damage(0, 0, 80, 200);           /* the footprint lives in the bar, outside the
                                                  System dropdown's damage clip - widen first
                                                  or the draw is clipped away (#153) */
        if (show_ram) draw_footprint();
        else gb_fill(FP_COL, 0, 13, 8, 1);
        gb_curshow();
    } else if (sel == 1) {                     /* Refresh Media (the old "Media") */
        gb_curhide();
        drive_poll();
        gb_restore_parent();
    } else if (sel == 2) {                     /* Tidy Icons */
        tidy_icons();
    } else if (sel == 3) {                     /* Exit to DOS */
        gb_exit();                              /* does not return */
    }
}

/* on_event: kernel callback (issue #32). Fires when the user clicks the
   kernel-owned top bar; proves the kernel->app round-trip by showing the
   message payload (the clicked column) in the hint line. */
/* trash_label: "Trash: NAME.EXT" from the dragged 11-byte 8.3 name (#62 phase 1). */
static char *trash_label(void)
{
    static char m[20] = "Trash: ";
    const char *e = gb_dragname;
    unsigned char i, j = 7;
    for (i = 0; i < 8 && e[i] != ' '; i++) m[j++] = e[i];
    if (e[8] != ' ') { m[j++] = '.'; for (i = 8; i < 11 && e[i] != ' '; i++) m[j++] = e[i]; }
    m[j] = 0;
    return m;
}

static void on_event(void)
{
    if (gb_msg.type == GB_MSG_DROP) {                  /* a file dropped on the desktop (#62) */
        if (hit_icon(gb_mx(), gb_my()) == IDX_TRASH) { /* on Trash -> delete the file */
            gb_file_delete(gb_dragname);               /* (the source window then re-lists) */
            gb_curhide();
            gb_text(1, 10, trash_label());             /* "Trash: NAME" confirmation */
            gb_curshow();
        }
        return;
    }
    gb_doc_event();                                    /* a top-bar title click -> the framework */
}

/* on_frame: one frame of the desktop, called by the kernel's window-manager loop
   (issue #45). The kernel polls before calling, so read input with gb_flags/mx/my
   - never gb_poll here. What used to be the body of a for(;;) loop, with each
   'continue' now a 'return' (end this frame). */
static void on_frame(void)
{
    unsigned char flags = gb_flags(), mx = gb_mx(), my = gb_my(), held, icon;

    desk_active = 1;                              /* the desktop is focused this frame (#153) */
    if (!menu_inited) {                          /* 1st frame: the window is now registered+focused */
        menu_inited = 1;
        gb_doc(&deskdoc);                        /* empty doc: no File/Edit/View */
        gb_menu_add("System", sys_items, 4, sys_action);
    }
    if (dc_timer) dc_timer--;
    /* the desktop is the permanent root - ESC doesn't exit GEOBENCH (use System >
       Exit to DOS to leave); ESC only closes apps launched on top of it */

    if (gb_doc_frame()) {                  /* a System menu opened/ran (#142) */
        gb_curhide();
        gb_wm_damage(0, 0, 80, 200);       /* gb_popup left a narrow damage clip; the desktop
                                              repaints fully here and paint() doesn't reset the
                                              clip, so widen it back or the next op stays clipped
                                              (#153: a dragged icon vanished on drop). */
        paint();
        gb_curshow();
        return;
    }

    held = flags & GB_FIRE;
    if (held_prev && !held) {              /* fire released */
        if (drag_active) drop();           /* was dragging -> drop at the new spot */
        drag_armed = 0;                    /* armed but never moved -> just a click, icon untouched */
        held_prev = 0;
        return;
    }
    held_prev = held;

    if (drag_active) {                     /* follow the pointer */
        dragmove(mx, my);
        return;
    }

    if (drag_armed) {                      /* pressed on an icon: lift only once it really moves (#153) */
        unsigned char dx = (mx > arm_mx) ? (unsigned char)(mx - arm_mx) : (unsigned char)(arm_mx - mx);
        unsigned char dy = (my > arm_my) ? (unsigned char)(my - arm_my) : (unsigned char)(arm_my - my);
        if (dx >= DRAGTH || dy >= DRAGTH) { drag_armed = 0; dragstart(dc_idx, mx, my); }
        return;
    }

    if (!(flags & GB_CLICK)) return;       /* a fresh press? */
    icon = hit_icon(mx, my);
    if (icon == NONE) { select_icon(NONE); return; }   /* click empty space -> deselect (#153) */

    if (dc_timer && dc_idx == icon) {      /* second click -> open */
        unsigned char opens = ic_drive[icon] || icon == IDX_CLOCK;
        if (opens && gb_wm_full())                           /* no free bank -> say so (#153) */
            gb_alert("Sorry, not enough RAM", "to run more apps.");
        else if (ic_drive[icon]) {                           /* browse that drive (#65): */
            select_icon(NONE);                               /* opening clears the selection (#153) */
            gb_set_drive(icon);                              /* icon idx 0/1/2 = C/A/B   */
            gb_wm_open("FILEMGR APP");
        }
        else if (icon == IDX_CLOCK) { select_icon(NONE); gb_wm_open("CLOCK   APP"); } /* clock (#72) */
        dc_timer = 0;
        held_prev = 0;
    } else {                               /* first click: select + arm; the lift waits for movement (#153) */
        select_icon(icon);
        dc_idx = icon;
        dc_timer = DCLICK;
        drag_armed = 1;
        arm_mx = mx;
        arm_my = my;
    }
}

/* the desktop is the root window: full screen below the top bar, on_repaint = the
   full paint() (restacked behind any window), on_event = file-drop + the System menu.
   menu = 0: the gb_doc framework installs the "System" title dynamically (#142). Its
   rect spans the screen so it is the bottom catch-all for click-to-focus. */
static const gb_win_t deskwin = { 0, 8, 80, 192, on_frame, paint, on_event, 0 };

void main(void)
{
    *WM_FS = 0;                                 /* clear the fullscreen flag at boot (low RAM is
                                                   uninitialised; bar_draw reads it every frame) */
    drive_poll();                               /* drives present at boot -> icons (#65) */
    paint();
    gb_curshow();                               /* paint() no longer shows the pointer (#153) */
    drag_active = 0;
    dc_timer = 0;
    held_prev = 0;
    gb_on_bar(bar_draw);                        /* top-bar handler runs every frame (#77) */
    gb_wm_run(&deskwin);                        /* register + run the kernel WM (#45) */
}
