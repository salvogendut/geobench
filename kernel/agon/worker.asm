; One pure Z80 computation at a time. Kernels, app UI callbacks and the VDP
; are never interrupted by the scheduler. MBASE stays at the MOS user segment.
                include "../../lib/gbapp.inc"
                include "input.inc"
                org AG_PROBE
                jp start_demo
                jp stop_probe
                jp slice
                jp shutdown
                jp start_job         ; HL=worker function, DE=published row byte
                db "ATD2"
                jp key_init
                jp key_sample
                jp key_pop
store_page      equ AG_SYSTEM+60
fetch_page      equ AG_SYSTEM+63
visible         equ AG_SYSTEM+66
slot            equ #17e6
active          db 0
owner           db 0
done            db 0
progress        dw 0
counter         dw 0
old_vector      defs 3
root_sp         dw 0
worker_sp       dw 0
last_count      dw 0
report_tick     db 0
caption         db "Task "
digits          db "0000",0

start_demo      ld de,0
start_job       ld a,(active)
                or a
                jr nz,busy
                ld a,h               ; entry must belong to the requesting guest
                cp #40
                jr c,busy
                cp #7e
                jr nc,busy
                db #ed,#38,#8f       ; timer 5 must not already be in use
                and #41
                jr nz,busy
                ld (resume+1),hl
                ld (progress),de
                ld a,(slot)
                ld (owner),a
                ld hl,#ffff
                ld (last_count),hl
                inc hl
                ld (counter),hl
                di
                ld (root_sp),sp
                ld sp,#8000          ; both guests reserve top 512 bytes at link time
                ld de,finished
                push de
                ld b,10              ; 10 saved pairs, including alternate set
init_stack      push hl
                djnz init_stack
                ld (worker_sp),sp
                ld sp,(root_sp)
                ei
                db #ed,#6e           ; LD A,MB
                ld (handler_addr+4),a
                ld (save_old+4),a
                ld (load_old+4),a
handler_addr    db #5b,#21           ; LD.LIL HL,irq
                dw irq
                db 0
                ld e,#14
                ld a,#14             ; mos_setintvector
                db #49
                rst 8
save_old        db #5b,#22
                dw old_vector
                db 0
                xor a
                ld (done),a
                inc a
                ld (active),a
                ret
busy            xor a
                ret

stop_probe      ld a,(owner)
                ld hl,slot
                cp (hl)
                ret nz
shutdown        ld a,(active)
                ld hl,done
                or (hl)
                ret z
                ld a,(active)
                or a
                call nz,detach
                xor a
                ld (done),a
                ifdef AGON_TASK_PROBE
                ld bc,#3200
                ld de,#1008
                ld a,1
                jp GB_FILL
                else
                ret
                endif
detach          xor a
                ld (active),a
                db #ed,#39,#8f
load_old        db #5b,#2a
                dw old_vector
                db 0
                ld e,#14
                ld a,#14
                db #49
                rst 8
                ret

slice           ld a,(active)
                or a
                ifdef AGON_TASK_PROBE
                jr nz,dispatch
                ld a,(done)
                or a
                jp nz,report
                endif
                ret z
dispatch
                ; Preserve the root's index and alternate registers. The
                ; caller (input_idle) already treats AF/BC/DE/HL as volatile.
                db #5b
                push ix
                db #5b
                push iy
                ex af,af'
                push af
                ex af,af'
                exx
                db #5b
                push bc
                db #5b
                push de
                db #5b
                push hl
                exx
                ld a,(owner)
                ld hl,slot
                cp (hl)
                jr z,mapped
                ld a,(hl)
                call store_page
                call key_sample
                ld a,(owner)
                call fetch_page
                call key_sample
mapped          db #5b
                push ix              ; MOS sysvars for samples on the return trip
                di
                ld (root_sp),sp
                ld sp,(worker_sp)
                xor a
                db #ed,#39,#90
                ld a,64              ; 16384 timer ticks /4, ~3.6 ms one-shot
                db #ed,#39,#91
                ld a,#43
                db #ed,#39,#8f
                pop iy
                pop ix
                exx
                pop hl
                pop de
                pop bc
                exx
                ex af,af'
                pop af
                ex af,af'
                pop hl
                pop de
                pop bc
                pop af
                ei
resume          jp 0

; IRQ return lands here on the worker's own SPS, with every register intact.
reclaimed       di
                ld (counter),hl      ; TASKDEMO's INC HL count
                push af
                push bc
                push de
                push hl
                ex af,af'
                push af
                ex af,af'
                exx
                push bc
                push de
                push hl
                exx
                push ix
                push iy
                ld (worker_sp),sp
                jr leave_worker
finished        di                   ; ordinary return from a completed job
                xor a
                db #ed,#39,#8f
                inc a
                ld (done),a
leave_worker    ld sp,(root_sp)
                db #5b
                pop ix
                ei
                call key_sample
                ld hl,(progress)
                ld a,h
                or l
                jr z,restore_page
                ld l,(hl)
                ld h,0
                ld (counter),hl      ; read publication before unmapping owner
restore_page    ld a,(owner)
                ld hl,slot
                cp (hl)
                jr z,restored
                call store_page
                call key_sample
                ld a,(slot)
                call fetch_page
                call key_sample
restored        ld a,(done)
                or a
                call nz,detach       ; release timer even if the window stays open
                exx
                db #5b
                pop hl
                db #5b
                pop de
                db #5b
                pop bc
                exx
                ex af,af'
                pop af
                ex af,af'
                db #5b
                pop iy
                db #5b
                pop ix
                ifdef AGON_TASK_PROBE
report          call visible
                or a
                ret z
                ld a,(AG_NOW)
                ld hl,report_tick
                sub (hl)
                cp 10                ; cap VDP status traffic at ten updates/s
                ret c
                ld a,(AG_NOW)
                ld (hl),a
                ld de,(counter)
                ld hl,(last_count)
                or a
                sbc hl,de
                ret z
                ld (last_count),de
                ld hl,digits
                ld b,4
digit_loop      ld a,d
                rrca
                rrca
                rrca
                rrca
                and 15
                add a,'0'
                cp '9'+1
                jr c,store_digit
                add a,7
store_digit     ld (hl),a
                inc hl
                sla e
                rl d
                sla e
                rl d
                sla e
                rl d
                sla e
                rl d
                djnz digit_loop
                ld hl,caption
                ld bc,#3200
                ld de,#0201
                jp GB_TEXT
                else
                ret
                endif

; ADL interrupt vector. SPL has mode,PCL,PCH after the two long pushes.
; A nested firmware IRQ (ADL) or the return trampoline isn't guest code.
irq             push af
                push ix
                db #dd,#21,0,0,0
                add ix,sp
                db #ed,#38,#8f
                ld a,(ix+6)
                cp 2
                jr nz,again
                ld a,(ix+8)
                cp #40
                jr c,again
                cp #80
                jr nc,again
                db #40               ; LD.SIS through unchanged MBASE
                ld (resume+2),a
                ld a,(ix+7)
                db #40
                ld (resume+1),a
                ld (ix+7),reclaimed & 255
                ld (ix+8),reclaimed >> 8
                xor a
                jr set_ctl
again           ld a,#43
set_ctl         db #ed,#39,#8f
                pop ix
                pop af
                ei
                db #5b,#ed,#4d       ; RETI.L restores interrupted mode

; The VDP can send both edges during a background page switch. Sampling at
; the individual copy boundaries keeps keydown (including ASCII) available
; after a later keyup. Only the root accesses this queue; no new MOS vector.
key_seq         db 0
key_rd          db 0
key_wr          db 0
key_buf         defs 32
key_init        ld (key_seq),a
                xor a
                ld (key_rd),a
                ld (key_wr),a
                ret
key_sample      push hl
                push bc
                push de
                di
                db #5b
                ld b,(ix+#19)
                db #5b
                ld c,(ix+#18)
                db #5b
                ld e,(ix+#17)
                db #5b
                ld d,(ix+5)
                ei
                ld a,(key_seq)
                cp b
                jr z,key_ready
                ld a,b
                ld (key_seq),a
                ld a,(key_wr)
                ld l,a
                add a,4
                and 31
                ld b,a
                ld a,(key_rd)
                cp b
                jr z,key_ready
                ld a,b
                ld (key_wr),a
                ld h,0
                ld a,c
                ld bc,key_buf
                add hl,bc
                ld (hl),a
                inc hl
                ld (hl),e
                inc hl
                ld (hl),d
key_ready       ld a,(key_rd)
                ld hl,key_wr
                sub (hl)
                pop de
                pop bc
                pop hl
                ret
key_pop         ld a,(key_rd)
                ld hl,key_wr
                cp (hl)
                ret z                ; empty, carry clear
                ld l,a
                ld h,0
                add a,4
                and 31
                ld (key_rd),a
                ld bc,key_buf
                add hl,bc
                ld c,(hl)
                inc hl
                ld e,(hl)
                inc hl
                ld d,(hl)
                scf
                ret
                assert $ <= #3e00    ; system clipboard starts here
