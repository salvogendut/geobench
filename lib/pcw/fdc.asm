; ---------------------------------------------------------------------------
; lib/pcw/fdc.asm - polled uPD765A floppy driver for the PCW target (#331).
;
; The resident graduation of kernel/pcwboot.asm's loader, following the
; REAL-CHIP protocol learned from a proven CP/M 3 boot sector + the MCU
; bootstrap (see pcwboot.asm's header):
;   - MSR settle: after every command byte written or result byte read,
;     an EX (SP),HL chain runs before the next MSR poll (the real chip's
;     status stays stale ~12us; polling early double-feeds/double-reads -
;     the emulator updates MSR instantly and never shows it)
;   - seek/recalibrate completion = the ASIC's live FDC INTRQ mirror
;     (port #F8 read, bit 5), then ONE acknowledging SENSE INTERRUPT
;   - transfers: TC clear before the command, READ #66 / WRITE #45 with
;     EOT = 9 (the FDC streams into the gap while we assert TC), TC set
;     right after the payload, then wait INTRQ and read the results
;   - the polled loops keep the count in B (2 x 256) to beat the ~32us
;     MFM byte window
;
; Ports: #00 = main status register, #01 = data register. #F8 cmds:
; 04 = FDC irq routing off (we poll the raw mirror), 05/06 = TC set/clear,
; 09 = motor on. Each sector transfer preserves IFF2 and runs DI: once the
; preemptive PCW timer is installed, even its short ISR can overrun the real
; controller's MFM byte window. Emulator FDCs generally hide that timing bug.
;
;   pcwfdc_init                     SPECIFY (ND=1) + motor + recalibrate
;   pcwfdc_setunit A=0/1            select drive (B double-steps CF2 media)
;   pcwfdc_read  C=side D=track E=sector(1..9) HL=512-byte dest -> CF set = ok
;   pcwfdc_read1                    one attempt, no retry (presence probe)
;   pcwfdc_write C=side D=track E=sector(1..9) HL=512-byte src  -> CF set = ok
;
; The motor is turned on at init and left running.
; ---------------------------------------------------------------------------

pcwfdc_init
                ld    a,4
                out   (PCW_SYSCTL),a          ; irq routing off: we poll #F8 bit5
                ld    a,9
                out   (PCW_SYSCTL),a          ; motor on (stays on)
                ld    a,6
                out   (PCW_SYSCTL),a          ; terminal count clear
                ld    hl,fdc_cmd_spec         ; SPECIFY: timings + ND=1 (polled)
                ld    b,3
                call  fdc_send
                ; fall through: recalibrate = seek to a known track 0

; fdc_recal: home the head and reset the track shadow.
fdc_recal
                ld    hl,fdc_cmd_rcal
                ld    b,2
                call  fdc_send
                call  fdc_wait_seek
                xor   a
                ld    (fdc_track),a
                ret

; pcwfdc_setunit: A = drive unit (0/1) -> select it in every command and
; force a fresh seek. Unit 1's stepping depends on the MECHANISM: an
; 8512-style 80-track CF2DD drive DOUBLE-STEPS 40-track CF2 media (seek
; NCN = 2*track; the READ/WRITE C field stays the media track), but a
; Gotek or a 40-track bolt-on B maps tracks 1:1. pcwfdc_detect measures
; which one is attached; until it has run, assume the classic CF2DD.
pcwfdc_setunit
                ld    (fdc_unit),a
                ld    (fdc_cmd_rcal+1),a
                ld    (fdc_cmd_seek+1),a
                ld    (fdc_rd_u),a
                ld    (fdc_wr_u),a
                or    a                       ; unit 0 never double-steps
                jr    z,psu_dbl
                ld    a,(fdc_dbl_b)           ; unit 1 = the measured B mode
psu_dbl
                ld    (fdc_dbl),a
                ld    a,#FF
                ld    (fdc_track),a
                ret

; pcwfdc_detect: unit 1 selected -> measure the B mechanism's stepping.
; Seek PHYSICAL track 2 and ask READ ID for the on-media cylinder:
;   C = 2  the drive maps tracks 1:1 (Gotek / 40-track mechanism)
;   C = 1  an 80-track CF2DD mechanism double-stepping 40-track media
; (an 80-track CF2DD disc also answers C = 2 = 1:1, which is right).
; Any failure keeps the current mode. Leaves the head position unknown.
pcwfdc_detect
                ld    a,2
                ld    (fdc_sk_trk),a
                ld    hl,fdc_cmd_seek
                ld    b,3
                call  fdc_send
                call  fdc_wait_seek
                ld    hl,fdc_cmd_rdid
                ld    b,2
                call  fdc_send
                call  fdc_result              ; fdc_st: ST0 ST1 ST2 C H R N
                and   #C8                     ; abnormal / not ready: keep mode
                jr    nz,pfd_home
                ld    a,(fdc_st+3)            ; C under physical track 2
                cp    2
                ld    a,1
                jr    nz,pfd_set              ; C=1: double-step mechanism
                dec   a                       ; C=2: 1:1
pfd_set
                ld    (fdc_dbl),a
                ld    (fdc_dbl_b),a
pfd_home
                ld    a,#FF                   ; the head sits on a physical,
                ld    (fdc_track),a           ; not a media, track: re-seek
                ret

; fdc_ncn: the media track fdr_trk -> A = the physical seek target.
fdc_ncn
                ld    a,(fdc_dbl)
                or    a
                ld    a,(fdr_trk)
                ret   z
                add   a,a
                ret

; pcwfdc_read: C = side, D = track, E = sector R, HL = 512-byte destination.
; CF set = sector read. Three attempts, recalibrating between them.
; pcwfdc_read1: one attempt, no retry - for presence probes.
pcwfdc_read1
                ld    a,i                     ; P/V = caller's IFF2
                push  af
                di                            ; atomic through command/data/result
                ld    (fdr_dst),hl
                ld    a,c
                and   1
                ld    (fdr_side),a
                ld    a,d
                ld    (fdr_trk),a
                ld    a,e
                ld    (fdr_sec),a
                ld    b,1
                jr    fdr_try
pcwfdc_read
                ld    a,i                     ; preserve boot-time DI vs runtime EI
                push  af
                di
                ld    (fdr_dst),hl
                ld    a,c
                and   1
                ld    (fdr_side),a
                ld    a,d
                ld    (fdr_trk),a
                ld    a,e
                ld    (fdr_sec),a
                ld    b,3                     ; attempts
fdr_try
                push  bc
                call  fdr_once
                pop   bc
                jp    c,fdc_irq_ok
                push  bc
                call  fdc_recal               ; re-home before retrying
                pop   bc
                djnz  fdr_try
                or    a                       ; NC = hard failure
                jp    fdc_irq_fail

fdr_once
                ld    a,(fdc_track)           ; seek only when the head moves
                ld    hl,fdr_trk
                cp    (hl)
                jr    z,fdo_onspot
                call  fdc_ncn                 ; media -> physical (drive B x2)
                ld    (fdc_sk_trk),a
                ld    hl,fdc_cmd_seek
                ld    b,3
                call  fdc_send
                call  fdc_wait_seek
                ld    a,(fdr_trk)
                ld    (fdc_track),a
fdo_onspot
                ld    a,(fdr_side)           ; command unit/head and H must agree
                ld    (fdc_rd_h),a
                add   a,a
                add   a,a
                ld    c,a
                ld    a,(fdc_unit)
                or    c
                ld    (fdc_rd_u),a
                ld    a,6
                out   (PCW_SYSCTL),a          ; TC clear before the command
                ld    a,(fdr_trk)             ; READ DATA #66: R = the sector,
                ld    (fdc_rd_c),a            ; EOT stays 9 (TC terminates)
                ld    a,(fdr_sec)
                ld    (fdc_rd_r),a
                ld    hl,fdc_cmd_read
                ld    b,9
                call  fdc_send
                ld    hl,(fdr_dst)            ; polled transfer of 512 bytes
                ld    c,1
                ld    d,2                     ; 2 x 256, count in B (byte window)
fdo_half
                ld    b,0
fdo_rx
                in    a,(0)
                add   a,a                     ; RQM -> carry
                jr    nc,fdo_rx
                add   a,a                     ; EXM -> bit7
                jp    p,fdo_bad               ; result phase early = failed
                ini
                jr    nz,fdo_rx
                dec   d
                jr    nz,fdo_half
                ld    a,5                     ; payload in: terminal count
                out   (PCW_SYSCTL),a
                ld    a,6
                out   (PCW_SYSCTL),a
                call  fdc_result              ; wait INTRQ, read results, A = ST0
                and   #88                     ; fatal IC or NR only (EN = TC-normal)
                jr    nz,fdo_fail
                scf
                ret
fdo_bad
                call  fdc_result              ; swallow the error result
fdo_fail
                or    a
                ret

; pcwfdc_write: C = side, D = track, E = sector R, HL = 512-byte source.
; CF set = sector written. Three attempts, recalibrating between them.
pcwfdc_write
                ld    a,i                     ; preserve boot-time DI vs runtime EI
                push  af
                di                            ; writes have the same byte deadline
                ld    (fdr_dst),hl
                ld    a,c
                and   1
                ld    (fdr_side),a
                ld    a,d
                ld    (fdr_trk),a
                ld    a,e
                ld    (fdr_sec),a
                ld    b,3
fdw_try
                push  bc
                call  fdw_once
                pop   bc
                jp    c,fdc_irq_ok
                push  bc
                call  fdc_recal
                pop   bc
                djnz  fdw_try
                or    a
                jp    fdc_irq_fail

fdw_once
                ld    a,(fdc_track)           ; seek only when the head moves
                ld    hl,fdr_trk
                cp    (hl)
                jr    z,fdw_onspot
                call  fdc_ncn                 ; media -> physical (drive B x2)
                ld    (fdc_sk_trk),a
                ld    hl,fdc_cmd_seek
                ld    b,3
                call  fdc_send
                call  fdc_wait_seek
                ld    a,(fdr_trk)
                ld    (fdc_track),a
fdw_onspot
                ld    a,(fdr_side)
                ld    (fdc_wr_h),a
                add   a,a
                add   a,a
                ld    c,a
                ld    a,(fdc_unit)
                or    c
                ld    (fdc_wr_u),a
                ld    a,6
                out   (PCW_SYSCTL),a          ; TC clear before the command
                ld    a,(fdr_trk)             ; WRITE DATA #45: R = the sector,
                ld    (fdc_wr_c),a            ; EOT stays 9 (TC terminates)
                ld    a,(fdr_sec)
                ld    (fdc_wr_r),a
                ld    hl,fdc_cmd_write
                ld    b,9
                call  fdc_send
                ld    hl,(fdr_dst)            ; polled transfer of 512 bytes OUT
                ld    c,1
                ld    d,2
fdw_half
                ld    b,0
fdw_tx
                in    a,(0)
                add   a,a                     ; RQM -> carry
                jr    nc,fdw_tx
                add   a,a                     ; EXM -> bit7
                jp    p,fdw_bad               ; result phase early = aborted
                outi
                jr    nz,fdw_tx
                dec   d
                jr    nz,fdw_half
                ld    a,5                     ; payload out: terminal count
                out   (PCW_SYSCTL),a
                ld    a,6
                out   (PCW_SYSCTL),a
                call  fdc_result
                and   #88
                jr    nz,fdw_fail
                scf
                ret
fdw_bad
                call  fdc_result
fdw_fail
                or    a
                ret

; Restore the interrupt state captured by the public sector entry point while
; returning its carry-only success contract. EI is placed immediately before
; RET so an enabled caller cannot be preempted inside this resident epilogue.
fdc_irq_ok
                pop   af                      ; P/V still holds entry IFF2
                jp    po,fdc_irq_ok_di
                scf
                ei
                ret
fdc_irq_ok_di
                scf
                ret

fdc_irq_fail
                pop   af
                jp    po,fdc_irq_fail_di
                or    a                       ; clear carry
                ei
                ret
fdc_irq_fail_di
                or    a
                ret

; --- primitives (real-chip MSR settle discipline throughout) ----------------

; send B command bytes from (HL): RQM-gated + settle after every byte
fdc_send
                in    a,(0)
                add   a,a
                jr    nc,fdc_send
                ld    a,(hl)
                out   (1),a
                inc   hl
                ex    (sp),hl                 ; ~76 T: let MSR settle before
                ex    (sp),hl                 ; the next poll (real chip)
                ex    (sp),hl
                ex    (sp),hl
                djnz  fdc_send
                ret

; send the single command byte in A
fdc_send1
                push  af
fdc_s1w
                in    a,(0)
                add   a,a
                jr    nc,fdc_s1w
                pop   af
                out   (1),a
                ex    (sp),hl
                ex    (sp),hl
                ex    (sp),hl
                ex    (sp),hl
                ret

; read one result byte -> A (settle after the read)
fdc_res
                in    a,(0)
                add   a,a
                jr    nc,fdc_res
                in    a,(1)
                ex    (sp),hl
                ex    (sp),hl
                ex    (sp),hl
                ex    (sp),hl
                ret

; fdc_result: wait for the command-end INTRQ (raw mirror on #F8 bit 5),
; then read every result byte into fdc_st. Returns A = ST0.
fdc_result
                in    a,(PCW_SYSCTL)
                and   #20
                jr    z,fdc_result
                ld    hl,fdc_st
                ld    b,7
fdc_rs
                in    a,(0)
                bit   4,a                     ; command over (CB clear)?
                jr    z,fdc_rsdone
                add   a,a                     ; RQM?
                jr    nc,fdc_rs
                in    a,(1)
                ld    (hl),a
                inc   hl
                ex    (sp),hl                 ; settle before the next poll
                ex    (sp),hl
                ex    (sp),hl
                ex    (sp),hl
                djnz  fdc_rs
fdc_rsdone
                ld    a,(fdc_st)
                ret

; after RECALIBRATE/SEEK: wait for the INTRQ mirror, then acknowledge with
; ONE SENSE INTERRUPT - never hammered during the seek (real-chip rule)
fdc_wait_seek
                in    a,(PCW_SYSCTL)
                and   #20                     ; live FDC INTRQ
                jr    z,fdc_wait_seek
                ld    a,8                     ; SENSE INTERRUPT (the acknowledge)
                call  fdc_send1
                call  fdc_res                 ; ST0
                cp    #80                     ; spurious race: wait again
                jr    z,fdc_wait_seek
                ld    b,a
                call  fdc_res                 ; PCN, discard
                bit   5,b                     ; seek end?
                jr    z,fdc_wait_seek
                ret

; --- command templates / state -------------------------------------------------
fdc_cmd_spec    db    #03,#0F,#FF             ; SPECIFY: timings + non-DMA
fdc_cmd_rcal    db    #07,#00                 ; RECALIBRATE unit 0
fdc_cmd_seek    db    #0F,#00                 ; SEEK unit 0
fdc_sk_trk      db    0
fdc_cmd_read    db    #66                     ; READ DATA, MFM + SK
fdc_rd_u        db    #00                     ; unit + head
fdc_rd_c        db    0
fdc_rd_h        db    #00                     ; H
fdc_rd_r        db    1
                db    #02                     ; N = 512
                db    #09                     ; EOT = 9 (TC terminates)
                db    #2A                     ; GPL
                db    #FF                     ; DTL
fdc_cmd_write   db    #45                     ; WRITE DATA, MFM
fdc_wr_u        db    #00                     ; unit + head
fdc_wr_c        db    0
fdc_wr_h        db    #00                     ; H
fdc_wr_r        db    1
                db    #02                     ; N = 512
                db    #09                     ; EOT = 9 (TC terminates)
                db    #2A                     ; GPL
                db    #FF                     ; DTL

fdc_cmd_rdid    db    #4A,#01                 ; READ ID, MFM, unit 1

fdc_track       db    #FF                     ; head position shadow (#FF = unknown)
fdc_unit        db    0                       ; selected drive unit (0 = A, 1 = B)
fdc_dbl         db    0                       ; 1 = double-step (CF2 in the DD drive)
fdc_dbl_b       db    1                       ; unit 1's measured stepping mode
fdr_trk         db    0
fdr_side        db    0
fdr_sec         db    0
fdr_dst         dw    0
fdc_st          ds    7                       ; last command's result bytes
