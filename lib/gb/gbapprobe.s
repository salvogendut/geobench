;; gbapprobe.s - CPC-only APP preamble reader.
;;
;; A large .APP cannot be partially read by the ordinary CPC loader, while the
;; chunk loaders page a storage module over GBAPICK.MOD. Copy this position-
;; independent helper to low RAM, borrow a free app page, load the candidate
;; there, and return its first 1 KiB without displacing the calling app/module.

        .module gbapprobe
        .globl  _gb_app_probe

PROBE_EXEC      = 0x3B80
PIC_EDIT_BUF    = 0x134B
BANK_CUR        = 0x134F
APP_NPAGES      = 0x1437
APP_PAGES       = 0x1438
APP_BUSY        = 0x1440
FS_XFLAGS       = 0x144F
GB_FSLOAD       = 0x803F
GB_GETDRIVE     = 0x8084
BANK_PORT       = 0x7F00
APP_BASE        = 0x4000
APP_LOAD_MAX    = 0x3F00
PROBE_LEN       = 0x0400

        .area   _CODE

;; unsigned char gb_app_probe(char *dst);
;; The focused window name must already identify the candidate APP.
_gb_app_probe:
        ld      (PIC_EDIT_BUF), hl
        call    GB_GETDRIVE
        or      a
        jr      nz, probe_whole

        ;; Drive C may use M4, whose resident backend recognizes bit 7 as a
        ;; bounded prefix request. Albireo/IDE safely reject the 1 KiB cap and
        ;; fall through to the existing whole-file probe below. Floppy drives
        ;; skip this path because their chunk loader would page over GBAPICK.
        ld      a, #0x80
        ld      (FS_XFLAGS), a
        ld      hl, (PIC_EDIT_BUF)
        ld      de, #PROBE_LEN
        call    GB_FSLOAD
        push    af
        xor     a
        ld      (FS_XFLAGS), a
        pop     af
        jr      nc, probe_whole
        ld      a, b
        cp      #4
        jr      c, probe_whole
        ld      a, #1
        ret

probe_whole:
        ld      hl, #probe_low_start
        ld      de, #PROBE_EXEC
        ld      bc, #probe_low_end-probe_low_start
        ldir
        call    PROBE_EXEC
        ret

;; Everything below is copied to PROBE_EXEC before it runs. Keep internal
;; control transfers relative so the copied image remains position-independent.
probe_low_start:
        ld      a, (APP_NPAGES)
        or      a
        ret     z
        ld      b, a
        ld      hl, #APP_BUSY
        ld      de, #APP_PAGES
probe_scan:
        ld      a, (hl)
        or      a
        jr      z, probe_take
        inc     hl
        inc     de
        djnz    probe_scan
        xor     a
        ret

probe_take:
        ld      (hl), #1
        push    hl                      ; busy-flag address
        ld      a, (BANK_CUR)
        push    af                      ; caller's mapped page
        ld      a, (de)
        ld      (BANK_CUR), a
        ld      bc, #BANK_PORT
        out     (c), a

        xor     a
        ld      (FS_XFLAGS), a          ; force the ordinary whole-file loader
        ld      hl, #APP_BASE
        ld      de, #APP_LOAD_MAX
        call    GB_FSLOAD               ; safe: code and stack are below #4000
        ld      d, #0
        jr      nc, probe_restore
        ld      a, b                    ; every icon-bearing APP must provide 1 KiB
        cp      #4
        jr      c, probe_restore
        ld      hl, #APP_BASE
        ld      de, (PIC_EDIT_BUF)
        ld      bc, #PROBE_LEN
        ldir
        ld      d, #1

probe_restore:
        pop     af
        ld      (BANK_CUR), a
        ld      bc, #BANK_PORT
        out     (c), a
        pop     hl
        ld      (hl), #0
        ld      a, d
        ret
probe_low_end:
