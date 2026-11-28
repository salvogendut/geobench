; Both devices drive one VDP sprite and the normal GB pointer coordinates.
; Do not wait for a mouse acknowledgement: a machine without one must boot.
input_init      ld hl,pointer_setup
                ld bc,pointer_setup_end-pointer_setup
                call vdu
                ld a,8
                call mos
                db LIL
                ld a,(ix+0)
                ld b,a
input_settle    halt
                db LIL
                ld a,(ix+0)
                sub b
                cp 8
                jr c,input_settle
                call mouse_sample
                ld (last_x),hl
                ld (last_y),de
                ld (last_button),a
                db LIL
                ld a,(ix+#19)
                call AG_PROBE+19
                jp pointer_plot

; Read a coherent MOS packet, not a mixture of coordinates across an IRQ.
mouse_sample    di
                db LIL
                ld l,(ix+#29)
                db LIL
                ld h,(ix+#2a)
                db LIL
                ld e,(ix+#2b)
                db LIL
                ld d,(ix+#2c)
                db LIL
                ld a,(ix+#2f)        ; a button-only report can contain the
                db LIL               ; device's unsynchronised initial position
                or (ix+#30)
                db LIL
                or (ix+#31)
                db LIL
                or (ix+#32)
                ld c,a
                db LIL
                ld a,(ix+#2d)
                ei
                and 1
                ret

input_wait      db LIL
                push ix
                ld a,8
                call mos
                xor a
                ld (POLL_FLAGS),a
input_again     call mouse_sample
                push af
                ld a,c
                or a
                jr nz,mouse_compare
                ld (last_x),hl       ; anchor the device, don't warp the sprite
                ld (last_y),de
                jr mouse_button
mouse_compare
                ld bc,(last_x)
                ld (last_x),hl
                or a
                sbc hl,bc
                jr nz,mouse_move
                ld hl,(last_y)
                or a
                sbc hl,de
                jr z,mouse_button
mouse_move      ld (last_y),de
                ld hl,(last_x)
                call pointer_move
mouse_button    pop bc               ; B = sampled physical button
                ld a,(last_button)
                xor b
                and b                ; only the rising edge is a click
                ld a,b
                ld (last_button),a
                jr nz,input_click

                call keyboard_pointer
                call AG_PROBE+22
                call AG_PROBE+25      ; dequeue one packet: C=state, D=ASCII, E=VK
                jr nc,input_idle
                bit 0,c
                jr nz,input_keydown
                ld a,(space_vk)
                cp e
                jr nz,input_again
                xor a
                ld (space_vk),a
                jr input_again
input_idle      halt                 ; pump the RTC and saver even inside a guest
                ; A key may arrive during HALT. Service it before spending a
                ; quantum on a different guest (MOS keeps the latest packet).
                call AG_PROBE+22
                or a
                jp nz,input_again
                db LIL
                push ix
                call AG_PROBE+6
                db LIL
                pop ix
                xor a
                call system_input
                cp 253
                jr z,input_deliver
                jp input_again
input_keydown   ld a,d
                or a
                jp z,input_again
                cp ' '
                jr nz,input_arrows
                ld a,(space_vk)
                or a
                jp nz,input_again    ; a held click must not start typing on refocus
                db LIL
                bit 1,(ix+6)         ; Shift+Space always emulates a primary click
                jr nz,input_space
                push de              ; keep the virtual key for click release
                call AG_TEXT_FOCUS   ; ordinary Space belongs to the focused editor?
                pop de
                or a
                jr z,input_space
                ld a,254             ; text Space, without GB_FIRE or a click timestamp
                jr input_return
input_space
                ld a,e
                ld (space_vk),a
input_click     di
                db LIL
                ld l,(ix+0)
                db LIL
                ld h,(ix+1)
                db LIL
                ld e,(ix+2)
                db LIL
                ld d,(ix+3)
                ei
                ld (AG_CLICK_TIME),hl
                ld (AG_CLICK_TIME+2),de
                ld a,5               ; GB_CLICK | GB_FIRE
                ld (POLL_FLAGS),a
                ld a,' '
input_return    call system_input
                or a
                jr nz,input_deliver
                ld (POLL_FLAGS),a    ; do not leak an accessory click to the guest
                jp input_again
input_deliver
                db LIL
                pop ix
                ret
system_input    db LIL
                ld l,(ix+0)
                db LIL
                ld h,(ix+1)
                ld (AG_NOW),hl
                db LIL
                push ix
                call AG_SYSTEM+3
                db LIL
                pop ix
                ret

; Motion is sampled independently of ASCII repeat; do not pass arrows to apps.
input_arrows    cp 21
                jp z,input_again
                cp 8
                jr nz,input_down
                ld a,e               ; use this packet, not a later held-key sample
                cp 132               ; fabgl::VK_BACKSPACE (Left also sends ASCII 8)
                jp nz,input_again
                ld a,8
                jr input_return
input_down
                cp 10
                jp z,input_again
                cp 11
                jr nz,input_return
                jp input_again

; MOS's held-key bitmap also captures diagonals and releases if the app was
; busy. A tap nudges one pixel; a held key moves on centisecond ticks, without
; accumulating a jump after an app redraw. Shift is an explicit coarse step.
keyboard_pointer db LIL
                push ix
                ld a,#1e
                call mos
                ld c,0
                db LIL
                bit 1,(ix+3)         ; INKEY(-26): Left
                jr z,kp_right
                set 0,c
kp_right        db LIL
                bit 1,(ix+15)        ; INKEY(-122): Right
                jr z,kp_up
                set 1,c
kp_up           db LIL
                bit 1,(ix+7)         ; INKEY(-58): Up
                jr z,kp_down
                set 2,c
kp_down         db LIL
                bit 1,(ix+5)         ; INKEY(-42): Down
                jr z,kp_sampled
                set 3,c
kp_sampled      db LIL
                pop ix
                ld a,(arrow_mask)
                ld b,a
                ld a,c
                ld (arrow_mask),a
                or a
                ret z
                cp b
                db LIL
                ld a,(ix+0)          ; low byte suffices for intervals <= 8 cs
                ld hl,arrow_tick
                jr nz,kp_first
                sub (hl)
                ld b,a
                ld a,(arrow_steps)
                or a
                ld a,b
                jr nz,kp_regular
                cp 8                 ; a quick tap remains exactly one pixel
                ret c
kp_regular      cp 2
                ret c
                ld a,(arrow_steps)
                cp 24
                jr nc,kp_time
                inc a
                jr kp_time
kp_first        xor a
kp_time         ld (arrow_steps),a
                db LIL
                ld b,(ix+0)
                ld (hl),b
                ld b,1
                cp 8
                jr c,kp_stride
                inc b
                cp 24
                jr c,kp_stride
                inc b
kp_stride       db LIL
                bit 1,(ix+6)         ; Shift
                jr z,kp_move
                ld b,8
kp_move         ld a,c
                ld c,b
                ld b,0
                ld hl,(pointer_x)
                push af
                and 3
                call kp_axis
                ex de,hl
                ld hl,(pointer_y)
                pop af
                rrca
                rrca
                and 3
                call kp_axis
                ex de,hl
                jp pointer_move
kp_axis         cp 2                 ; 0 or both directions: unchanged
                jr z,kp_add
                cp 1
                ret nz
                or a
                sbc hl,bc
                ret nc
                ld hl,0
                ret
kp_add          add hl,bc
                ret

pointer_move    ld a,(AG_MOTION)
                inc a
                ld (AG_MOTION),a
                ld bc,319
                or a
                sbc hl,bc
                jr c,pointer_x_ok
                ld hl,0
pointer_x_ok    add hl,bc
                ld (pointer_x),hl
                ld a,d
                or a
                jr nz,pointer_y_max
                ld a,e
                cp AG_LINES
                jr c,pointer_y_ok
pointer_y_max   ld e,AG_LINES-1
pointer_y_ok    ld d,0
                ld (pointer_y),de
pointer_plot    ld hl,(pointer_x)
                srl h
                rr l
                srl h
                rr l
                ld a,l
                ld (POLL_MX),a
                ld a,(pointer_y)
                ld (POLL_MY),a
                ld hl,pointer_packet
                ld bc,pointer_end-pointer_packet
                jp vdu

; Window gestures and scrollbar thumbs own input until the button comes up.
; Sample movement on release too, without redrawing a stationary pointer.
input_held      db LIL
                push ix
                ld a,8
                call mos
                halt
                call mouse_sample
                ld b,a
                ld a,(last_button)
                or b
                ld a,b
                ld (last_button),a
                jr z,held_keyboard
                ld a,c
                or a
                jr z,held_button
                ld bc,(last_x)
                or a
                sbc hl,bc
                add hl,bc
                jr nz,held_move
                push hl
                ld hl,(last_y)
                or a
                sbc hl,de
                pop hl
                jr z,held_button
held_move
                ld (last_x),hl
                ld (last_y),de
                call pointer_move
held_button     ld a,(last_button)
                or a
                jr nz,held_fire
held_keyboard   call keyboard_pointer
                ld a,#1e
                call mos
                db LIL
                bit 2,(ix+12)        ; Space in the held-key map
                jr nz,held_fire
                xor a
                ld (space_vk),a
                jr held_done
held_fire       ld a,4
held_done       db LIL
                pop ix
                ld (POLL_FLAGS),a
                ld d,a
                ld a,(POLL_MX)
                ld b,a
                ld a,(POLL_MY)
                ld c,a
                ld a,d
                ret

last_x          dw 0
last_y          dw 0
last_button     db 0
space_vk        db 0
key_count       db 0
arrow_mask      db 0
arrow_steps     db 0
arrow_tick      db 0
pointer_setup   db 23,0,#c0,0        ; MOS mouse positions in physical pixels
                db 23,0,#89,0        ; accept PS/2 packets if available
                db 23,0,#89,3,#ff,#ff ; suppress native pointer, not input
                db 23,0,#a0
                dw POINTER_ID
                db 2,23,0,#a0
                dw POINTER_ID
                db 0
                dw 64
                db #c0,0,0,0,0,0,0,0
                db #c0,#c0,0,0,0,0,0,0
                db #c0,#ff,#c0,0,0,0,0,0
                db #c0,#ff,#ff,#c0,0,0,0,0
                db #c0,#ff,#ff,#ff,#c0,0,0,0
                db #c0,#ff,#ff,#ff,#ff,#c0,0,0
                db #c0,#ff,#c0,#c0,#c0,#c0,#c0,0
                db #c0,#c0,0,0,0,0,0,0
                db 23,27,#20
                dw POINTER_ID
                db 23,27,#21
                dw 8,8
                db 1,23,27,4,0,23,27,5,23,27,#26
                dw POINTER_ID
                db 23,27,7,1,23,27,11
pointer_setup_end
pointer_packet  db 23,27,13
pointer_x       dw 16
pointer_y       dw 16
                db 23,27,15
pointer_end
