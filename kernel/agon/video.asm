; The launcher and Settings use the same two profiles. Never enter here while
; guest windows own bitmap snapshots from the previous screen mode.
video_pending   ld a,(AG_WANT_VGA)
                ld hl,AG_VGA
                cp (hl)
                ret

configure_video ld a,(AG_WANT_VGA)
                ld (AG_VGA),a
                ld hl,profile_320
                or a
                jr z,video_setup
                ld hl,profile_640
video_setup     ld de,last_col
                ld bc,5              ; inclusive pointer bounds and desktop clock column
                ldir
                ld a,(hl)
                ld (status_line+6),a
                ld (files_setup+2),a
                ld (calc_error+2),a
                ld (fm_error+2),a
                ld (logo_error+2),a
                inc hl
                ld a,(hl)
                ld (screen_setup+1),a
                inc hl
                ld de,plot_logo+8
                ld bc,4
                ldir
                ld a,8
                call mos
                db LIL
                res 4,(ix+4)
                ld hl,screen_setup
                ld bc,setup_end-screen_setup
                call vdu
                ld a,(AG_VGA)
                or a
                ret z
                ld b,75              ; bounded mode acknowledgement from the VDP
video_wait      db LIL
                bit 4,(ix+4)
                jr nz,video_reply
                halt
                djnz video_wait
                jr video_fallback
video_reply     db LIL
                ld l,(ix+#0f)
                db LIL
                ld h,(ix+#10)
                ld de,640
                or a
                sbc hl,de
                jr nz,video_fallback
                db LIL
                ld l,(ix+#11)
                db LIL
                ld h,(ix+#12)
                ld de,480
                or a
                sbc hl,de
                jr nz,video_fallback
                db LIL
                ld a,(ix+#15)
                cp 16
                ret z
video_fallback  xor a
                ld (AG_WANT_VGA),a
                call configure_video
                ld hl,video_warning
                ld bc,video_warning_end-video_warning
                jp vdu

profile_320     dw 319,239
                db 68,29,9
                dw (320-LOGO_W)/2,(240-LOGO_H)/2
profile_640     dw 639,479
                db 148,59,0
                dw (640-LOGO_W)/2,(480-LOGO_H)/2
last_col        dw 0
last_line       dw 0
clock_col       db 0
video_warning   db 31,0,29," VGA unavailable; using 320x240.       "
video_warning_end
logo_error      db 31,0,29," Cannot switch: missing/invalid logo.  "
logo_error_end
