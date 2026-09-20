; kernel/scheduler.asm - fixed low-RAM scheduler image (issue #477).
;
; This file is assembled by scheduler_image.asm, not included in GBKERN. The
; preemptive desktop carries the resulting image in its app page and copies it
; to SCHED_BASE before entering the window-manager loop. CPC/PCW reserve
; #3C00-#3DFF; MSX uses fixed page-3 RAM at #C900.
;
; An active task continues to use the platform's existing fixed stack.  On a
; switch, only BOOT_SP-SP live bytes are copied into the owning app bank.  This
; avoids eight fixed-RAM stacks and keeps the stack visible while kernel APIs
; temporarily map PAGE_DATA over #4000-#7FFF.

TASK_SNAPSHOT   equ   #7F00        ; top 256 bytes of a participating app bank
TASK_STACK_LEN  equ   TASK_SNAPSHOT ; byte: saved stack length
TASK_STACK_DATA equ   TASK_SNAPSHOT+1
TASK_STACK_CAP  equ   255          ; includes interrupt-saved Z80 context
TASK_STACK_SIZE equ   256          ; length byte plus TASK_STACK_CAP payload
                ifdef PLATFORM_MSX
SCHED_QUANTUM_TICKS equ 2          ; 25/30 Hz slices from the 50/60 Hz MSX IRQ
                else
SCHED_QUANTUM_TICKS equ 6          ; six 300 Hz CPC firmware ticks per 50 Hz slice
                endif

; #1450..#1480 is write-before-read kernel scratch. A real switch may use it
; as a tiny emergency stack only after SCHED_LOCK reaches zero. Interrupts
; arriving while kernel scratch is live return without switching.
SCHED_TMP_BOTTOM equ #1450
SCHED_TMP_TOP    equ   #1481

                org   SCHED_BASE
                jp    sched_init_impl
                jp    sched_yield
                jp    k_task_enable
                jp    sched_irq_uninstall

sched_init_impl
                xor   a
                ld    hl,SCHED_LOCK
                ld    de,SCHED_LOCK+1
                ld    bc,7
                ld    (hl),a
                ldir                          ; clear all eight scheduler bytes
                ld    a,#FF
                ld    (SCHED_CURRENT),a
                ld    a,1
                ld    (SCHED_LOCK),a          ; kernel owns execution at boot
                if PREEMPTIVE_CONTEXT
                ld    a,SCHED_QUANTUM_TICKS
                ld    (SCHED_QUANTUM),a
                if PREEMPTIVE_TIMER
                call  sched_irq_install       ; one install for the scheduler lifetime
                endif
                endif
                ret

                if PREEMPTIVE_CONTEXT
; sched_switch_context: switch from one fully saved app context to the next
; runnable WM slot. The interrupt wrapper must have pushed, in order:
;
;   AF, BC, DE, HL, IX, IY, AF', BC', DE', HL'
;
; and CALLed this routine. The CALL return address is intentionally part of the
; copied stack. Restoring another snapshot and RET therefore continues in that
; task's copy of the yield/interrupt wrapper, which restores its registers and
; either returns from a yield or completes the pending firmware tick. I and the
; interrupt mode are kernel-owned, not task-local.
;
; Preconditions: SCHED_LOCK=0, SCHED_CURRENT names a runnable slot, the current
; app bank is mapped, and participating app layouts reserve #7F00-#7FFF.
; Clobbers every register (the wrapper has already saved them).
sched_switch_context
                ld    hl,0
                add   hl,sp
                ex    de,hl                   ; DE = old SP
                ld    hl,(BOOT_SP)
                or    a
                sbc   hl,de                   ; HL = live fixed-stack bytes
                ld    a,h
                or    a
                jp    nz,sched_switch_fault
                ld    a,l                     ; zero is impossible (CALL is live)
                or    a
                jp    z,sched_switch_fault
                ld    hl,SCHED_STACK_MAX      ; always retain the observed high-water mark
                cp    (hl)
                jr    c,sched_stack_sampled
                ld    (hl),a
sched_stack_sampled

                ld    sp,SCHED_TMP_TOP        ; kernel scratch is dead while unlocked
                push  de                      ; old SP, below scheduler call frames
                ld    (TASK_STACK_LEN),a      ; current app bank is still mapped
                ld    c,a
                ld    b,0
                ex    de,hl                   ; HL = old SP
                ld    de,TASK_STACK_DATA
                ldir

                ; Search the other seven slots round-robin. A runnable flag is
                ; set only after its initial snapshot has been constructed. Build
                ; the first entry pointer once and then advance it directly; the
                ; old wm_entry-per-candidate loop was needlessly quadratic.
                ld    a,(SCHED_CURRENT)
                inc   a
                and   WM_MAXWIN-1             ; WM_MAXWIN is deliberately a power of two
                ld    c,a
                call  sched_wm_entry
                ld    de,WM_FR_FLAGS
                add   hl,de
                ld    b,WM_MAXWIN-1
sched_next_slot
                ld    a,(hl)
                and   WM_TASK_RUNNABLE|1      ; both alive and runnable must be set
                cp    WM_TASK_RUNNABLE|1
                jr    z,sched_restore_slot
                inc   c
                ld    a,c
                cp    WM_MAXWIN
                jr    nc,sched_scan_wrap
                ld    de,WM_ESZ
                add   hl,de
sched_scan_count
                djnz  sched_next_slot

                ; No peer is runnable. The original fixed stack was never
                ; overwritten, so resume it without copying back.
sched_resume_old
                pop   hl
                ld    sp,hl
                ret
sched_scan_wrap
                ld    c,0
                ld    hl,WM_TABLE+WM_FR_FLAGS
                jr    sched_scan_count

sched_restore_slot
                ld    a,c
                push  af                      ; target slot above the saved old SP
                ld    de,-WM_FR_FLAGS
                add   hl,de                   ; flags pointer -> entry/page pointer
                ld    a,(hl)                  ; target app page from WM entry +0
                call  sched_bank_set
                ld    a,(TASK_STACK_LEN)
                or    a
                jr    z,sched_restore_fault
                ld    c,a
                ld    e,a
                ld    b,0
                ld    d,b
                ld    hl,(BOOT_SP)
                or    a
                sbc   hl,de                   ; target fixed-stack bottom
                push  hl                      ; keep target SP below mapper call frames
                ex    de,hl                   ; DE = fixed destination
                ld    hl,TASK_STACK_DATA
                ldir
                pop   hl                      ; target SP
                pop   af                      ; target slot
                ld    (SCHED_CURRENT),a
                pop   de                      ; discard old SP
                ld    sp,hl
                ret

sched_restore_fault
                ; The old bank was already replaced. Map the old task again,
                ; then resume its still-intact fixed stack.
                pop   af                      ; discard target slot above the saved old SP
                ld    a,(SCHED_CURRENT)
                call  sched_wm_entry
                ld    a,(hl)
                call  sched_bank_set
                ld    a,1
                ld    (SCHED_FAULT),a
                jr    sched_resume_old

sched_switch_fault
                ld    a,1
                ld    (SCHED_FAULT),a
                ret                             ; original SP is still active

; sched_yield: safe-point counterpart of the interrupt wrapper. Every task
; context has the same register image; only its continuation differs. A system
; or finite worker task uses this path, while a timer-preempted worker resumes
; through sched_irq_restore below.
sched_yield
                di
                push  af
                push  bc
                push  de
                push  hl
                push  ix
                push  iy
                ex    af,af'
                push  af
                ex    af,af'
                exx
                push  bc
                push  de
                push  hl
                exx
                ld    a,(SCHED_CURRENT)
                call  sched_wm_entry
                ld    de,WM_FR_FLAGS
                add   hl,de
                set   3,(hl)                   ; root becomes runnable with its first snapshot
                call  sched_switch_context
sched_yield_restore
                ld    a,1
                ld    (SCHED_LOCK),a
sched_context_restore
                exx
                pop   hl
                pop   de
                pop   bc
                exx
                ex    af,af'
                pop   af
                ex    af,af'
                pop   iy
                pop   ix
                pop   hl
                pop   de
                pop   bc
                ld    a,(SCHED_RESERVED)      ; AF is still saved, so restored HL stays untouched
                rra
                jr    nc,sched_restore_yield
                xor   a
                ld    (SCHED_RESERVED),a
                ifdef PLATFORM_PCW
                jp    sched_pcw_irq_finish
                else
                pop   af
                ifdef PLATFORM_MSX
                jp    sched_irq_chain          ; finish through the saved DOS IM1 handler
                else
                jp    sched_irq_chain          ; finish through this machine's firmware handler
                endif
                endif
sched_restore_yield
                pop   af
                ei
                ret

; k_task_enable: opt an already-registered managed window into task service.
; desc.task_worker becomes the compute-only callback; desc.proc stays on the
; root/compositor task for draw, input, and lifecycle. The app build must reserve
; #7F00-#7FFF.
k_task_enable
                di
                ld    a,(WM_FOCUS)
                call  sched_wm_entry
                push  hl
                ld    a,(BANK_CUR)
                cp    (hl)                    ; only the calling app may enable itself
                jr    nz,sched_task_enable_fail
                ld    a,2
                ld    (TASK_STACK_LEN),a
                ld    hl,sched_task_start
                ld    (TASK_STACK_DATA),hl
                pop   hl
                ld    de,WM_FR_FLAGS
                add   hl,de
                bit   1,(hl)                   ; only managed windows have a task_worker field
                jr    z,sched_task_enable_done
                bit   3,(hl)
                jr    nz,sched_task_enable_done
                set   3,(hl)                  ; publish only after snapshot is complete
                ld    hl,SCHED_RUNNABLE
                inc   (hl)
sched_task_enable_done
                ei
                ret
sched_task_enable_fail
                pop   hl
                ei
                ret

; A task-enabled managed descriptor's task_worker runs outside the compositor.
; Existing apps are not opted in: the normal WM continues to drive their proc
; exactly as it does in release builds. A worker is deliberately pure compute;
; kernel and paged-module work belongs in proc callbacks on the root task.
sched_task_start
sched_task_loop
                ld    a,(SCHED_CURRENT)
                call  sched_wm_entry
                push  hl
                ld    de,WM_FR_FLAGS
                add   hl,de
                ld    a,(hl)
                and   WM_TASK_RUNNABLE|1
                cp    WM_TASK_RUNNABLE|1
                pop   hl
                jr    nz,sched_task_sleep
                ld    de,WM_FR_FRAME
                add   hl,de
                ld    e,(hl)                   ; entry+5 = managed descriptor pointer
                inc   hl
                ld    d,(hl)
                ex    de,hl
                ld    de,10                    ; desc.task_worker
                add   hl,de
                ld    a,(hl)
                inc   hl
                ld    h,(hl)
                ld    l,a
                ld    a,h
                or    l
                jr    z,sched_task_sleep
                xor   a
                ld    (SCHED_LOCK),a
                ei                              ; first-run snapshots inherit sched_yield's DI
                call  sched_md_call
                ld    a,1
                ld    (SCHED_LOCK),a
sched_task_sleep
                call  sched_yield
                jr    sched_task_loop

                ifdef PLATFORM_PCW
; GEOBENCH owns the PCW outright. Its FDC interrupt route remains disabled and
; storage is polled, leaving the ASIC's 300 Hz maskable timer as the only INT
; source. The standard PCW MCU bootstrap has F0 21 F5 at #0038; k_exit restores
; those bytes before jumping to #0000 for a warm boot.
PCW_IRQ_VECTOR  equ #0038
PCW_TIMER_ACK   equ #F4

sched_irq_install
                ld    hl,PCW_IRQ_VECTOR
                ld    (hl),#C3
                inc   hl
                ld    de,sched_irq_vector
                ld    (hl),e
                inc   hl
                ld    (hl),d
                im    1
                in    a,(PCW_TIMER_ACK)       ; discard timer ticks accumulated while DI
                ret

sched_irq_uninstall
                ld    hl,PCW_IRQ_VECTOR
                ld    (hl),#F0
                inc   hl
                ld    (hl),#21
                inc   hl
                ld    (hl),#F5
                ret

; AF is still the first word on the selected task's restored stack. Acknowledge
; the PCW timer before restoring it, then return directly from IM 1.
sched_pcw_irq_finish
                in    a,(PCW_TIMER_ACK)
                pop   af
                ei
                reti
                else
                ifdef PLATFORM_MSX
; MSX-DOS leaves a writable IM 1 trampoline at #0038 while the TPA is active.
; Hooking it keeps page-0 GEOBENCH state visible, unlike H.TIMI (which runs
; after the BIOS maps its ROM into page 0). The original DOS JP target is
; copied into sched_irq_chain and receives the exact raw interrupt stack after
; either a fast path or a task switch.
MSX_IRQ_VECTOR  equ #0038

sched_irq_install
                ld    hl,(MSX_IRQ_VECTOR+1)
                ld    (sched_irq_chain+1),hl
                ld    hl,sched_irq_vector
                ld    (MSX_IRQ_VECTOR+1),hl
                ret

sched_irq_uninstall
                ld    hl,(sched_irq_chain+1)
                ld    (MSX_IRQ_VECTOR+1),hl
                ret

sched_irq_chain
                jp    0                       ; patched with the original DOS IRQ target
                else
; CPC IM 1 enters through writable RAM at #0038. The scheduler runs before the
; firmware tick. Preserve and chain through the target already installed there:
; the CPC6128 firmware uses #B941, the CPC464 uses #B939, and replacement lower
; ROMs may install another compatible handler. A context switch marks the shared
; restore path, which tail-calls that saved target after the selected registers
; and stack are active. Firmware therefore always sees its native interrupt
; stack contract. External interrupts skip scheduler work because the firmware
; handler owns a different stack contract for them.
CPC_IRQ_VECTOR  equ #0038

sched_irq_install
                ld    hl,(CPC_IRQ_VECTOR+1)
                ld    (sched_irq_chain+1),hl
                ld    hl,sched_irq_vector
                jr    sched_irq_set_vector

; Restore the machine's original firmware vector before returning to BASIC/DOS.
sched_irq_uninstall
                ld    hl,(sched_irq_chain+1)
sched_irq_set_vector
                ld    (CPC_IRQ_VECTOR+1),hl
                ret

sched_irq_chain
                jp    0                       ; patched with the original CPC IRQ target
                endif
                endif

sched_irq_vector
                ifndef PLATFORM_PCW
                ifndef PLATFORM_MSX
                ex    af,af'
                jr    nc,sched_irq_normal
                ex    af,af'                  ; restore app AF and leave firmware's stack untouched
                jp    sched_irq_chain
sched_irq_normal
                ex    af,af'
                endif
                endif
sched_irq_body
                push  af
                push  hl                      ; quantum bookkeeping must preserve worker HL
                ld    a,(SCHED_CURRENT)
                or    a
                jr    z,sched_irq_fast
sched_irq_worker
                if !PREEMPTIVE_SWITCH
                jp    sched_irq_fast          ; diagnostic: prove the firmware trampoline alone
                endif
                ld    hl,SCHED_QUANTUM
                dec   (hl)
                jr    nz,sched_irq_fast
                ld    (hl),SCHED_QUANTUM_TICKS
                ld    a,(SCHED_LOCK)
                or    a
                jr    z,sched_irq_switch
sched_irq_fast
                pop   hl
                ifdef PLATFORM_PCW
                jp    sched_pcw_irq_finish
                else
                pop   af
                ifdef PLATFORM_MSX
                jp    sched_irq_chain
                else
                jp    sched_irq_chain
                endif
                endif

sched_irq_switch
                pop   hl
                inc   a                       ; A is zero after the SCHED_LOCK test
                ld    (SCHED_RESERVED),a      ; common restore must complete this firmware tick
                push  bc
                push  de
                push  hl
                push  ix
                push  iy
                ex    af,af'
                push  af
                ex    af,af'
                exx
                push  bc
                push  de
                push  hl
                exx
                ld    hl,21                   ; interrupted PC high byte after ten PUSHes
                add   hl,sp
                ld    a,(hl)
                cp    #40                     ; only code in the mapped app bank is preemptible
                jr    c,sched_irq_defer
                cp    #80
                jr    nc,sched_irq_defer
                ld    a,(SCHED_CURRENT)
                call  sched_wm_entry
                ld    a,(BANK_CUR)
                cp    (hl)                    ; reject kernel modules mapped over the app bank
                jr    nz,sched_irq_defer
                call  sched_switch_context
sched_irq_restore
                xor   a
                ld    (SCHED_LOCK),a
                jp    sched_context_restore
sched_irq_defer
                jp    sched_context_restore

; Fixed-address scheduler helpers. They duplicate only the small pieces that
; cannot be called through profile-dependent resident labels.
sched_wm_entry
                ld    hl,WM_TABLE
                or    a
                ret   z
                ld    b,a
                ld    de,WM_ESZ
sched_we_add
                add   hl,de
                djnz  sched_we_add
                ret

sched_md_call
                jp    (hl)

sched_bank_set
                ld    (BANK_CUR),a
                ifdef PLATFORM_MSX
                ld    hl,(MSX_PUTP1)
                jp    (hl)                    ; mapper RET returns to our caller
                else
                ifdef PLATFORM_PCW
                out   (PCW_BANK1),a
                else
                ld    bc,#7F00
                out   (c),a
                endif
                ret
                endif

                endif                         ; PREEMPTIVE_CONTEXT

sched_image_end
                ifndef SCHED_LIMIT
SCHED_LIMIT     equ   512
                endif
                assert sched_image_end-SCHED_BASE<=SCHED_LIMIT,"scheduler exceeds fixed slot"
