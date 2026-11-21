; ---------------------------------------------------------------------------
; lib/msx/fs.asm - MSX-DOS 2 / Nextor storage backend for the MSX2 target (#287).
;
; Replaces the whole CPC storage stack (dispatcher + card/floppy backends):
; the DOS already owns the disk, so every operation is a BDOS call. The
; interface is the same one lib/fs.asm exposed - fs_init / fs_set_drive /
; fs_dir_first / fs_dir_next / fs_load_file / fs_load_cur_sys / fs_load_sys /
; fs_save_file / fs_delete_file / fs_free_kib / fs_sysdir_enter / fs_sysdir_
; leave / fs_sys_resolve + fsmx_chdir / fsmx_back for GB_CHDIR/GB_BACK - and
; the same fs_ent_* / fs_req_name / fs_load_* low-RAM cells.
;
; Directory enumeration maps _FFIRST/_FNEXT FIBs into fs_ent_* (the FIB
; carries name/attr/size straight from the FAT entry). Loads open a handle,
; read up to fs_load_max, and probe one extra byte to detect too-big files.
; GEOBENCH keeps three window slots, but each slot maps to the actual DOS drive
; number discovered at startup. Nextor's _GDLI/_GDRVR metadata classifies each
; slot independently of its letter; plain MSX-DOS 2 falls back to _LOGIN.
;
; Notes:
;  - BDOS may be entered with a mapped app segment in page 1: switching only
;    ever goes through PUT_P1, so DOS's page-1 shadow stays coherent and its
;    own restore logic works (the M0 spike proved the map + r/w path).
;  - BDOS calls run with interrupts enabled (an `ei` up front): callers wrap
;    fs ops in DI/EI for the CPC's banking, but the DOS drivers need IRQs.
;  - System files live in \GEOBENCH on the boot drive, entered per-load with
;    _CHDIR and restored with the saved _GETCD path - same semantics as the
;    CPC's fs_sysdir_enter/leave.
; ---------------------------------------------------------------------------

FS_ATTR_DIRS    equ   #10          ; _FFIRST search attributes: include directories

FSMX_MEDIA_FLOPPY equ 0
FSMX_MEDIA_SD     equ 1
FSMX_MEDIA_IDE    equ 2
FSMX_MEDIA_OTHER  equ 3
FSMX_MEDIA_RAM    equ 4

; fs_init: capture the real DOS boot drive and populate the three GEOBENCH drive
; slots. The boot drive is always slot 0; remaining assigned letters follow in
; alphabetical order. This keeps the system volume reachable even on a machine
; with more drive letters than the desktop can display.
fs_init
                ld    a,(MSX_TPASEG)          ; fs_init precedes msx_mem_init: seed the
                ld    (bank_cur),a             ; page shadow before any BDOS restore
                xor   a
                ld    (FS_XFLAGS),a
                ei
                push  ix
                ld    c,_CURDRV
                call  BDOS
                pop   ix
                ld    a,l                     ; DOS/Nextor return the drive in L
                ld    (MSX_BOOT_DOS),a
                call  fsmx_scan_drives
                xor   a
                ld    (fs_boot_drive),a
                ld    (fs_cur_drive),a
                ret

; fs_set_drive: A = GEOBENCH slot. Resolve it to the DOS drive number, make that
; drive current through _SELDSK, and update fs_cur_drive only on success.
; Nextor's CP/M-compatible _SELDSK return value is not consistent with the
; MSX-DOS 2 error-code convention (some kernels return the drive count in A),
; so verify the result through _CURDRV instead of testing A.
fs_set_drive
                ld    (fsmx_target_slot),a
                ld    b,a
                ld    a,(MSX_DRIVE_COUNT)
                cp    b
                ret   z
                ret   c
                ld    hl,MSX_DRIVE_MAP
                ld    e,b
                ld    d,0
                add   hl,de
                ld    e,(hl)
                ld    a,e
                ld    (fsmx_target_dos),a
                ei
                push  ix
                ld    c,_SELDSK
                call  BDOS
                ld    c,_CURDRV
                call  BDOS
                pop   ix
                ld    a,(fsmx_target_dos)
                cp    l
                call  fsmx_restore_page
                ret   nz
                ld    a,(fsmx_target_slot)
                ld    (fs_cur_drive),a
                ret

; fsmx_drive_poll: translate the populated slot count to the historical mask
; layout (slot 0=GB_DRV_C, slot 1=GB_DRV_A, slot 2=GB_DRV_B). On MSX these are
; slot bits only; letters and media types live in MSX_DRIVE_MAP/TYPE.
fsmx_drive_poll
                ld    a,(MSX_DRIVE_COUNT)
                or    a
                ret   z
                ld    b,a
                ld    a,%00000100
                dec   b
                ret   z
                or    %00000001
                dec   b
                ret   z
                or    %00000010
                ret

; Discover the DOS drive assignments once, while the kernel is starting. Nextor
; supplies both the actual letter assignment and the owning driver. If _GDLI is
; unavailable (plain MSX-DOS 2), _LOGIN still gives us the assigned letters.
fsmx_scan_drives
                xor   a
                ld    (MSX_DRIVE_COUNT),a
                ld    hl,MSX_DRIVE_MAP
                ld    de,MSX_DRIVE_MAP+1
                ld    bc,5
                ld    (hl),#FF
                ldir

                ld    a,(MSX_BOOT_DOS)        ; preserve the launch volume as slot 0
                call  fsmx_probe_nextor
                xor   a
                ld    (fsmx_scan_drive),a
fsmx_scan_next
                ld    a,(MSX_DRIVE_COUNT)
                cp    3
                ret   nc
                ld    a,(fsmx_scan_drive)
                cp    8
                jr    nc,fsmx_scan_next_done
                call  fsmx_probe_nextor
                ld    a,(fsmx_scan_drive)
                inc   a
                ld    (fsmx_scan_drive),a
                jr    fsmx_scan_next
fsmx_scan_next_done
                ld    a,(MSX_DRIVE_COUNT)
                or    a
                ret   nz

                ; No _GDLI result: use the standard MSX-DOS assigned-drive map.
                ei
                push  ix
                ld    c,_LOGIN
                call  BDOS
                pop   ix
                ld    (fsmx_login),hl
                ld    a,(MSX_BOOT_DOS)
                call  fsmx_append_fallback
                xor   a
                ld    (fsmx_scan_drive),a
fsmx_scan_login
                ld    a,(MSX_DRIVE_COUNT)
                cp    3
                ret   nc
                ld    a,(fsmx_scan_drive)
                cp    8
                ret   nc
                ld    b,a
                ld    hl,(fsmx_login)
                inc   b
fsmx_login_shift
                srl   h
                rr    l
                djnz  fsmx_login_shift
                jr    nc,fsmx_scan_login_next
                ld    a,(fsmx_scan_drive)
                call  fsmx_append_fallback
fsmx_scan_login_next
                ld    a,(fsmx_scan_drive)
                inc   a
                ld    (fsmx_scan_drive),a
                jr    fsmx_scan_login

; Probe one actual DOS drive with Nextor. The drive-letter block identifies the
; owning driver; its printable name distinguishes common SD and IDE drivers.
; Legacy MSX-DOS drivers have flags=0 and no name, and are treated as floppies.
fsmx_probe_nextor
                ld    (fsmx_probe_drive),a
                ld    hl,fsmx_fib
                ld    de,fsmx_fib+1
                ld    bc,63
                xor   a
                ld    (hl),a
                ldir
                ld    a,(fsmx_probe_drive)
                ld    hl,fsmx_fib
                ei
                push  ix
                ld    c,_GDLI
                call  BDOS
                pop   ix
                or    a
                ret   nz
                ld    a,(fsmx_fib)
                cp    4
                jp    z,fsmx_probe_ram
                cp    1
                ret   nz

                ; _GDLI reports assigned letters even when an SD slot or floppy
                ; has no mounted medium. Only expose drives whose filesystem is
                ; currently accessible; otherwise every controller slot becomes
                ; a misleading desktop icon and opening it can block on I/O.
                call  fsmx_drive_accessible
                ret   nz

                ld    a,FSMX_MEDIA_OTHER      ; retain a useful fallback if this
                ld    (fsmx_probe_hint),a     ; legacy driver rejects _GDRVR
                ld    a,(fsmx_fib+3)          ; relative drive, FF=device-based
                inc   a
                jr    z,fsmx_probe_driver
                ld    a,FSMX_MEDIA_FLOPPY
                ld    (fsmx_probe_hint),a
fsmx_probe_driver
                ld    a,(fsmx_fib+1)
                ld    d,a
                ld    a,(fsmx_fib+2)
                ld    e,a
                xor   a
                ld    hl,fsmx_fib
                ei
                push  ix
                ld    c,_GDRVR
                call  BDOS
                pop   ix
                or    a
                jr    nz,fsmx_probe_hint_append

                ; Search the 32-byte, space-padded driver name case-insensitively.
                ld    hl,fsmx_fib+8
                ld    b,31
fsmx_find_sd
                ld    a,(hl)
                and   #DF
                cp    'S'
                jr    nz,fsmx_find_sd_next
                inc   hl
                ld    a,(hl)
                dec   hl
                and   #DF
                cp    'D'
                jr    z,fsmx_probe_sd
fsmx_find_sd_next
                inc   hl
                djnz  fsmx_find_sd

                ld    hl,fsmx_fib+8
                ld    b,30
fsmx_find_ide
                ld    a,(hl)
                and   #DF
                cp    'I'
                jr    nz,fsmx_find_ide_next
                inc   hl
                ld    a,(hl)
                and   #DF
                cp    'D'
                jr    nz,fsmx_find_ide_back
                inc   hl
                ld    a,(hl)
                and   #DF
                cp    'E'
                jr    z,fsmx_probe_ide
                dec   hl
fsmx_find_ide_back
                dec   hl
fsmx_find_ide_next
                inc   hl
                djnz  fsmx_find_ide

                ld    a,(fsmx_fib+4)          ; device-based but unknown storage
                and   1
                jr    nz,fsmx_probe_other
                ld    e,FSMX_MEDIA_FLOPPY     ; legacy/drive-based media
                jr    fsmx_probe_append
fsmx_probe_sd
                ld    e,FSMX_MEDIA_SD
                jr    fsmx_probe_append
fsmx_probe_ide
                ld    e,FSMX_MEDIA_IDE
                jr    fsmx_probe_append
fsmx_probe_other
                ld    e,FSMX_MEDIA_OTHER
                jr    fsmx_probe_append
fsmx_probe_hint_append
                ld    a,(fsmx_probe_hint)
                ld    e,a
                jr    fsmx_probe_append
fsmx_probe_ram
                ld    e,FSMX_MEDIA_RAM
fsmx_probe_append
                ld    a,(fsmx_probe_drive)
                jr    fsmx_append_drive

; Return Z only when Nextor can query the drive's filesystem. Empty removable
; units raise a DOS disk error instead of returning it normally, so install a
; short-lived handler which aborts that one BDOS call back to this probe.
fsmx_drive_accessible
                call  fsmx_trap_errors
                ld    a,(fsmx_probe_drive)
                inc   a                       ; _DSPACE: 1=A:, 2=B:, ...
                ld    e,a
                xor   a                       ; query free space
                ld    c,_DSPACE
                push  ix
                call  BDOS
                pop   ix
                ld    (fsmx_probe_error),a
                call  fsmx_untrap_errors
                call  fsmx_restore_page
                ld    a,(fsmx_probe_error)
                or    a
                ret
; The graphical desktop cannot answer DOS's console Abort/Retry/Ignore prompt.
; Scope handlers to the operation; normal DOS behaviour resumes on return.
fsmx_trap_errors
                push  ix
                ld    de,fsmx_abort_exit
                ld    c,_DEFAB
                call  BDOS
                ld    de,fsmx_disk_error
                ld    c,_DEFER
                jr    fsmx_set_handler
fsmx_untrap_errors
                push  ix
                ld    de,0
                ld    c,_DEFER
                call  BDOS
                ld    de,0
                ld    c,_DEFAB
fsmx_set_handler
                call  BDOS
                pop   ix
                ret
fsmx_disk_error
                ld    a,1                     ; abort this disk operation
                ret
fsmx_abort_exit
                pop   hl                      ; resume after the interrupted BDOS call
                ret

; Plain MSX-DOS has no driver metadata. A:/B: are conventional floppy letters;
; later letters are retained as generic storage rather than guessed as SD/IDE.
fsmx_append_fallback
                ld    e,FSMX_MEDIA_FLOPPY
                cp    2
                jr    c,fsmx_append_drive
                ld    e,FSMX_MEDIA_OTHER

; Append A=actual DOS drive, E=media type unless already present/full.
fsmx_append_drive
                ld    (fsmx_probe_drive),a
                ld    a,e
                ld    (fsmx_probe_type),a
                ld    a,(MSX_DRIVE_COUNT)
                cp    3
                ret   nc
                ld    b,a
                ld    hl,MSX_DRIVE_MAP
fsmx_append_check
                ld    a,b
                or    a
                jr    z,fsmx_append_store
                ld    a,(fsmx_probe_drive)
                cp    (hl)
                ret   z
                inc   hl
                dec   b
                jr    fsmx_append_check
fsmx_append_store
                ld    a,(fsmx_probe_drive)
                ld    (hl),a
                ld    hl,MSX_DRIVE_TYPE
                ld    a,(MSX_DRIVE_COUNT)
                ld    e,a
                ld    d,0
                add   hl,de
                ld    a,(fsmx_probe_type)
                ld    (hl),a
                ld    a,(MSX_DRIVE_COUNT)
                inc   a
                ld    (MSX_DRIVE_COUNT),a
                ret

; fs_sys_resolve: nothing to pre-resolve - fs_sysdir_enter chdirs per call.
fs_sys_resolve
                ret

; fs_sysdir_enter: save the CWD, then _CHDIR into \GEOBENCH. A missing system
; dir leaves the CWD unchanged (loads then resolve against the current dir,
; the same flat-root fallback the CPC card backend has).
fs_sysdir_enter
                ei
                push  ix
                ld    c,_GETCD
                ld    b,0                     ; current drive
                ld    de,fsmx_cwd
                call  BDOS
                ld    c,_CHDIR
                ld    de,fsmx_sysdir
                call  BDOS
                pop   ix
                jp    fsmx_restore_page

; fs_sysdir_leave: _CHDIR back to the saved CWD (root-relative path).
fs_sysdir_leave
                ei
                push  ix
                ld    a,(fsmx_cwd)           ; empty string = root
                or    a
                jr    nz,fsl_go
                ld    de,fsmx_root
                jr    fsl_cd
fsl_go
                ld    de,fsmx_cwdsl          ; "\" + saved path
fsl_cd
                ld    c,_CHDIR
                call  BDOS
                pop   ix
                jp    fsmx_restore_page

; --- directory enumeration ---------------------------------------------------
; DOS has one live browse context. Preserve it when a window is suspended;
; merely repainting a window or polling the same focus does not call DOS.
FSMX_CTX_LEN    equ   66                      ; drive slot, backslash, ASCIIZ _GETCD (64)
fsmx_window_ctx ld    hl,fsmx_window_dirs
                ld    de,FSMX_CTX_LEN
                or    a
                ret   z
                ld    b,a
fsmx_ctx_next   add   hl,de
                djnz  fsmx_ctx_next
                ret

fsmx_save_window
                call  fsmx_window_ctx
fsmx_save_ctx   ld    a,(fs_cur_drive)
                ld    (hl),a
                push  hl
                inc   hl
                ld    (hl),92
                inc   hl
                ex    de,hl
                ei
                push  ix
                ld    b,0
                ld    c,_GETCD
                call  BDOS
                pop   ix
                pop   hl
                or    a
                jr    z,fsmx_save_done
                ld    (hl),#FF                 ; never restore a partial pathname
fsmx_save_done  jp    fsmx_restore_page

fsmx_load_ctx   ld    a,(hl)
                cp    #FF
                ret   z
                inc   hl
                push  hl
                call  fs_set_drive
                pop   de                       ; absolute path, including root
                ei
                push  ix
                ld    c,_CHDIR
                call  BDOS
                pop   ix
                jp    fsmx_restore_page

fsmx_focus_dir  ld    hl,WM_FOCUS
                ld    a,(fsmx_cwd_owner)
                cp    (hl)
                ret   z
                call  wm_entry
                ld    de,WM_FR_FLAGS
                add   hl,de
                bit   0,(hl)                   ; no need to snapshot a closing window
                ld    a,(fsmx_cwd_owner)
                call  nz,fsmx_save_window
                ld    a,(WM_FOCUS)
                ld    (fsmx_cwd_owner),a
                call  fsmx_window_ctx
                jr    fsmx_load_ctx

; fs_dir_first / fs_dir_next -> CF set = entry ready in fs_ent_*, NC = done.
fs_dir_first
                ei
                push  ix
                ld    c,_FFIRST
                ld    de,fsmx_star           ; "*.*" = every entry in the current dir
                ld    b,FS_ATTR_DIRS
                ld    ix,fsmx_fib
                call  BDOS
                pop   ix
                call  fsmx_restore_page
                jr    fsd_result
fs_dir_next
                ei
                push  ix
                ld    c,_FNEXT
                ld    ix,fsmx_fib
                call  BDOS
                pop   ix
                call  fsmx_restore_page
fsd_result
                or    a
                ret   nz                      ; error/end -> NC
                ld    a,(fsmx_fib+FIB_NAME)
                cp    '.'                     ; File Manager supplies its own ".." entry
                jr    z,fs_dir_next
                jr    fsd_fill

; fsd_fill: FIB -> fs_ent_name (11-byte padded 8.3), fs_ent_attr, fs_ent_size.
fsd_fill
                ld    hl,fs_ent_name         ; blank the 11-byte name
                ld    de,fs_ent_name+1
                ld    bc,10
                ld    (hl),' '
                ldir
                ld    hl,fsmx_fib+FIB_NAME   ; "NAME.EXT",0 -> padded 8.3
                ld    de,fs_ent_name
                ld    b,8
fsdf_name
                ld    a,(hl)
                or    a
                jr    z,fsdf_done
                cp    '.'
                jr    z,fsdf_dot
                ld    (de),a
                inc   hl
                inc   de
                djnz  fsdf_name
                ld    a,(hl)                  ; 8 name chars used: next is '.' or NUL
                cp    '.'
                jr    nz,fsdf_done
fsdf_dot
                inc   hl                       ; skip the '.'
                ld    de,fs_ent_name+8
                ld    b,3
fsdf_ext
                ld    a,(hl)
                or    a
                jr    z,fsdf_done
                ld    (de),a
                inc   hl
                inc   de
                djnz  fsdf_ext
fsdf_done
                ld    a,(fsmx_fib+FIB_ATTR)
                ld    (fs_ent_attr),a
                ld    hl,(fsmx_fib+FIB_SIZE)
                ld    (fs_ent_size),hl
                ld    hl,(fsmx_fib+FIB_SIZE+2)
                ld    (fs_ent_size+2),hl
                scf
                ret

; --- load ---------------------------------------------------------------------
; fs_load_file: load fs_req_name (11-byte 8.3) into (fs_load_dst), capped at
; (fs_load_max). CF set = loaded, fs_ent_size = byte count. NC = missing or
; too big for the buffer (CPC contract).
fs_load_file
                ei
                push  ix
                call  fsmx_name_to_path      ; fs_req_name -> fsmx_path ASCIIZ
                ld    c,_DOPEN
                ld    de,fsmx_path
                ld    a,1                     ; open mode: no write
                call  BDOS
                or    a
                jp    nz,fsload_miss
                ld    a,b
                ld    (fsmx_handle),a
                ld    a,(FS_XFLAGS)          ; chunked copy? read a chunk from FS_LOAD_OFS,
                and   1                       ; no too-big probe (the caller loops to EOF)
                jr    z,fsload_start
                call  fsmx_seek_ofs          ; seek handle to FS_LOAD_OFS (from start)
fsload_start
                ld    hl,(fs_load_dst)
                ld    (fsmx_dst),hl
                ld    hl,(fs_load_max)
                ld    (fsmx_rem),hl
                ld    hl,0
                ld    (fsmx_total),hl
fsload_loop
                ld    hl,(fsmx_rem)
                ld    a,h
                or    l
                jr    z,fsload_maxed
                ld    a,h
                cp    2
                jr    c,fsload_short
                ld    hl,FSMX_BUFSZ
fsload_short
                ld    (fsmx_chunk),hl
                ld    a,(fsmx_handle)
                ld    b,a
                ld    c,_READ
                ld    de,FSMX_IOBUF
                ld    hl,(fsmx_chunk)
                call  BDOS
                or    a
                jr    nz,fsload_readfail
                ld    (fsmx_actual),hl
                ld    a,h
                or    l
                jr    z,fsload_success       ; EOF
                push  hl
                call  fsmx_restore_page      ; copy from resident buffer into the mapped caller page
                pop   bc
                ld    hl,FSMX_IOBUF
                ld    de,(fsmx_dst)
                ldir
                ld    hl,(fsmx_dst)
                ld    de,(fsmx_actual)
                add   hl,de
                ld    (fsmx_dst),hl
                ld    hl,(fsmx_total)
                ld    de,(fsmx_actual)
                add   hl,de
                ld    (fsmx_total),hl
                ld    hl,(fsmx_rem)
                ld    de,(fsmx_actual)
                or    a
                sbc   hl,de
                ld    (fsmx_rem),hl
                ld    hl,(fsmx_actual)
                ld    de,(fsmx_chunk)
                or    a
                sbc   hl,de
                jr    nz,fsload_success      ; short read -> EOF
                jr    fsload_loop
fsload_maxed
                ld    a,(FS_XFLAGS)
                and   1
                jr    nz,fsload_success      ; chunked caller loops to EOF; no too-big probe
                ld    a,(fsmx_handle)        ; probe: 1 more byte readable = too big
                ld    b,a
                ld    c,_READ
                ld    de,fsmx_probe
                ld    hl,1
                call  BDOS
                or    a
                jr    nz,fsload_readfail
                ld    a,h
                or    l
                jr    nz,fsload_toobig
fsload_success
                ld    hl,(fsmx_total)
                ld    (fs_ent_size),hl
                ld    hl,0
                ld    (fs_ent_size+2),hl
                ld    a,(fsmx_handle)
                ld    b,a
                ld    c,_DCLOSE
                call  BDOS
                pop   ix
                call  fsmx_restore_page
                scf
                ret
fsload_toobig
fsload_readfail
                ld    a,(fsmx_handle)
                ld    b,a
                ld    c,_DCLOSE
                call  BDOS
fsload_miss
                pop   ix
                call  fsmx_restore_page
                or    a
                ret

; fs_load_cur_sys: load from /GBENCH on the current DOS drive.
fs_load_cur_sys
                call  fs_sysdir_enter
                call  fs_load_file
                push  af
                call  fs_sysdir_leave
                pop   af
                ret

; fs_load_sys: try /GBENCH on the boot drive first, restore the browse drive,
; then fall back to its current directory. This is the same contract used by
; CPC/PCW and lets an app on drive B use modules from the system disk.
fs_load_sys
                ld    a,(fs_cur_drive)
                ld    (fls_browse),a
                ld    a,(fs_boot_drive)
                call  fs_set_drive
                call  fs_load_cur_sys
                push  af
                ld    a,(fls_browse)
                call  fs_set_drive
                pop   af
                ret   c
                jp    fs_load_file

; --- write ops ----------------------------------------------------------------
; fs_save_file: write fs_save_len bytes from (fs_save_src) to fs_req_name in
; the current directory. _CREATE truncates an existing file (the CPC contract
; only promised overwrite-in-place; BDOS handles grow/shrink). CF set = saved.
fs_save_file
                ei
                push  ix
                call  fsmx_name_to_path
                call  fsmx_trap_errors
                ld    a,(FS_XFLAGS)
                and   2                       ; append mode? open existing + seek to end
                jr    nz,fsave_append
                ld    c,_CREATE
                ld    de,fsmx_path
                xor   a                       ; open mode 0 (read/write)
                ld    b,a                     ; attributes 0
                call  BDOS
                or    a
                jr    nz,fsave_fail
                ld    a,b
                ld    (fsmx_handle),a
                jr    fsave_do
fsave_append
                ld    c,_DOPEN
                ld    de,fsmx_path
                xor   a                       ; mode 0 = read+write
                call  BDOS
                or    a
                jr    nz,fsave_fail
                ld    a,b
                ld    (fsmx_handle),a
                ld    b,a                     ; B = handle; seek to end (method 2, offset 0)
                ld    a,2
                ld    de,0
                ld    hl,0
                ld    c,_SEEK
                call  BDOS
                or    a
                jr    nz,fsave_closefail
fsave_do
                call  fsmx_restore_page      ; ensure _WRITE below reads the mapped app page
                ld    a,(fsmx_handle)         ; BDOS clobbers B on the seek; reload the handle
                ld    b,a
                ld    c,_WRITE
                ld    de,(fs_save_src)
                ld    hl,(fs_save_len)
                call  BDOS
                or    a
                jr    nz,fsave_closefail
                ld    de,(fs_save_len)
                sbc   hl,de                  ; _WRITE reports the actual count
                jr    nz,fsave_closefail
                ld    a,(fsmx_handle)
                ld    b,a
                ld    c,_DCLOSE
                call  BDOS
                or    a
                jr    nz,fsave_fail
                scf
                jr    fsave_return
fsave_closefail
                ld    a,(fsmx_handle)
                ld    b,a
                ld    c,_DCLOSE
                call  BDOS
fsave_fail
                or    a
fsave_return    push  af
                call  fsmx_untrap_errors
                pop   af
                pop   ix
                jp    fsmx_restore_page

; fs_delete_file: delete fs_req_name from the current directory. CF = deleted.
fs_delete_file
                ei
                push  ix
                call  fsmx_name_to_path
                ld    c,_DELETE
                ld    de,fsmx_path
                call  BDOS
                pop   ix
                call  fsmx_restore_page
                sub   1                       ; A=0 (no error) -> CF set
                ret

; fs_free_kib: HL = free KiB on the current drive, CF set (known).
; _ALLOC: A = sectors/cluster (power of 2), BC = sector size (512),
; HL = free clusters -> KiB = free * spc / 2, clamped to #FFFF.
fs_free_kib
                ei
                push  ix
                ld    c,_ALLOC
                ld    e,0                     ; current drive
                call  BDOS
                ld    e,a                     ; E = sectors per cluster
ffk_lp          ld    a,e
                cp    2
                jr    c,ffk_half              ; E == 1 -> final /2
                add   hl,hl                   ; KiB *= 2 per remaining spc doubling
                jr    nc,ffk_ok
                ld    hl,#FFFF                ; overflow -> clamp
ffk_ok          srl   e
                jr    ffk_lp
ffk_half        srl   h                       ; /2: two 512-byte sectors per KiB
                rr    l
                pop   ix
                call  fsmx_restore_page
                scf
                ret

; --- GB_CHDIR / GB_BACK --------------------------------------------------------
; fsmx_chdir: descend into the positioned entry (fs_ent_name).
fsmx_chdir
                ei
                push  ix
                ld    hl,fs_ent_name
                ld    de,fsmx_path
                call  fsmx_n2p_copy
                ld    c,_CHDIR
                ld    de,fsmx_path
                call  BDOS
                pop   ix
                jp    fsmx_restore_page

; fsmx_back: up one level.
fsmx_back
                ei
                push  ix
                ld    c,_CHDIR
                ld    de,fsmx_dotdot
                call  BDOS
                pop   ix
                jp    fsmx_restore_page

; BDOS/Nextor may restore page 1 to the process TPA segment on return, even when
; GEOBENCH entered DOS with another mapper segment selected. Re-assert the kernel's
; page shadow before returning to app/kernel code that expects APP_BASE to still be
; the caller's mapped segment.
fsmx_restore_page
                push  af
                ld    a,(bank_cur)
                call  bank_set
                pop   af
                ret

; fsmx_seek_ofs: seek fsmx_handle to the 24-bit FS_LOAD_OFS, from the start (method 0).
fsmx_seek_ofs
                ld    a,(FS_LOAD_OFS)
                ld    l,a
                ld    a,(FS_LOAD_OFS+1)
                ld    h,a                     ; HL = low word of the offset
                ld    a,(FS_LOAD_OFS+2)
                ld    e,a
                ld    d,0                     ; DE = high word (24-bit, so D=0)
                ld    a,(fsmx_handle)
                ld    b,a
                ld    a,0                     ; method 0 = from start
                ld    c,_SEEK
                call  BDOS
                jp    fsmx_restore_page

; --- name conversion ------------------------------------------------------------
; fsmx_name_to_path: fs_req_name (11 bytes, space padded) -> fsmx_path ASCIIZ.
fsmx_name_to_path
                ld    hl,fs_req_name
                ld    de,fsmx_path
fsmx_n2p_copy                                  ; HL = 11-byte name, DE = dst ASCIIZ
                ld    b,8
fnp_name
                ld    a,(hl)
                cp    ' '
                jr    z,fnp_nskip
                ld    (de),a
                inc   de
fnp_nskip
                inc   hl
                djnz  fnp_name
                ld    a,(hl)                  ; any extension?
                cp    ' '
                jr    z,fnp_term
                ld    a,'.'
                ld    (de),a
                inc   de
                ld    b,3
fnp_ext
                ld    a,(hl)
                cp    ' '
                jr    z,fnp_term
                ld    (de),a
                inc   de
                inc   hl
                djnz  fnp_ext
fnp_term
                xor   a
                ld    (de),a
                ret

; --- state ------------------------------------------------------------------------
FSMX_IOBUF      equ   #1800                    ; resident 512-byte sector buffer
FSMX_BUFSZ      equ   #0200
fsmx_star       db    "*.*",0                 ; "*" alone matches only ext-less names
fsmx_sysdir     db    92,"GBENCH",0           ; "\GBENCH" (the staged system folder)
fsmx_root       db    92,0                    ; "\" (root)
fsmx_dotdot     db    "..",0
fsmx_target_slot db   0
fsmx_target_dos db    0
fsmx_probe_drive db   0
fsmx_probe_type db    0
fsmx_probe_hint db    0
fsmx_probe_error db   0
fsmx_scan_drive db    0
fsmx_login      dw    0
fsmx_handle     db    0
fsmx_probe      db    0
fsmx_dst        dw    0
fsmx_rem        dw    0
fsmx_chunk      dw    0
fsmx_actual     dw    0
fsmx_total      dw    0
fsmx_cwdsl      db    92                      ; "\" prefix, falls into the CWD buffer
fsmx_cwd        ds    64                      ; _GETCD result (no leading slash)
fsmx_path       ds    16                      ; "FILENAME.EXT",0
fsmx_fib        ds    64                      ; the _FFIRST/_FNEXT fileinfo block
fsmx_cwd_owner  db    0
fsmx_window_dirs ds   WM_MAXWIN*FSMX_CTX_LEN
fsmx_drag_source ds   FSMX_CTX_LEN
fsmx_copy_dest   ds   FSMX_CTX_LEN
fs_dir_clus     ds    4                       ; dummy: k_copy_begin/end context swap

; fixed low-RAM cells: the same contract addresses as the CPC dispatcher
fs_cur_drive    equ   #1335
fs_boot_drive   equ   #1336
fls_browse      equ   #1337
fs_ent_name     equ   #14DC
fs_ent_attr     equ   #14E7
fs_ent_size     equ   #14E8
fs_req_name     equ   #14EC
fs_load_dst     equ   #14F7
fs_load_max     equ   #14F9
fs_save_src     equ   #14FB
fs_save_len     equ   #14FD
; chunked-copy cells (the #144C..#144F free gap, shared with the CPC dispatcher):
FS_LOAD_OFS     equ   #144C        ; 24-bit read offset (used when FS_XFLAGS bit0 set)
FS_XFLAGS       equ   #144F        ; bit0 = chunk-read (read from offset, no too-big),
                                    ; bit1 = append-write (open existing + seek to end)
