;; gblib.s - shared C bindings for the GEOBENCH kernel API (see gb.h).
;;
;; Every GEOBENCH C app links this one libgb. Each function is a thin trampoline
;; mapping SDCC's __sdcccall(1) convention onto the kernel jump table:
;;   args:   first small args pack into A then L; the rest are pushed (adjacent
;;           u8 args pack two-per-word, an odd one as a lone byte). Callee-clean
;;           (no caller cleanup), so wrappers consume their pushed args.
;;   return: unsigned char in A, 16-bit (int/pointer) in DE.
;; The jump-table addresses mirror lib/gbapp.inc.
        .module gblib
        .globl  _gb_cls
        .globl  _gb_text
        .globl  _gb_textk
        .globl  _gb_textbw
        .globl  _gb_textrev
        .globl  _gb_window
        .globl  _gb_restorerect
        .globl  _gb_mxp
        .globl  _gb_clip_set
        .globl  _gb_clip_get
        .globl  _gb_clip_len
        .globl  _gb_ui
        .globl  _gb_fill
        .globl  _gb_frame
        .globl  _gb_icon
        .globl  _gb_icon_half
        .globl  _gb_curshow
        .globl  _gb_curhide
        .globl  _gb_poll
        .globl  _gb_mx
        .globl  _gb_my
        .globl  _gb_dir1
        .globl  _gb_dirn
        .globl  _gb_launch
        .globl  _gb_run
        .globl  _gb_fs_load
        .globl  _gb_fs_save
        .globl  _gb_getkey
        .globl  _gb_vsync
        .globl  _gb_on_event
        .globl  _gb_menu
        .globl  _gb_set_name
        .globl  _gb_get_name
        .globl  _gb_pic_open
        .globl  _gb_pic_blit
        .globl  _gb_pic_close
        .globl  _gb_restore_parent
        .globl  _gb_wm_damage
        .globl  _gb_wm_full
        .globl  _gb_flags
        .globl  _gb_wm_run
        .globl  _gb_wm_add
        .globl  _gb_wm_managed
        .globl  _gb_wm_x
        .globl  _gb_wm_y
        .globl  _gb_wm_w
        .globl  _gb_wm_h
        .globl  _gb_wm_open
        .globl  _gb_wm_close
        .globl  _gb_wm_setpos
        .globl  _gb_wm_setsize
        .globl  _gb_wm_launch_as
        .globl  _gb_time
        .globl  _gb_exit
        .globl  _gb_isdir
        .globl  _gb_chdir
        .globl  _gb_back
        .globl  _gb_entname
        .globl  _gb_drag_start
        .globl  _gb_file_delete
        .globl  _gb_drives
        .globl  _gb_set_drive
        .globl  _gb_get_drive
        .globl  _gb_copy_begin
        .globl  _gb_copy_end
        .globl  _gb_on_bar

        .area   _BSS
sv_ret: .ds     2               ; saved return addr for the multi-pop wrappers

        .area   _CODE

;; void gb_cls(void);
_gb_cls:
        jp      0x8003          ; GB_CLS

;; void gb_text(u8 col, u8 line, const char *s);
;;   A=col, L=line, [SP+2]=s. pop/ex (sp),hl loads HL=s and collapses the pushed
;;   arg into the return slot so a plain ret cleans the stack.
_gb_text:
        ld      b, a
        ld      c, l
        ld      d, #1           ; pen   = 1 (white)
        ld      e, #0           ; paper = 0
        pop     hl
        ex      (sp), hl
        call    0x800C          ; GB_TEXT
        ret

;; void gb_textk(u8 col, u8 line, const char *s);   like gb_text but pen 2 (black)
_gb_textk:
        ld      b, a
        ld      c, l
        ld      d, #2           ; pen   = 2 (black)
        ld      e, #0           ; paper = 0
        pop     hl
        ex      (sp), hl
        call    0x800C          ; GB_TEXT
        ret

;; void gb_textbw(u8 col, u8 line, const char *s);   black pen on white paper
_gb_textbw:
        ld      b, a
        ld      c, l
        ld      d, #2           ; pen   = 2 (black)
        ld      e, #1           ; paper = 1 (white)
        pop     hl
        ex      (sp), hl
        call    0x800C          ; GB_TEXT
        ret

;; void gb_textrev(u8 col, u8 line, const char *s);   white pen on black paper
;;   (reverse video, for a highlighted menu row)
_gb_textrev:
        ld      b, a
        ld      c, l
        ld      d, #1           ; pen   = 1 (white)
        ld      e, #2           ; paper = 2 (black)
        pop     hl
        ex      (sp), hl
        call    0x800C          ; GB_TEXT
        ret

;; void gb_window(u8 col, u8 line, u8 w, u8 h, const char *title);
;;   A=col, L=line; stack [ret][w,h][title]. -> B=x C=y D=w E=h HL=title.
_gb_window:
        ld      b, a
        ld      c, l
        pop     hl
        ld      (sv_ret), hl
        pop     hl              ; L=w, H=h
        ld      d, l
        ld      e, h
        pop     hl              ; title
        call    0x800F          ; GB_WINDOW
        ld      hl, (sv_ret)
        jp      (hl)

;; void gb_restorerect(u8 x, u8 y, u8 wbytes, u8 h, const void *buf);
;;   blit a wbytes x h byte buffer to the screen at (x,y). Same arg shape as
;;   gb_window: A=x, L=y, stack [ret][wbytes,h][buf]. (#114)
_gb_restorerect:
        ld      b, a
        ld      c, l
        pop     hl
        ld      (sv_ret), hl
        pop     hl              ; L=wbytes, H=h
        ld      d, l
        ld      e, h
        pop     hl              ; buf
        call    0x8039          ; GB_RESTORERECT
        ld      hl, (sv_ret)
        jp      (hl)

;; unsigned int gb_mxp(void);  -> pointer PIXEL x (0-319). (#114)
_gb_mxp:
        call    0x80A2          ; GB_MXP -> HL = pixel x
        ex      de, hl          ; SDCC z80 returns 16-bit in DE
        ret

;; void gb_clip_set(const char *buf, unsigned int len);  HL=buf, DE=len (shared clipboard #142)
_gb_clip_set:
        jp      0x80A5          ; GB_CLIPSET
;; unsigned int gb_clip_get(char *buf, unsigned int max);  HL=buf, DE=max -> DE = copied
_gb_clip_get:
        call    0x80A8          ; GB_CLIPGET -> BC = copied
        ld      d, b
        ld      e, c
        ret
;; unsigned int gb_clip_len(void);  -> DE = clipboard length
_gb_clip_len:
        call    0x80AB          ; GB_CLIPLEN -> BC = length
        ld      d, b
        ld      e, c
        ret

;; unsigned char gb_ui(void);  -> run the paged dialog module, BC=UI_RES -> A (#142)
_gb_ui:
        call    0x80AE          ; GB_UI
        ld      a, c
        ret

;; void gb_fill(u8 col, u8 line, u8 w, u8 h, u8 pen);    -> GB_FILL
;; void gb_frame(u8 col, u8 line, u8 w, u8 h, u8 pen);   -> GB_FRAME
;;   A=col, L=line; stack [ret][w(lone byte)][h,pen(word)]. w is an odd pushed
;;   byte, so the pen sits 1 byte deep: read it with an extra pop + dec sp.
_gb_fill:
        ld      b, a
        ld      c, l
        pop     hl
        ld      (sv_ret), hl
        pop     hl              ; L=w, H=h
        ld      d, l
        ld      e, h
        pop     hl              ; L=pen, H=overshoot
        dec     sp
        ld      a, l
        call    0x8033          ; GB_FILL
        ld      hl, (sv_ret)
        jp      (hl)
_gb_frame:
        ld      b, a
        ld      c, l
        pop     hl
        ld      (sv_ret), hl
        pop     hl
        ld      d, l
        ld      e, h
        pop     hl
        dec     sp
        ld      a, l
        call    0x8021          ; GB_FRAME
        ld      hl, (sv_ret)
        jp      (hl)

;; void gb_icon(u8 slot, u8 col, u8 line);
;;   A=slot, L=col, [SP+2]=line (lone byte). -> A=slot B=col C=line.
_gb_icon:
        ld      b, l            ; B = col (A still holds slot)
        pop     hl
        ld      (sv_ret), hl
        pop     hl              ; L=line, H=overshoot
        dec     sp
        ld      c, l
        call    0x8030          ; GB_ICON
        ld      hl, (sv_ret)
        jp      (hl)

;; void gb_icon_half(u8 slot, u8 col, u8 line);   half-height (middle band) blit of a
;; given slot. A=slot, L=col, [SP+2]=line -> A=slot B=col C=line. (#103)
_gb_icon_half:
        ld      b, l            ; B = col (A still holds slot)
        pop     hl
        ld      (sv_ret), hl
        pop     hl              ; L=line, H=overshoot
        dec     sp
        ld      c, l
        call    0x809F          ; GB_ICONHALF
        ld      hl, (sv_ret)
        jp      (hl)

;; void gb_curshow(void); / gb_curhide(void);
_gb_curshow:
        jp      0x801B          ; GB_CURSHOW
_gb_curhide:
        jp      0x8024          ; GB_CURHIDE

;; unsigned char gb_poll(void);   -> flags in A. The kernel also publishes the
;; result (mx/my/flags) to fixed low RAM, so gb_mx/gb_my/gb_flags read it without
;; re-polling - and a window-manager callback (gb_wm_run) can too.
_gb_poll:
        call    0x801E          ; GB_POLL -> D=flags (+ #1306/7/8 low RAM)
        ld      a, d
        ret
;; unsigned char gb_mx(void); / gb_my(void); / gb_flags(void);  read the last poll
_gb_mx:
        ld      a, (0x1306)     ; POLL_MX
        ret
_gb_my:
        ld      a, (0x1307)     ; POLL_MY
        ret
_gb_flags:
        ld      a, (0x1308)     ; POLL_FLAGS
        ret

;; char *gb_dir1(void); / char *gb_dirn(void);   -> name ptr in DE, 0 at end
_gb_dir1:
        call    0x8012          ; GB_DIR1
        jr      nc, gd_none
        ex      de, hl
        ret
_gb_dirn:
        call    0x8015          ; GB_DIRN
        jr      nc, gd_none
        ex      de, hl
        ret
gd_none:
        ld      de, #0
        ret

;; void gb_launch(void);   launch the current dir entry; returns when it quits
_gb_launch:
        jp      0x8027          ; GB_LAUNCH

;; void gb_run(const char *name);   name in HL
_gb_run:
        jp      0x802D          ; GB_RUN

;; unsigned int gb_fs_load(char *buf, unsigned int max);   buf=HL, max=DE -> count
_gb_fs_load:
        call    0x803F          ; GB_FSLOAD -> BC = byte count
        ld      d, b
        ld      e, c
        ret

;; unsigned char gb_fs_save(char *buf, unsigned int len);   buf=HL, len=DE -> 1/0
_gb_fs_save:
        call    0x8042          ; GB_FSSAVE -> CF = saved
        ld      a, #0
        ret     nc
        inc     a
        ret

;; unsigned char gb_getkey(void);   -> typed char in A, or 0 if none
_gb_getkey:
        jp      0x8045          ; GB_GETKEY (returns the char in A)

;; void gb_vsync(void);   wait one frame (50 Hz), no pointer/clock side effects
_gb_vsync:
        jp      0x8048          ; GB_VSYNC

;; void gb_on_event(void (*handler)(void));   handler ptr in HL -> kernel stores it
_gb_on_event:
        jp      0x804B          ; GB_ONEVENT

;; void gb_menu(const void *def);   def ptr in HL -> kernel copies + draws the bar
_gb_menu:
        jp      0x804E          ; GB_MENU

;; void gb_set_name(const char *name11);   11-byte 8.3 name in HL -> current file
_gb_set_name:
        jp      0x8051          ; GB_SETNAME

;; void gb_get_name(char *dst11);   copy this window's 11-byte launch name into dst
;; (GB_GETARG returns HL = the name in low RAM, always mapped). For the title bar.
_gb_get_name:
        push    hl              ; HL = dst (first arg)
        call    0x802A          ; GB_GETARG -> HL = name ptr
        pop     de              ; DE = dst
        ld      bc, #11
        ldir
        ret

;; unsigned char gb_pic_open(void);   #164: load the opened file into a borrowed bank + parse
;; the .PIC header -> A=1 (PIC_WB/PIC_H/PIC_OFF set) or A=0 (no bank / not a pic -> in-page).
_gb_pic_open:
        jp      0x803c          ; GB_PICOPEN (was the dead GB_XORFRAME stub)
;; void gb_pic_blit(u8 x, u8 y, u8 wbytes, u8 h, u16 src_off);   blit a region of the picture
;; bank to the screen. Same arg shape as gb_restorerect, last word = src_off not a buffer.
_gb_pic_blit:
        ld      b, a
        ld      c, l
        pop     hl
        ld      (sv_ret), hl
        pop     hl              ; L=wbytes, H=h
        ld      d, l
        ld      e, h
        pop     hl              ; src_off (16-bit)
        call    0x8054          ; GB_PICBLIT (was the dead GB_ONREPAINT stub)
        ld      hl, (sv_ret)
        jp      (hl)
;; void gb_pic_close(void);   release the borrowed picture bank.
_gb_pic_close:
        jp      0x8069          ; GB_PICCLOSE (was the dead GB_WMLAUNCH stub)

;; void gb_restore_parent(void);   repaint the ancestor apps behind the caller
_gb_restore_parent:
        jp      0x8057          ; GB_RESTPAR
; gb_wm_damage(x,y,w,h): set the repaint clip to a damage rect (#153). Pre-swap the
; args so the kernel can store them as two words: C=x B=y (clip_x,clip_y),
; E=w D=h (clip_w,clip_h).
_gb_wm_damage:
        ld      c, a            ; C = x
        ld      b, l            ; B = y
        pop     hl
        ld      (sv_ret), hl
        pop     hl              ; L=w, H=h
        ld      e, l            ; E = w
        ld      d, h            ; D = h
        call    0x80B4          ; GB_WMDAMAGE
        ld      hl, (sv_ret)
        jp      (hl)
; gb_wm_full(): 1 if the app-bank pool is full (no room for another window), else 0.
; A window == one busy bank, so full <=> WM_NWIN >= APP_NPAGES. Reads kernel low RAM
; directly (no kernel call), like gb_mx (#153).
_gb_wm_full:
        ld      a, (0x1437)     ; APP_NPAGES (total app banks)
        ld      b, a
        ld      a, (0x1350)     ; WM_NWIN (live windows = busy banks)
        cp      b               ; CF=1 if WM_NWIN < APP_NPAGES (room)
        sbc     a, a            ; 0xFF if room, 0x00 if full
        inc     a               ; 0 = room, 1 = full
        ret

;; void gb_wm_run(const gb_win_t *desc);   desc in HL -> register the root window
;; and enter the kernel master loop (issue #45). Does not return.
_gb_wm_run:
        jp      0x805A          ; GB_WMRUN
;; void gb_wm_add(const gb_win_t *desc);   desc in HL -> register a co-resident
;; window (called from an app opened with gb_wm_open); returns to the opener.
_gb_wm_add:
        jp      0x805D          ; GB_WMADD
;; void gb_wm_managed(const gb_mwin_t *desc);   desc in HL -> register a kernel-managed
;; window (#146): the WM owns the chrome, the app provides content hooks.
_gb_wm_managed:
        jp      0x80B1          ; GB_WMMANAGED
;; The live window rect, published by the WM into MW_RECT (#1448) before each managed
;; hook; the app reads it (gb_wm_x/y/w/h).  (char return in A, like gb_mx.)
_gb_wm_x:
        ld      a,(0x1448)
        ret
_gb_wm_y:
        ld      a,(0x1449)
        ret
_gb_wm_w:
        ld      a,(0x144A)
        ret
_gb_wm_h:
        ld      a,(0x144B)
        ret
;; void gb_wm_open(const char *name);   8.3 name in HL -> open an app as a new
;; co-resident window (non-blocking); returns once it has registered.
_gb_wm_open:
        jp      0x8060          ; GB_WMOPEN
;; void gb_wm_close(void);   close the calling window (return from on_frame after).
_gb_wm_close:
        jp      0x8063          ; GB_WMCLOSE
;; void gb_wm_setpos(u8 x, u8 y);   A=x, L=y -> move the focused window's hit rect
;; (call after dragging so click-to-focus follows the window).
_gb_wm_setpos:
        jp      0x8066          ; GB_WMSETPOS
;; void gb_wm_setsize(u8 w, u8 h);   A=w, L=h -> resize the focused window's hit rect
;; (call after a resize-drag so click-to-focus + restack use the new size, #81).
_gb_wm_setsize:
        jp      0x8099          ; GB_WMSETSIZE
;; void gb_wm_launch_as(const char *app);   app (8.3 name) in HL -> open it as a
;; co-resident window with the current dir entry as its file arg (#70).
_gb_wm_launch_as:
        jp      0x808A          ; GB_WMLAUNCHAS
;; void gb_time(void);   refresh GB_TIME_BUF (#1240: raw h, m, s, binmode) from the clock
_gb_time:
        jp      0x808D          ; GB_TIME
;; void gb_exit(void);   leave GEOBENCH and return to AMSDOS (does not return) (#74)
_gb_exit:
        jp      0x8090          ; GB_EXIT
;; unsigned char gb_isdir(void);   1 if the last dir entry is a directory, else 0
_gb_isdir:
        jp      0x806C          ; GB_ISDIR
;; void gb_chdir(void);   descend into the positioned dir entry (#54)
_gb_chdir:
        jp      0x806F          ; GB_CHDIR
;; void gb_back(void);    up to the parent directory
_gb_back:
        jp      0x8072          ; GB_BACK
;; char *gb_entname(void);  the current dir entry's raw 11-byte 8.3 name (with ext)
_gb_entname:
        call    0x8075          ; GB_ENTNAME -> HL = fs_ent_name
        ex      de, hl
        ret

;; unsigned char gb_drag_start(const char *name);  name (11-byte 8.3) in HL ->
;; start a drag-and-drop of that item; blocks following the pointer until release.
;; Returns 1 if dropped on another window (it handled it), 0 if it was just a click.
_gb_drag_start:
        jp      0x8078          ; GB_DRAGSTART (returns 1/0 in A)

;; unsigned char gb_file_delete(const char *name);  name (11-byte 8.3) in HL ->
;; delete that file from the current directory. Returns 1 if deleted, else 0.
_gb_file_delete:
        call    0x807B          ; GB_FSDELETE -> CF = deleted
        ld      a, #0
        ret     nc
        inc     a
        ret

;; unsigned char gb_drives(void);  probe drives -> bitmask in A:
;;   bit0 = floppy A, bit1 = floppy B, bit2 = IDE (Disk C)
_gb_drives:
        jp      0x807E          ; GB_DRIVES (returns the mask in A)

;; void gb_set_drive(unsigned char d);  d in A -> select the active drive
;;   (0 = IDE/Disk C, 1 = floppy A, 2 = floppy B)
_gb_set_drive:
        jp      0x8081          ; GB_SETDRIVE

;; unsigned char gb_get_drive(void);  -> the current active drive in A
_gb_get_drive:
        jp      0x8084          ; GB_GETDRIVE

;; void gb_copy_begin(void);  switch the active drive+dir to the dragged file's source
;; (captured at drag start), saving the caller's (target) context. Pair with gb_copy_end.
_gb_copy_begin:
        jp      0x8087          ; GB_COPYBEGIN
;; void gb_copy_end(void);  restore the caller's (target) drive+dir saved by gb_copy_begin
_gb_copy_end:
        jp      0x8093          ; GB_COPYEND
;; void gb_on_bar(void (*handler)(void));  handler in HL -> the WM runs it every frame
;; in the desktop's page to draw the top bar, regardless of focus (#77). 0 to clear.
_gb_on_bar:
        jp      0x8096          ; GB_ONBAR
