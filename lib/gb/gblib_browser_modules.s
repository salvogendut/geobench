;; Minimal libgb bindings for the Browser's paged render/decoder modules.
;; Keeping this as one ASxxxx object avoids retaining the many unused stubs in
;; gblib.s and leaves the modules room for bounded page rendering.
        .module gblib_browser_modules
        .globl  _gb_text
        .globl  _gb_textbw
        .globl  _gb_textrev
        .globl  _gb_fill
        .globl  _gb_frame
        .globl  _gb_pic_blit
        .globl  _gb_pic_edit

        .area   _BSS
sv_ret: .ds     2

        .area   _CODE

_gb_text:
        ld      b, a
        ld      c, l
        ld      d, #1
        ld      e, #0
        pop     hl
        ex      (sp), hl
        call    0x800C
        ret

_gb_textbw:
        ld      b, a
        ld      c, l
        ld      d, #2
        ld      e, #1
        pop     hl
        ex      (sp), hl
        call    0x800C
        ret

_gb_textrev:
        ld      b, a
        ld      c, l
        ld      d, #1
        ld      e, #2
        pop     hl
        ex      (sp), hl
        call    0x800C
        ret

_gb_fill:
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
        call    0x8033
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
        call    0x8021
        ld      hl, (sv_ret)
        jp      (hl)

_gb_pic_blit:
        ld      b, a
        ld      c, l
        pop     hl
        ld      (sv_ret), hl
        pop     hl
        ld      d, l
        ld      e, h
        pop     hl
        call    0x8054
        ld      hl, (sv_ret)
        jp      (hl)

_gb_pic_edit:
        jp      0x8048
