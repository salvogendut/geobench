/* gb.h - shared C bindings for the GEOBENCH kernel API.
 *
 * The one libgb every GEOBENCH C app uses. Each function is a thin asm
 * trampoline (gblib.s) over a fixed entry in the kernel jump table
 * (lib/gbapp.inc). Apps are compiled by SDCC to run in an expansion bank at
 * #4000 and reach the resident kernel only through these calls. */
#ifndef GB_H
#define GB_H

/* poll flag bits (the D byte from GB_POLL) */
#define GB_CLICK 0x01 /* fresh press this frame */
#define GB_QUIT  0x02 /* ESC pressed */
#define GB_FIRE  0x04 /* fire held */

void gb_cls(void);                                       /* clear screen + home  */
void gb_text(unsigned char col, unsigned char line,      /* 6x8 text, white      */
             const char *s);
void gb_textk(unsigned char col, unsigned char line,     /* 6x8 text, black (pen 2) */
              const char *s);
void gb_textbw(unsigned char col, unsigned char line,    /* 6x8 text, black on white */
               const char *s);
void gb_textrev(unsigned char col, unsigned char line,   /* 6x8 text, white on black (reverse) */
                const char *s);
/* Reusable UI triangle glyphs (font codes 128-131); draw like text, e.g.
 * gb_textk(x, y, GLYPH_TRI_RIGHT). Present in DEFAULT.FNT (not CLASSIC.FNT). */
#define GLYPH_TRI_UP    "\x80"
#define GLYPH_TRI_DOWN  "\x81"
#define GLYPH_TRI_LEFT  "\x82"
#define GLYPH_TRI_RIGHT "\x83"
void gb_window(unsigned char col, unsigned char line,    /* window: pos + size   */
               unsigned char w, unsigned char h, const char *title);
/* gb_drag_window: drag a w x h outline from (*x,*y) by the pointer until release;
 * updates *x,*y to the dropped position (clamped on screen). Caller lifts its
 * window to the backdrop first and redraws at (*x,*y) after. Returns 1 if moved.
 * (lib/gb/gbwin.c) */
unsigned char gb_drag_window(unsigned char *x, unsigned char *y,
                             unsigned char w, unsigned char h);
/* gb_restorerect: blit a wbytes x h Mode-1 byte buffer to the screen at (x,y) in
 * byte columns / rows (#114). The buffer is row-major (wbytes per row). Bracket
 * with gb_curhide/gb_curshow. Used for the PAINT canvas + tool icons. */
void gb_restorerect(unsigned char x, unsigned char y,
                    unsigned char wbytes, unsigned char h, const void *buf);
void gb_fill(unsigned char col, unsigned char line,      /* filled rectangle     */
             unsigned char w, unsigned char h, unsigned char pen);
void gb_frame(unsigned char col, unsigned char line,     /* rectangle outline    */
              unsigned char w, unsigned char h, unsigned char pen);
void gb_icon(unsigned char slot, unsigned char col,      /* full icon blit       */
             unsigned char line);
void gb_icon_half(unsigned char slot, unsigned char col,   /* half-height (16px) blit */
                  unsigned char line);                     /* by slot, for list views */
void gb_curshow(void);                                   /* draw the pointer     */
void gb_curhide(void);                                   /* lift the pointer     */
unsigned char gb_poll(void);          /* frame poll -> flags; caches cursor pos   */
unsigned char gb_mx(void);            /* last poll's cursor byte column           */
unsigned char gb_my(void);            /* last poll's cursor line                  */
unsigned int  gb_mxp(void);           /* pointer PIXEL x 0-319 (pixel-accurate, #114) */
unsigned char gb_flags(void);         /* last poll's flags (for gb_wm_run apps)   */
char *gb_dir1(void);                  /* first dir entry -> "NAME.EXT", 0 at end  */
char *gb_dirn(void);                  /* next dir entry  -> "NAME.EXT", 0 at end  */
void gb_launch(void);                 /* launch the current dir entry             */
void gb_run(const char *name);        /* run a named app, return when it quits    */
unsigned int gb_fs_load(char *buf, unsigned int max);    /* load opened file ->   */
                                                         /* byte count            */
unsigned char gb_fs_save(char *buf, unsigned int len);   /* save opened file ->   */
                                                         /* 1 ok / 0 fail         */
unsigned char gb_getkey(void);        /* typed char from the keyboard, 0 if none  */
unsigned char gb_vsync(void);         /* wait one frame -> 1 if ESC held, else 0  */

/* Event callback (issue #32): the kernel calls a registered handler when an
 * event it owns occurs - currently a click in the top bar (GB_MSG_MENU). The
 * handler is invoked during gb_poll (so call gb_poll in your loop), reads the
 * message from gb_msg, and must return promptly (do not call gb_poll from it).
 * The message lives at a fixed low-RAM address shared with the kernel. */
typedef struct {
    unsigned char type;   /* GB_MSG_* */
    unsigned char p0;     /* menu: clicked byte column */
    unsigned char p1;
    unsigned char p2;
} gb_msg_t;
#define GB_MSG_MENU 1
/* GB_MSG_DROP (#62): a file was dropped on this window via drag-and-drop. The
 * dragged item's 11-byte 8.3 name is at gb_dragname; the drop position is the
 * current gb_mx()/gb_my(). Delivered to the *target* window's on_event handler. */
#define GB_MSG_DROP 2
/* Window messages (#148): the kernel routes EVERY managed-window callback through
 * the single gb_mwin_t.proc, keyed by gb_msg.type - the proc switches on it. */
#define GB_MSG_DRAW  3   /* draw the content (the WM already drew frame/title/grip) */
#define GB_MSG_CLICK 4   /* a press in the content area (grip via gb_in_grip)        */
#define GB_MSG_FRAME 5   /* a focused frame, no chrome click (gb_doc_frame, tick)    */
#define GB_MSG_CLOSE 6   /* close requested (gadget/ESC): confirm + gb_wm_close      */
#define GB_MSG_DRAG  7   /* a title-bar press: gb_drag_window + gb_wm_setpos         */
#define gb_dragname ((const char *)0x13BB)
#define gb_msg (*(volatile gb_msg_t *)0x1302)
void gb_on_event(void (*handler)(void));   /* register handler, 0 to clear */

/* Top-bar menu (issue #34). The app registers menu titles the kernel draws in
 * the bar (persisting across clock ticks); a click on the bar arrives via the
 * gb_on_event callback as GB_MSG_MENU with p0 = the clicked byte column, so the
 * app maps the column to its title and draws its own dropdown. def layout:
 *   byte 0: title count (<=4)
 *   then per title: byte col, then an 8-byte NUL/space-padded label
 * Cleared automatically when the app launches a child or quits. */
void gb_menu(const void *def);

/* Shared modal dialogs (#114, lib/gb/gbwin.c) - the File-menu building blocks every
 * menu-driven app reuses. Recipe for a new app:
 *   - put a menu def in your gb_win_t.menu + a gb_on_event handler that, when a
 *     title is clicked, sets a "want_menu" flag IF !gb_modal() (so a click during a
 *     dialog is ignored);
 *   - in on_frame, when want_menu, call gb_popup(titleCol, 8, items, n), dispatch
 *     the returned row to your New/Load/Save handlers, then repaint your window.
 * gb_popup: framed list, auto-sized; returns the row clicked or 0xFF (cancel/ESC).
 * gb_prompt: name-entry box; fills buf with an UPPERCASE 8.3 name (<= maxlen, <=12),
 *   returns 1 on Enter (non-empty) or 0 on ESC/empty.
 * gb_modal: 1 while any of these (or a custom dialog) is up. A custom modal dialog
 *   brackets itself with gb_modal_set(1)/(0). Both leave the cursor shown and the
 *   popup erased to the backdrop - repaint your window after. */
unsigned char gb_popup(unsigned char col, unsigned char line,
                       const char *const *labels, unsigned char n);
unsigned char gb_prompt(const char *caption, char *buf, unsigned char maxlen);
unsigned char gb_modal(void);
void          gb_modal_set(unsigned char on);
/* gb_popup_close: make a live gb_popup cancel itself - used by the top-bar menu toggle.
 * A re-click of a menu title can't reach the popup as a click-away (the kernel consumes
 * bar clicks), so the title handler signals the close here. */
void          gb_popup_close(void);

/* Navigable file/folder chooser (#142, lib/gb/gbpick.c, opt-in via build_capp.sh
 * PICKER=1 - DOC=1 implies it). The standard Open/Save dialog, built on the dir
 * primitives (gb_dir1/dirn/isdir/entname + gb_chdir/back), shared by any app - this
 * is the file DIALOG, not the File Manager. Folders descend, ".." goes up; both
 * leave the filesystem positioned in the chosen directory, so a following
 * gb_fs_load / gb_fs_save (or gb_set_name) targets it.
 *   gb_pickfile - Open: pick a file; fills name11 with its raw 11-byte 8.3 name,
 *                 returns 1 (0 = cancel). Caller then gb_set_name(name11)+gb_fs_load.
 *   gb_pickdir  - Save destination: navigate, then "[Save here]" returns 1 with the
 *                 FS in that directory (0 = cancel). Caller then prompts a name.
 * `exts` is a NULL-terminated list of 3-char space-padded uppercase extensions to
 * show (e.g. {"TXT","BAS",0}); NULL shows all files. Folders are always shown. */
unsigned char gb_pickfile(char *name11, const char *const *exts);
unsigned char gb_pickdir(const char *const *exts);

/* ---- Document-app framework (#142) -----------------------------------------
 * An app that edits a document registers a gb_doc_t once, and the system gives
 * it a standard "File" menu (New / Load / Save / Save As) in the top bar and
 * runs those actions for it - so the app carries NO menu or file-dialog code.
 * The app provides only its buffer and three hooks:
 *   on_new()         reset to an empty document
 *   on_open(len)     buf now holds a `len`-byte file just loaded - parse it
 *   on_save() -> len fill buf with the document, return the byte count to write
 * Mark edits with gb_doc_dirty() so New / Load / close can offer to save first.
 *
 * Recipe: call gb_doc(&doc) before gb_wm_run; in your on_event call
 * gb_doc_event() first (returns 1 if the framework consumed the event); in your
 * on_frame call gb_doc_frame() (runs a pending File menu). Add app-specific
 * menus with gb_menu_add; their selections arrive as GB_MSG_MENUSEL. */
typedef struct {
    char         *buf;          /* document buffer (in the app's bank) */
    unsigned int  bufmax;       /* its capacity in bytes */
    void         (*on_new)(void);               /* clear to an empty document */
    void         (*on_open)(unsigned int len);  /* buf holds a loaded file of `len` bytes */
    unsigned int (*on_save)(void);              /* fill buf -> bytes to write */
    void         (*on_saved)(void);             /* optional: after the write (restore buf
                                                   if on_save transformed it); NULL if none */
    /* Optional Edit menu: if on_copy is non-NULL the framework also adds a
     * standard "Edit" menu (Select All / Copy / Paste) backed by the shared
     * clipboard (gb_clip_*), so copy/paste works across apps. The hooks do the
     * app-specific part: copy the selection INTO the clipboard, paste the
     * clipboard AT the cursor, select everything. Leave NULL for a view-only app. */
    void         (*on_copy)(void);
    void         (*on_paste)(void);
    void         (*on_selectall)(void);
    /* File dialog extension filter: a NULL-terminated list of 3-char (space-padded)
     * uppercase extensions the app handles, e.g. {"TXT","BAS","CFG",0}. The Open/Save
     * dialog then shows only these files (folders always shown). NULL = show all. */
    const char *const *exts;
    /* Optional extra File item: if export_label is non-NULL the framework adds it to the
     * File menu after Save As (e.g. XAOS "Save as PIC" - export the rendered image while
     * Save keeps the definition). on_export does the whole action itself. NULL = none. */
    const char  *export_label;
    void         (*on_export)(void);
    /* Optional View menu (added after Edit). "Fullscreen" is offered when on_fullscreen
     * is non-NULL - the framework toggles a flag and calls on_fullscreen(on); the app
     * resizes its own window to cover the screen (on=1) or restore (on=0) and repaints.
     * Any view_items (NULL-terminated) follow, dispatched to on_view(index). */
    void         (*on_fullscreen)(unsigned char on);
    const char *const *view_items;
    void         (*on_view)(unsigned char item);
} gb_doc_t;
void          gb_doc(const gb_doc_t *d);   /* register; adds the standard File (+Edit) menu */
void          gb_doc_dirty(void);          /* mark the document modified */
unsigned char gb_doc_modified(void);       /* is it dirty? (e.g. for a "*" in the title) */
const char   *gb_doc_name(void);           /* the current file's 11-byte 8.3 name */
unsigned char gb_doc_close(void);          /* on the close gadget: confirm-if-dirty; 1=close ok */
unsigned char gb_doc_event(void);          /* in on_event: 1 if the framework handled it */
unsigned char gb_doc_frame(void);          /* in on_frame: ran a menu? (1 -> repaint window) */

/* Shared clipboard (#142): a system buffer that survives app switches, so copy in
 * one app and paste in another. An app's on_copy fills it; on_paste reads it. */
void          gb_clip_set(const char *buf, unsigned int len);   /* copy in */
unsigned int  gb_clip_len(void);                                /* clipboard length */
unsigned int  gb_clip_get(char *buf, unsigned int max);         /* paste out -> length */

/* gb_menu_add: add an app-specific menu title with `n` items to the top bar
 * (alongside the standard File menu). When the user picks item `i`, the
 * framework calls on_select(i). e.g. gb_menu_add("Edit", edit_items, 3, do_edit). */
void gb_menu_add(const char *title, const char *const *items, unsigned char n,
                 void (*on_select)(unsigned char item));

/* gb_set_name: set the current file (an 11-byte 8.3 name, space-padded, no dot,
 * e.g. "NOTES   TXT") so a later gb_fs_load/gb_fs_save targets it - how an app
 * does New / Save As / open a file chosen from a picker. */
void gb_set_name(const char *name11);

/* gb_get_name: copy this window's 11-byte launch name (the file it was opened with,
 * space-padded 8.3, no dot) into dst - e.g. to show it in a title bar. */
void gb_get_name(char *dst11);

/* Banked picture buffer (#164, kernels with WM_GADGETS only). gb_pic_open loads the opened
 * file into a borrowed app-pool bank so the Viewer can show a .PIC larger than its own 16K
 * page; it returns 1 (header parsed: read PIC_WB/PIC_H/PIC_OFF at 0x130C-0x130E) or 0 (no
 * free bank / not a .PIC / unsupported kernel) -> fall back to the in-page buffer. gb_pic_blit
 * blits a wbytes x h region from offset src_off in the bank to the screen; gb_pic_close frees
 * the bank. The cursor is handled by the WM during on_draw. */
unsigned char gb_pic_open(void);
void gb_pic_blit(unsigned char x, unsigned char y, unsigned char wbytes,
                 unsigned char h, unsigned int src_off);
void gb_pic_close(void);
void gb_restore_parent(void);                /* repaint ancestor apps behind us */
void gb_wm_damage(unsigned char x, unsigned char y,
                  unsigned char w, unsigned char h);  /* limit the next repaint to a rect (#153) */
unsigned char gb_wm_full(void);              /* 1 = no free app bank for another window (#153) */
void gb_alert(const char *l1, const char *l2);  /* modal message box, click to dismiss (#153) */

/* Cooperative window manager (issue #45). Instead of owning a for(;;) loop, an
 * app fills a gb_win_t and registers a window: the KERNEL runs the master loop,
 * calls the focused window's on_frame every frame (read input with gb_flags/
 * gb_mx/gb_my - do NOT call gb_poll inside it), and on a click hit-tests the
 * rects to switch focus. on_repaint redraws the whole window with no input;
 * on_event handles a top-bar click (0 = none). The rect (x,y,w,h) is the
 * window's screen area used for click-to-focus.
 *
 *   gb_wm_run   - the ROOT window (desktop): register + enter the loop (no return).
 *   gb_wm_open  - open an app as a new co-resident window on top (non-blocking).
 *   gb_wm_add   - register the calling window (an app's main() does this when it
 *                 was started by gb_wm_open), then return to the opener.
 *   gb_wm_close - close the calling window; return from on_frame right after. */
typedef struct {
    unsigned char x, y, w, h;       /* window rect (byte col, line, size)  */
    void (*on_frame)(void);         /* called each frame when focused       */
    void (*on_repaint)(void);       /* redraw the whole window, no input    */
    void (*on_event)(void);         /* top-bar click handler, or 0          */
    const void *menu;               /* top-bar menu def (gb_menu fmt), or 0 */
} gb_win_t;
void gb_wm_run(const gb_win_t *desc);
void gb_wm_add(const gb_win_t *desc);

/* Managed window (#146, #148): the KERNEL owns the chrome. Register with gb_wm_managed
 * and the WM draws the frame/title/grip and runs close/drag/resize for you; the app
 * provides only the interior, through ONE handler `proc` (a WndProc). The kernel calls
 * proc(void) for every window callback with gb_msg.type set; the proc switches on it:
 *   GB_MSG_DRAW   draw the CONTENT (the WM already drew frame/title/grip)
 *   GB_MSG_CLICK  a press in the content area (chrome handled; grip via gb_in_grip)
 *   GB_MSG_FRAME  a focused frame, no chrome click (gb_doc_frame, idle/tick)
 *   GB_MSG_CLOSE  close requested (gadget/ESC): confirm + gb_wm_close
 *   GB_MSG_DRAG   a title-bar press: gb_drag_window + gb_wm_setpos + gb_restore_parent
 *   GB_MSG_MENU / GB_MSG_DROP  top-bar click / file drop (route to gb_doc_event)
 * Read input with gb_mx/gb_my, the live rect with gb_wm_x/y/w/h (WM-owned; read-only).
 * A message the proc doesn't handle is simply ignored. Resizable iff min_w != 0.
 * title -> the app's title string (read each repaint). */
typedef struct {
    unsigned char x, y, w, h;       /* initial rect (byte col, line, size) */
    unsigned char min_w, min_h;     /* min size; min_w == 0 -> not resizable */
    void (*proc)(void);             /* the window's ONE handler; switch on gb_msg.type */
    const char *title;
} gb_mwin_t;
void          gb_wm_managed(const gb_mwin_t *desc);   /* register a kernel-managed window */
unsigned char gb_wm_x(void);    /* live window rect (WM-owned) */
unsigned char gb_wm_y(void);
unsigned char gb_wm_w(void);
unsigned char gb_wm_h(void);
void gb_wm_open(const char *name);
void gb_wm_close(void);
void gb_wm_setpos(unsigned char x, unsigned char y);  /* move our hit rect (drag) */
/* Resizeable windows (#81). A window with a resize grip (bottom-right corner) draws
 * it with gb_draw_grip in its frame; a press there (gb_in_grip) runs gb_drag_resize,
 * which keeps the top-left fixed and pulls the bottom-right corner. After the drag,
 * call gb_wm_setsize so click-to-focus + the restack use the new size, then redraw.
 * Apps reflow content to win_w/win_h; fixed-layout apps (e.g. ICONED) skip it. */
#define GRIP_W 2          /* grip size: 2 bytes (8px) x 6px */
#define GRIP_H 6
void gb_wm_setsize(unsigned char w, unsigned char h);
void gb_draw_grip(unsigned char x, unsigned char y, unsigned char w, unsigned char h);
unsigned char gb_in_grip(unsigned char x, unsigned char y, unsigned char w,
                         unsigned char h, unsigned char mx, unsigned char my);
unsigned char gb_drag_resize(unsigned char x, unsigned char y, unsigned char *w,
                             unsigned char *h, unsigned char minw, unsigned char minh);
/* gb_wm_launch_as: open app `app` (an 8.3 name) as a co-resident window, with the
 * current dir entry as its file arg (so a data file auto-opens). The File Manager
 * picks the app by file extension - the file-type routing that used to live in the
 * kernel's app_for_ext now lives in C (#70). */
void gb_wm_launch_as(const char *app);

/* Clock read-out (#72). gb_time() refreshes the time; read gb_hour/min/sec - RAW
 * values, BCD unless gb_binmode is nonzero (convert in the app, to keep the kernel
 * lean). CLOCK.APP draws its hands by calling the firmware graphics directly. */
void gb_time(void);
#define gb_hour    (*(volatile unsigned char *)0x1240)
#define gb_min     (*(volatile unsigned char *)0x1241)
#define gb_sec     (*(volatile unsigned char *)0x1242)
#define gb_binmode (*(volatile unsigned char *)0x1243)

/* System menu (#74). gb_ksize = resident kernel size in bytes (for "Ram Usage").
 * gb_exit() leaves GEOBENCH and returns to AMSDOS - it does not return. */
#define gb_ksize (*(volatile unsigned int *)0x1246)
void gb_exit(void);

/* Directory navigation (issue #54). gb_isdir reports whether the entry just
 * returned by gb_dir1/gb_dirn is a directory; gb_chdir descends into the
 * positioned entry (re-list afterwards to show it in the same window); gb_back
 * returns to the parent directory (no-op at the top). */
unsigned char gb_isdir(void);
void gb_chdir(void);
void gb_back(void);
char *gb_entname(void);  /* current entry's raw 11-byte 8.3 name (with extension) */

/* Drag-and-drop (issue #62). gb_drag_start begins dragging the named item (an
 * 11-byte 8.3 name); it blocks, following the pointer until the button is
 * released. Returns 1 if it was dropped on another window (that window's
 * on_event got a GB_MSG_DROP and handled it), or 0 if it was just a click (no
 * movement / no target) so the caller should treat it as a normal selection. */
unsigned char gb_drag_start(const char *name);

/* gb_file_delete: delete a file (11-byte 8.3 name) from the current directory -
 * frees its clusters and clears its directory entry. Returns 1 if deleted, 0 on
 * failure (not found, a directory, or unsupported backend). Used by the desktop
 * Trash drop handler (#62). */
unsigned char gb_file_delete(const char *name);

/* gb_drives: probe the drives GEOBENCH can reach. Returns a bitmask (#65):
 *   GB_DRV_A (floppy A), GB_DRV_B (floppy B), GB_DRV_C (Disk C, the hard volume).
 *   GB_DRV_C_SD: Disk C is an Albireo SD/USB card (vs IDE), set with GB_DRV_C so
 *   the desktop can show a different icon (#104).
 * Floppy probing spins the drive motor, so call at boot and on demand. */
#define GB_DRV_A    0x01
#define GB_DRV_B    0x02
#define GB_DRV_C    0x04
#define GB_DRV_C_SD 0x08
unsigned char gb_drives(void);

/* Multi-drive (#65). gb_set_drive selects which drive subsequent FS calls target
 * (0 = IDE / Disk C, 1 = floppy A, 2 = floppy B); gb_get_drive returns the current
 * one. A File Manager window reads gb_get_drive at startup (the drive the desktop
 * opened it on) and re-asserts gb_set_drive before its FS ops, so each window
 * browses its own drive. */
#define GB_DRIVE_C 0
#define GB_DRIVE_A 1
#define GB_DRIVE_B 2
void gb_set_drive(unsigned char d);
unsigned char gb_get_drive(void);

/* Cross-drive copy (#65 phase 3; orchestration moved app-side in #74). On a
 * GB_MSG_DROP, the drop-target window copies the dragged file (name = gb_dragname)
 * from its source drive/dir to its own drive's root, via the shared staging buffer:
 *   gb_copy_begin();                          // switch to the drag source
 *   gb_set_name(gb_dragname);
 *   n = gb_fs_load(gb_copybuf, GB_COPYMAX);
 *   gb_set_drive(my_drive);                    // back to this window's drive
 *   gb_set_name(gb_dragname);
 *   gb_fs_save(gb_copybuf, n);                 // lands in the root
 *   gb_copy_end();                             // restore this window's drive/dir
 * gb_copybuf is the kernel's FAT staging area (low RAM), so no app buffer is needed. */
void gb_copy_begin(void);
void gb_copy_end(void);
#define gb_copybuf ((char *)0x2200)
#define GB_COPYMAX 0x1C00

/* Top-bar handler (experiment #77). gb_on_bar registers a handler the WM master loop
 * runs every frame in the desktop's page, regardless of which window has focus - so a
 * desktop-owned top bar (clock, menu titles, footprint) stays live. Draw only (no
 * input / no gb_poll); use change-detection to avoid a wasteful per-frame repaint. */
void gb_on_bar(void (*handler)(void));
#endif /* GB_H */
