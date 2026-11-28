; Standalone MOS program, running Z80 code in the user segment.
; RASM is a Z80 assembler. The mode prefixes below apply to the immediately
; following instruction: LIS for mixed-mode MOS calls/return, LIL for full
; register saves and absolute addressing through MOS's 24-bit sysvars pointer.
LIS             equ #49
LIL             equ #5b
LOGO_W          equ 176
LOGO_H          equ 176
PIC_SIZE        equ 14 + LOGO_W/4 * LOGO_H
BITMAP_ID       equ #4742
POINTER_ID      equ #4744
                include "../../lib/gbapp.inc"
PREEMPTIVE      equ 0
                include "../lowram.inc"
                include "input.inc"
                include "../../build/agon/ist.inc"

                org 0
                jp start
                assert $ == AG_WAIT_INPUT
                jp input_wait
                assert $ == AG_LOAD_CALC
                jp read_calc_at
                assert $ == AG_POLL_HELD
                jp input_held
                assert $ == AG_LOAD_PREVIEW
                jp read_preview
                defs #40-$
                db "MOS",0,0          ; header v0, run in Z80 mode
start
                ld (entry_sp),sp
                ld sp,#ff00           ; MOS supplies an ADL stack, not a Z80 stack
                db LIL
                push ix
                db LIL
                push iy
                ld hl,gesture_image
                ld de,#f240
                ld bc,gesture_end-gesture_image
                ldir
                ld hl,runtime_image
                ld de,GB_KERNEL
                ld bc,runtime_end-runtime_image
                ldir
                ld hl,files_image
                ld de,AG_FILES
                ld bc,files_end-files_image
                ldir
                ld hl,system_image
                ld de,AG_SYSTEM
                ld bc,system_end-system_image
                ldir
                ld hl,probe_image
                ld de,AG_PROBE
                ld bc,probe_end-probe_image
                ldir
                call read_logo        ; guest page is free after relocation
                jp nz,file_error
                ifdef AGON_VGA
                ld a,1                ; compatibility launcher: default without AGON.CFG
                else
                xor a
                endif
                call AG_SYSTEM+81     ; load saved settings, including requested mode
                ld a,8
                call mos
                db LIL
                ld a,(ix+#27)
                ld (restore_mode),a   ; restore original MOS mode even after several changes
                jr enter_video
change_video    call read_logo        ; no guest windows open; leave old display intact on failure
                jr z,release_display
                ld a,(AG_VGA)
                ld (AG_WANT_VGA),a
                ld hl,logo_error
                ld bc,logo_error_end-logo_error
                jp paint
release_display call AG_FILES+33      ; release old glyphs and icons before resetting VDP
enter_video     call configure_video
                call upload_logo
                ld hl,16
                ld (pointer_x),hl
                ld (pointer_y),hl
                call input_init
                xor a
                ld (AG_FILEMODE),a
                ld (WM_NWIN),a
                ld (AG_YBASE),a
                ld (AG_HHI),a
                call AG_FILES+36     ; initialise the VDP icon cache
                call AG_WM           ; start with no saved window backgrounds
                call load_icons
                call desktop_disk
                call AG_SYSTEM       ; initialise preferences and draw the top bar
                ld a,(visible)
                or a
                jr z,wait_key
                jr show_logo
toggle_logo     xor 1
                ld (visible),a
                jr z,hide_logo
show_logo       ld hl,plot_logo
                ld bc,plot_end-plot_logo
                jr paint
hide_logo       ld hl,(plot_logo+8)
                srl h
                rr l
                srl h
                rr l
                ld b,l
                ld a,(plot_logo+10)
                ld c,a
                ld de,#2cb0           ; 176 x 176
                xor a
                call GB_FILL          ; viewport CLS would deactivate the sprite
                jr wait_key
paint
                call vdu
wait_key        call video_pending
                jp nz,change_video
                call AG_WAIT_INPUT
                cp 27
                jp z,leave
                cp ' '
                jr nz,desktop_key
                ld a,(POLL_MY)
                cp 16
                jr c,wait_key
                cp 58
                jr nc,wait_key
                ld a,(clock_col)
                ld b,a
                ld a,(POLL_MX)        ; icon and caption share a hit area
                sub b
                jr c,disk_hit
                cp 8
                jr nc,wait_key
                ld a,'T'
                jr accessory
disk_hit        ld a,(POLL_MX)
                sub 2
                cp 8
                jp c,open_manager
                jr wait_key
desktop_key     and #df
                cp 'S'
                jr z,accessory
                cp 'T'
                jr z,accessory
                cp 'V'
                jr z,accessory
                cp 'F'
                jp z,open_manager
                cp 'B'
                jr z,open_files
                cp 'C'
                jr z,open_calc
                cp 'W'
                jr nz,wait_key
                ld a,(visible)
                jp toggle_logo
accessory       call AG_SYSTEM+6
                jp wait_key
open_calc       call read_calc
                jr nz,app_error
                call GB_INIT          ; single-app dispatcher; returns after close
restore_desktop
                call video_pending    ; all document windows have now closed
                jp nz,change_video
                call desktop_disk
                ld hl,status_line
                ld bc,setup_end-status_line
                call vdu
                ld a,(visible)
                or a
                jp z,wait_key
                ld hl,plot_logo
                ld bc,plot_end-plot_logo
                jp paint
app_error       ld hl,calc_error
                ld bc,calc_error_end-calc_error
                jp paint
open_files      xor a
files_loop      push af
                ld hl,files_setup
                ld bc,files_setup_end-files_setup
                call vdu
                pop af
                call AG_FILES         ; SDCC: A = mode, DE = path or NULL
                ld a,d
                or e
                jr z,close_files
                ex de,hl
                call read_calc_at
                ld a,2
                jr nz,files_loop
                call GB_INIT
                ld a,1
                jr files_loop
close_files     ld bc,#0810           ; browser rectangle, x in 4-pixel columns
                ld de,#40a8
                xor a
                call GB_FILL
                jr restore_desktop
desktop_disk    ld a,(icons_loaded)
                or a
                jr z,disk_caption
                ld a,13              ; portable .IST SD card slot
                ld bc,#0210
                call GB_ICON
                ld a,(clock_col)
                ld b,a
                ld c,16
                ld a,1               ; portable analog clock slot
                call GB_ICON
                ld hl,clock_text
                ld de,#0100
                ld a,(clock_col)
                ld b,a
                ld c,50
                call GB_TEXT
disk_caption    ld hl,disk_text
                ld de,#0100
                ld bc,#0432
                jp GB_TEXT
load_icons      ld a,(icons_loaded)
                or a
                ret nz
                ld hl,ist_path
                ld de,#2000
                ld bc,IST_SIZE
                ld ix,IST_CRC
                call read_checked
                ret nz
                ld a,1
                ld (icons_loaded),a
                ret
open_manager    call load_icons
                ld a,(icons_loaded)
                or a
                jr z,manager_error
                ld hl,manager_path
                call AG_SYSTEM+12
                cp 1
                jr nz,manager_error
                call AG_FILES+3      ; new File Manager starts at the MOS root
                ld a,3
                ld (APP_NPAGES),a
                ld a,1
                ld (WM_NWIN),a
                ld (AG_FILEMODE),a
                call GB_INIT
                xor a
                ld (AG_FILEMODE),a
                call AG_FILES+27     ; an enumeration can finish on a match
                jp restore_desktop
manager_error   ld hl,fm_error
                ld bc,fm_error_end-fm_error
                jp paint
leave
                call AG_PROBE+9
                call AG_FILES+33     ; discard bitmap buffers owned by this run
                ld hl,cleanup
                ld bc,cleanup_end-cleanup
                call vdu
                ld hl,0
                jr return_mos
file_error
                ld hl,error_text
                ld bc,error_end-error_text
                call vdu
                ld hl,1
return_mos
                db LIL
                pop iy
                db LIL
                pop ix
                ld sp,(entry_sp)
                db LIS
                ret

mos             db LIS
                rst #08
                ret
vdu             db LIS
                rst #18
                ret

; Load exactly the canonical logo, not an arbitrary-size bitmap. The extra
; byte detects oversized files. No graphics state is touched on failure.
read_logo
                ld hl,logo_path
                ld de,picture
                ld bc,PIC_SIZE
                call read_exact
                ret nz
                ld hl,picture
                ld de,logo_header
                ld b,14
check_header    ld a,(de)
                cp (hl)
                ret nz
                inc de
                inc hl
                djnz check_header
                ret

; HL = absolute filename, DE = destination, BC = expected size. Reserve one
; extra byte at the destination to distinguish an oversized file.
read_exact      push bc
                push de
                ld c,1                ; FA_READ
                ld a,#0a              ; mos_fopen
                call mos
                pop hl
                db LIL,#11,0,0,0     ; LD.LIL DE,0 before the 16-bit pop
                pop de
                or a
                jr z,read_missing
                ld c,a
                push de
                inc de
                ld a,#1a              ; mos_fread: C=handle, DE=actual count
                call mos
                ld a,#0b              ; mos_fclose preserves DE
                call mos
                pop hl
                or a
                sbc hl,de
                ret
read_missing    ld a,1
                or a
                ret

; CRC-16/CCITT-FALSE. This is a compatibility check, not a security boundary.
read_preview    ld hl,preview_path
                call AG_SYSTEM+12
                cp 2
                ret
read_calc       ld hl,calc_path
read_calc_at    call AG_SYSTEM+12
                or a
                ret
read_checked    push ix
                push bc
                push de
                call read_exact
                pop hl
                pop bc
                pop ix
                ret nz
                ld de,#ffff
crc_byte        ld a,(hl)
                inc hl
                xor d
                ld d,a
                push bc
                ld b,8
crc_bit         sla e
                rl d
                jr nc,crc_next
                ld a,d
                xor #10
                ld d,a
                ld a,e
                xor #21
                ld e,a
crc_next        djnz crc_bit
                pop bc
                dec bc
                ld a,b
                or c
                jr nz,crc_byte
                push ix
                pop hl
                sbc hl,de
                ret

; Stream one VDP buffer block, converting just one scanline at a time.
; Pen zero is transparent over the desktop blue. The remaining three pens
; are opaque white, black and red in Agon's ABGR2222 byte order.
upload_logo
                ld hl,upload_start
                ld bc,upload_end-upload_start
                call vdu
                ld hl,picture+14
                ld a,LOGO_H
next_row        push af
                ld de,row_buf
                ld b,LOGO_W/4
next_byte       ld c,(hl)
                inc hl
                push bc
                ld b,4
next_pixel      bit 7,c
                jr z,low_clear
                ld a,#ff
                bit 3,c
                jr z,store
                ld a,#c3
                jr store
low_clear       xor a
                bit 3,c
                jr z,store
                ld a,#c0
store           ld (de),a
                inc de
                sla c
                djnz next_pixel
                pop bc
                djnz next_byte
                push hl
                ld hl,row_buf
                ld bc,LOGO_W
                call vdu
                pop hl
                pop af
                dec a
                jr nz,next_row
                ld hl,define_bitmap
                ld bc,define_end-define_bitmap
                jp vdu

logo_path       db "/PICS/LOGO.PIC",0
calc_path       db "/GBENCH/CALC.APP",0
preview_path    db "/GBENCH/PICVIEW.APP",0
manager_path    db "/GBENCH/FILEMGR.APP",0
ist_path        db "/GBENCH/DEFAULT.IST",0
logo_header     db "GBPC",2,1
                dw LOGO_W,LOGO_H
                db 1,26,0,6
error_text      db "GEOBENCH: missing or invalid /PICS/LOGO.PIC",13,10
error_end
disk_text       db "SD:",0
clock_text      db "Clock",0
screen_setup    db 22,9
                db 23,1,0             ; hide text cursor
                db 17,132,12          ; blue paper, CLS
                db 17,135,17,0,31,0,0
                db " GEOBENCH                    AgonLight2 "
; Leave the bottom-right character untouched: printing there scrolls the VDP.
status_line     db 17,132,17,7,31,0,AG_STATUS_ROW," F: Files  C: Calc  W: Wall  Esc: MOS  "
setup_end
files_setup     db 31,0,AG_STATUS_ROW," Arrows: pointer  Space: click  Esc    "
files_setup_end
calc_error      db 31,0,AG_STATUS_ROW," CALC.APP missing or invalid; C: retry "
calc_error_end
fm_error        db 31,0,AG_STATUS_ROW," Missing/invalid FILEMGR.APP or IST    "
fm_error_end
upload_start    db 23,0,#a0
                dw BITMAP_ID
                db 2                  ; discard any previous block
                db 23,0,#a0
                dw BITMAP_ID
                db 0
                dw LOGO_W*LOGO_H
upload_end
define_bitmap   db 23,27,#20
                dw BITMAP_ID
                db 23,27,#21
                dw LOGO_W,LOGO_H
                db 1                  ; RGBA2222
define_end
plot_logo       db 23,27,#20
                dw BITMAP_ID
                db 23,27,3
                dw 0,0               ; centred by configure_video
plot_end
cleanup         db 23,27,12,23,27,15,23,27,7,0,23,27,5
                db 23,0,#a0
                dw POINTER_ID
                db 2,23,0,#89,3,0,0,23,0,#89,1,23,0,#c0,1
                db 23,0,#a0
                dw BITMAP_ID
                db 2,22
restore_mode    db 0
                db 23,1,1             ; restore visible MOS prompt
cleanup_end
entry_sp        dw 0
visible         db 1
icons_loaded    db 0
                include "input.asm"
                assert $ <= AG_WM
                defs AG_WM-$
                incbin "../../build/agon/window.bin"
                include "video.asm"
runtime_image   incbin "../../build/agon/runtime.bin"
runtime_end
files_image     incbin "../../build/agon/files.bin"
files_end
system_image    incbin "../../build/agon/system.bin"
system_end
probe_image
                ifdef AGON_TASK_PROBE
                incbin "../../build/agon/task_probe.bin"
                else
                incbin "../../build/agon/worker.bin"
                endif
probe_end
gesture_image   incbin "../../build/agon/gesture.bin"
gesture_end
                assert gesture_end < GB_KERNEL
                assert probe_end < GB_KERNEL
; The bootstrap is live below the app-visible low-RAM contracts. The temporary
; runtime image is copied before those contracts or the PIC scratch are used.
                assert runtime_image < #1000
                assert system_end < GB_KERNEL
                assert AG_SYSTEM+system_end-system_image <= #e550
                assert AG_FILES+files_end-files_image <= #a000
                assert #2000+IST_SIZE <= #3500 ; document path and MOS FILINFO follow
picture         equ #4000            ; used only before or between guest lifetimes
row_buf         equ picture+PIC_SIZE+1
                assert GB_KERNEL+runtime_end-runtime_image <= AG_FILES
                assert row_buf+LOGO_W <= GB_KERNEL
