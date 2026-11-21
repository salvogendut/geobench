; kernel/boot_pcw.asm - GEOBENCH boot/exit flow for the PCW target (#331).
;
; The PCW counterpart of kernel/boot.asm / boot_msx.asm, included right after
; the fixed API table so kernel_main stays the GB_INIT target and k_exit the
; GB_EXIT target. kernel/pcwboot.asm (the boot sector) has already loaded
; this kernel to #8000 from the disc's reserved tracks and jumped here - the
; machine is ours: no OS, no firmware, interrupts stay off, everything polls.
;
; Memory plan (lib/pcw/glue.inc): slot 0 = block 0 (low RAM), slot 1 = the
; app window (pool blocks #86..#8D), slot 2 = block 2 (this kernel, stack at
; its top), slot 3 = the screen/keyboard window. PAGE_DATA = block 14 (#8E).
;
; GB_EXIT: there is nowhere to return to - jump to #0000, where the MCU
; bootstrap still lives in RAM, for a clean warm reboot of GEOBENCH.

kernel_main
                di
                ld    sp,#C000               ; stack at the top of the kernel block
                ld    (BOOT_SP),sp
                ld    hl,0                    ; empty the shared clipboard (#142)
                ld    (CLIP_LEN),hl
                xor   a
                ld    (UI_MODAL),a
                call  bank_normal            ; seed the slot-1 shadow (identity)
                call  pcw_video_init         ; roller table + cls + display on
                ld    hl,pal_def             ; seed the default inks: KCFG_INKS stays
                ld    de,KCFG_INKS           ; in CPC ink numbers (the canonical config
                ld    bc,5                    ; space); the CGA2 palette is fixed, so
                ldir                          ; set_palette is a stub - the seed only
                ld    a,1                     ; feeds Settings' read-back.
                ld    (BD_SOLID),a
                call  fs_init
                call  input_init
                call  pcw_mem_init           ; KCFG_MEMKB from the bank-wrap probe
                call  cfg_boot               ; parse GEOBENCH.CFG (paged C module)
                call  clip_set_full          ; boot clip rect = full screen
                call  boot_splash            ; lollipop + empty load bar
                call  boot_tick              ; bar 1/4
                call  assets_load            ; font + icons + cursor + backdrop
                call  boot_tick              ; bar 2/4
                call  clock_init
                call  boot_tick              ; bar 3/4
                include "wm_boot_init.inc"
                ld    hl,kern_end-GB_KERNEL
                ld    (GB_KSIZE),hl
                call  boot_tick              ; bar 4/4
                call  boot_desktop           ; the WM master loop never returns

name_desktop    db    "DESKTOP APP"

; k_exit (GB_EXIT): no host OS - warm-reboot through the MCU bootstrap, which
; is still in RAM at #0000 (it reloads the boot sector and GEOBENCH afresh).
k_exit
km_finish
                di
                if PREEMPTIVE_CONTEXT
                call  SCHED_IRQ_UNINSTALL_ENTRY ; restore retained MCU bootstrap bytes at #0038
                endif
                jp    0

; pcw_mem_init: 256K vs 512K by the bank-wrap probe: non-existent blocks
; alias modulo the fitted RAM, so block 16 writes land in block 0 on a 256K
; machine. Probes through the slot-3 window at a phys offset nothing uses
; (block 0 #0FF0 - clear of the resident bootstrap image at #0000-#0113).
pcw_mem_init
                ld    a,#80                   ; block 0 in the window
                out   (PCW_BANK3),a
                ld    a,#5A
                ld    (#CFF0),a               ; phys #0FF0 = #5A
                ld    a,#90                   ; block 16 in the window
                out   (PCW_BANK3),a
                ld    a,#A5
                ld    (#CFF0),a               ; phys #40FF0 (or the alias)
                ld    a,#80
                out   (PCW_BANK3),a
                ld    a,(#CFF0)               ; aliased? then 16 blocks fitted
                cp    #A5
                ld    hl,256
                jr    z,pmi_have
                ld    hl,512
pmi_have
                ld    (KCFG_MEMKB),hl
                ret

; app_pool_init: fixed pool - physical blocks 6..13 (#86..#8D), all present
; on the smallest 256K machine. The desktop (first claim) gets [0] = #86 =
; PAGE_APP0. PAGE_DATA is block 14, outside the pool.
app_pool_init
                ld    hl,APP_PAGES
                ld    a,PAGE_APP0
                ld    b,WM_MAXWIN
apl_fill
                ld    (hl),a
                inc   hl
                inc   a
                djnz  apl_fill
                ld    a,WM_MAXWIN
                ld    (APP_NPAGES),a
                ld    hl,APP_BUSY            ; mark every page free
                ld    de,APP_BUSY+1
                ld    bc,WM_MAXWIN-1
                ld    (hl),0
                ldir
                ret
