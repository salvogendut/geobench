/* gbdoc.c - the document-app framework (#142), an OPT-IN libgb module.
 *
 * ONE File menu for every app. An app registers a gb_doc_t (buffer + new/open/save
 * hooks) and gets a standard top-bar "File" menu (New / Load / Save / Save As) that
 * the framework renders and runs - so the app carries no menu or file-dialog code.
 * Apps add their own titles with gb_menu_add. Linked only when build_capp.sh sets
 * DOC=1; the dialogs it calls (gb_popup/gb_prompt/gb_pickfile/gb_pickdir) are the tiny
 * gbui_stub stubs - the heavy render lives in the paged GBUI module (#142 step 1b), so
 * gbdoc + an app's menus add only a few hundred bytes, not the whole dialog stack.
 *
 * Recipe (see gb.h): gb_doc(&doc) before gb_wm_run; in on_event call gb_doc_event()
 * first; in on_frame call gb_doc_frame() and repaint your window if it returns 1.
 */
#include "gb.h"

#define DOC_MAXTITLES 4              /* File + up to 3 app titles */

static const gb_doc_t *g_doc;
static unsigned char   g_dirty;
static unsigned char   g_want;       /* 0 = none, else (title index + 1) to drop */
static char            g_name[11];   /* current file's 8.3 name (Load/Save As update it) */
static void set_name(const char *n11);

static const char     *g_label[DOC_MAXTITLES];
static const char *const *g_items[DOC_MAXTITLES];
static unsigned char   g_nitems[DOC_MAXTITLES];
static void          (*g_handler[DOC_MAXTITLES])(unsigned char);
static unsigned char   g_col[DOC_MAXTITLES];     /* each title's byte column */
static unsigned char   g_ntitles;
static unsigned char   g_def[1 + DOC_MAXTITLES * 9];   /* MENU_DEF: count, {col,label[8]}* */

/* The File menu is built per-app: only items whose hook exists appear, so a
 * read-only app (on_open only) shows just "Load", and a document-less app (the
 * File Manager) shows no File title at all (#142). file_act maps each visible
 * item back to its action. */
static const char     *file_items[6];
static unsigned char   file_act[6];   /* per visible item: 0 New,1 Load,2 Save,3 Save As,4 Export */
static unsigned char   file_nf;       /* number of visible File items */
static void            file_action(unsigned char sel);
static const char *const confirm_items[] = { "Save", "Don't Save", "Cancel" };

static unsigned char slen(const char *s) { unsigned char n = 0; while (s[n]) n++; return n; }

#ifndef GBDOC_RO       /* to_83 / name83 are only used by Save As (omitted in read-only) */
/* to_83: "NAME.EXT" (or "NAME") -> an 11-byte space-padded 8.3 name (no dot) for
 * gb_set_name. */
static char name83[11];
static void to_83(const char *src)
{
    unsigned char i = 0, j;
    for (j = 0; j < 11; j++) name83[j] = ' ';
    for (j = 0; j < 8 && src[i] && src[i] != '.'; j++) name83[j] = src[i++];
    while (src[i] && src[i] != '.') i++;          /* skip any over-long name tail */
    if (src[i] == '.') {
        i++;
        for (j = 0; j < 3 && src[i]; j++) name83[8 + j] = src[i++];
    }
}
#endif

/* (re)build the kernel MENU_DEF from the registered titles, auto-placing columns,
 * and hand it to the bar. */
static void rebuild(void)
{
    unsigned char i, j, col = 10, *p = g_def;
    *p++ = g_ntitles;
    for (i = 0; i < g_ntitles; i++) {
        unsigned char len = slen(g_label[i]);
        g_col[i] = col;
        *p++ = col;
        for (j = 0; j < 8; j++) *p++ = (j < len) ? g_label[i][j] : 0;
        col = (unsigned char)(col + (len * 6 + 3) / 4 + 1);   /* next title's column */
    }
    gb_menu(g_def);
}

/* --- the standard Edit menu (#142): added when the doc provides on_copy --- *
 * Copy/Paste go through the shared clipboard - gb_clip_* are resident kernel calls
 * (a fixed low-RAM buffer at #3E00), so the clipboard survives app switches and the
 * code isn't duplicated into every app's bank. */
static const char *const edit_items[] = { "Select All", "Copy", "Paste" };
static void edit_action(unsigned char sel)
{
    if      (sel == 0) { if (g_doc->on_selectall) g_doc->on_selectall(); }
    else if (sel == 1) { if (g_doc->on_copy)      g_doc->on_copy(); }
    else if (sel == 2) { if (g_doc->on_paste)     g_doc->on_paste(); }
}

/* --- the View menu (#142): added when the doc provides on_fullscreen or view_items.
 * "Fullscreen" toggles (the app resizes its own window); app view_items follow. */
static const char *g_view[8];          /* "Fullscreen" + the app's view_items */
static unsigned char g_fullscreen;     /* current fullscreen toggle state */
/* #156: the kernel sets this byte when the title-bar maximize gadget is clicked; gb_doc_frame
 * toggles the SAME g_fullscreen the View > Fullscreen menu uses, so the two stay in sync. We
 * clear it when a doc registers (below) so stale RAM can't toggle before the first frame. */
#define gb_maxreq (*(volatile unsigned char *)0x1309)
static void view_action(unsigned char sel)
{
    unsigned char base = 0;
    if (g_doc->on_fullscreen) {
        if (sel == 0) { g_fullscreen = (unsigned char)!g_fullscreen; g_doc->on_fullscreen(g_fullscreen); return; }
        base = 1;
    }
    if (g_doc->on_view) g_doc->on_view((unsigned char)(sel - base));
}

void gb_doc(const gb_doc_t *d)
{
    g_doc = d;
    g_dirty = 0;
    g_want = 0;
    g_ntitles = 0;
    {                                 /* build the File menu from the hooks present */
        unsigned char nf = 0;
        if (d->on_new)  { file_items[nf] = "New";     file_act[nf++] = 0; }
        if (d->on_open) { file_items[nf] = "Load";    file_act[nf++] = 1; }
        if (d->on_save) { file_items[nf] = "Save";    file_act[nf++] = 2;
                          file_items[nf] = "Save As"; file_act[nf++] = 3; }
        if (d->export_label) { file_items[nf] = d->export_label; file_act[nf++] = 4; }
        file_items[nf] = 0; file_nf = nf;
        if (nf) {                     /* a document-less app gets no File title at all */
            g_label[g_ntitles]   = "File";
            g_items[g_ntitles]   = file_items;
            g_nitems[g_ntitles]  = nf;
            g_handler[g_ntitles] = file_action;
            g_ntitles++;
        }
    }
    if (d->on_copy) {                 /* standard Edit menu, backed by the shared clipboard */
        g_label[g_ntitles]   = "Edit";
        g_items[g_ntitles]   = edit_items;
        g_nitems[g_ntitles]  = 3;
        g_handler[g_ntitles] = edit_action;
        g_ntitles++;
    }
    if (d->on_fullscreen || d->view_items) {        /* View menu (Fullscreen + app items) */
        unsigned char t = g_ntitles, nv = 0, i;
        if (d->on_fullscreen) g_view[nv++] = "Fullscreen";
        if (d->view_items) for (i = 0; d->view_items[i] && nv < 7; i++) g_view[nv++] = d->view_items[i];
        g_label[t]   = "View";
        g_items[t]   = g_view;
        g_nitems[t]  = nv;
        g_handler[t] = view_action;
        g_fullscreen = 0;
        gb_maxreq    = 0;                /* #156: arm the maximize gadget from a known state */
        g_ntitles    = (unsigned char)(t + 1);
    }
    rebuild();
    gb_get_name(g_name);              /* adopt the file we were launched with */
    if (g_name[0] == 0 || g_name[0] == ' ') set_name("UNTITLED   ");
}

void gb_menu_add(const char *title, const char *const *items, unsigned char n,
                 void (*on_select)(unsigned char item))
{
    unsigned char i = g_ntitles;
    if (i >= DOC_MAXTITLES) return;
    g_label[i]   = title;
    g_items[i]   = items;
    g_nitems[i]  = n;
    g_handler[i] = on_select;
    g_ntitles++;
    rebuild();
}

void gb_doc_dirty(void) { g_dirty = 1; }
unsigned char gb_doc_modified(void) { return g_dirty; }

/* gb_doc_name: the current file's 8.3 name (kept in step with the kernel via
 * set_name), so the app can read it for its title / extension checks. */
const char *gb_doc_name(void) { return g_name; }
static void set_name(const char *n11)
{
    unsigned char i;
    for (i = 0; i < 11; i++) g_name[i] = n11[i];
    gb_set_name(g_name);
}

/* on_event: a top-bar title was clicked -> arm its menu. Returns 1 if it was one
 * of ours (so the app's on_event stops). */
unsigned char gb_doc_event(void)
{
    unsigned char i, col;
    if (gb_msg.type != GB_MSG_MENU) return 0;
    if (gb_modal()) { gb_popup_close(); return 1; }  /* a dropdown is up: re-clicking the title
                                          closes it. The kernel ate this bar click before the popup
                                          loop could see it as a click-away, so signal it. (#142) */
    col = gb_msg.p0;
    for (i = 0; i < g_ntitles; i++) {
        unsigned char w = (unsigned char)((slen(g_label[i]) * 6 + 3) / 4);
        if (col >= g_col[i] && col < (unsigned char)(g_col[i] + w)) { g_want = (unsigned char)(i + 1); return 1; }
    }
    return 0;
}

/* --- the standard File actions ------------------------------------------- */

#ifndef GBDOC_RO
static void do_save(void);
#endif

/* confirm_save: if the document is dirty, ask. -> 1 = go ahead (saved or
 * discarded), 0 = cancel the operation. */
static unsigned char confirm_save(void)
{
#ifdef GBDOC_RO
    return 1;                                        /* read-only: never dirty, nothing to save */
#else
    unsigned char sel;
    if (!g_dirty) return 1;
    sel = gb_popup(28, 90, confirm_items, 3);
    if (sel == 0) { do_save(); return 1; }           /* Save */
    if (sel == 1) return 1;                          /* Don't Save */
    return 0;                                        /* Cancel / ESC */
#endif
}

static void do_new(void)
{
    if (!confirm_save()) return;
    set_name("UNTITLED   ");
    if (g_doc->on_new) g_doc->on_new();
    g_dirty = 0;
}

/* do_load: the shared navigable file chooser (gbpick), then open the choice from
 * its directory. The picker leaves the FS positioned in the chosen folder. */
static void do_load(void)
{
    char nm[11];
    unsigned int len;
    if (!confirm_save()) return;
    if (!gb_pickfile(nm, g_doc->exts)) return;     /* filtered to the app's file types */
    set_name(nm);                                  /* chosen file -> current file */
    len = gb_fs_load(g_doc->buf, g_doc->bufmax);   /* targets the navigated directory */
    if (g_doc->on_open) g_doc->on_open(len);
    g_dirty = 0;
}

#ifndef GBDOC_RO       /* Save / Save As are omitted entirely in a read-only doc (#144) */
static void do_save(void)
{
    unsigned int n  = g_doc->on_save();
    unsigned char ok = gb_fs_save(g_doc->buf, n);
    if (g_doc->on_saved) g_doc->on_saved();          /* restore buf if on_save transformed it */
    if (ok) g_dirty = 0;
}

static char namebuf[16];
static void do_saveas(void)
{
    if (!gb_pickdir(g_doc->exts)) return;          /* navigate to the destination dir */
    if (!gb_prompt("Save as:", namebuf, 12)) return;
    to_83(namebuf);
    set_name(name83);
    do_save();                                     /* writes into the navigated dir */
}
#endif

static void file_action(unsigned char sel)
{
    unsigned char a;
    if (sel >= file_nf) return;
    a = file_act[sel];
    if      (a == 0) do_new();
    else if (a == 1) do_load();
#ifndef GBDOC_RO
    else if (a == 2) do_save();
    else if (a == 3) do_saveas();
#endif
    else if (a == 4) { if (g_doc->on_export) g_doc->on_export(); }   /* the optional export */
}

/* on_frame: drop a pending menu and dispatch it. Returns 1 if a menu ran (the
 * app should repaint its window, since the popup erased a hole in it). */
unsigned char gb_doc_frame(void)
{
    unsigned char t, sel;
    if (gb_maxreq) {                                  /* maximize/restore gadget clicked */
        gb_maxreq = 0;
        if (g_doc->on_fullscreen) { view_action(0); return 1; }  /* same toggle as View>Fullscreen */
    }
    if (!g_want) return 0;
    t = (unsigned char)(g_want - 1);
    g_want = 0;
    sel = gb_popup(g_col[t], 8, g_items[t], g_nitems[t]);
    if (sel != 0xFF) {
        if (g_handler[t]) g_handler[t](sel);         /* File / Edit / View / app menu */
    }
    return 1;
}

/* gb_doc_close: the window close gadget was hit - offer to save first. 1 = go
 * ahead and close, 0 = stay open (user cancelled). */
unsigned char gb_doc_close(void) { return confirm_save(); }
