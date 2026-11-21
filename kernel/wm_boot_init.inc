; Low RAM may contain loader bytes at startup. Clear through the drag state,
; not just the z-order: WM_DRAGMOV bit 7 locks directory scans and file jobs.
wm_boot_init
                ld    hl,WM_NWIN
                ld    de,WM_NWIN+1
                ld    bc,APP_NPAGES-WM_NWIN-1
                ld    (hl),0
                ldir
                call  app_pool_init          ; initializes APP_NPAGES and the pool after it
                ld    a,#FF
                ld    (WM_FPREV),a           ; force first focus to install the desktop menu
