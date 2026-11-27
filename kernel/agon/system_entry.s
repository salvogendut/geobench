        .module system_entry
        .globl _sys_init, _sys_event, _sys_open, _cfg_handle
        .area _CODE
        jp _sys_init
        jp _sys_event
        jp _sys_open
        jp _saver_pixel         ; window snapshot fence; A = probe scanline
_sys_send::
        ld b,d
        ld c,e
        .db 0x49
        rst 0x18
        ret
_sys_rtc::
        ld a,#0x12
        jr mos
_cfg_open::
        ld c,#1
        jr open
_cfg_create::
        ld c,#0x0a              ; FA_CREATE_ALWAYS | FA_WRITE
open:   ld a,#0x0a
        jr mos
_cfg_read::
        ld a,#0x1a
        jr transfer
_cfg_write::
        ld a,#0x1b
transfer:
        ld de,#8
        push af
        ld a,(_cfg_handle)
        ld c,a
        pop af
        jr mos
_cfg_close::
        ld c,a
        ld a,#0x0b
mos:    .db 0x49
        rst 0x08
        ret
_layer_open::
        jp 0x0903
_layer_click::
        jp 0x0906
_layer_close::
        jp 0x0909

; Synchronous point query for MOS 2.x as well as 3.x. The tick deadline
; prevents a VDP with no response from trapping the desktop in the saver.
_saver_pixel::
        ld (pixel_y),a
        .db 0x5b
        push ix
        ld a,#8
        .db 0x49
        rst 0x08
        .db 0x5b
        res 2,4(ix)
        ld hl,#pixel_query
        ld bc,#7
        .db 0x49
        rst 0x18
        .db 0x5b
        ld b,0(ix)
pixel_wait:
        .db 0x5b
        bit 2,4(ix)
        jr nz,pixel_ready
        halt
        .db 0x5b
        ld a,0(ix)
        sub b
        cp #100
        jr c,pixel_wait
        ld a,#255
        jr pixel_done
pixel_ready:
        .db 0x5b
        ld a,0x16(ix)
pixel_done:
        .db 0x5b
        pop ix
        ret
pixel_query:
        .db 23,0,0x84,63,1
pixel_y: .db 0,0
