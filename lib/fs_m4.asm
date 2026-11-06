; ---------------------------------------------------------------------------
; Active M4 storage backend (#174/#259). The normal build now ships GBM4.BIN
; beside GBALB.BIN on the shared FAT card image.
; ---------------------------------------------------------------------------
; lib/fs_m4.asm - storage backend: FAT over the M4 board (file-level, #174).
;
; Like the Albireo CH376 (fs_albireo.asm), the M4 firmware owns the filesystem -
; we do NOT parse FAT here (contrast fs_ide_fat.asm). We issue the M4's high-level
; file commands and the board walks the directory / file. So this build does NOT
; include fs_fat32_core.asm / fs_ide_read.asm at all - no IDE FAT core resident.
;
; M4 command protocol (mirrors Duke's M4ROM.s in ../m4rom + 1984 src/m4.c):
;   * write a packet [#00 lead][cmd_lo][cmd_hi=#43][params...] one byte at a time
;     to DATAPORT (#FE00), then strobe ACKPORT (#FC00) - the board bus-HOLDS the
;     OUT until the command (incl. the SD access) completes, so no poll/NMI wait.
;   * read the response from rom_response (#E800), visible only while M4ROM (upper
;     ROM slot 6) is the selected upper ROM. Response = [len@0][cmd_lo@1][#43@2]
;     [data@3..]; resp_frame sets len = (#data + 2), so #E800+0 == 2 means "no data"
;     (C_READDIR end-of-directory). Per-command status lives in the data area (+3..).
;   GEOBENCH normally runs with the upper ROM enabled, so each command preserves
;   the current video mode while paging slot 6 (#DF00=6) for send+ack+read, then
;   restores AMSDOS (#DF00=7).
;
; Backend entry points (same shape as fsalb_*/fside_* so lib/fs.asm routes here):
;   fsm4_dir_first -> CF set = first entry in fs_ent_*, NC = empty
;   fsm4_dir_next  -> CF set = next entry,               NC = end of dir
;   fsm4_load_file -> CF set = loaded (fs_ent_size = bytes), NC = not found
;   fsm4_save_file -> CF set = saved, NC = failed (paged M4SAVE.MOD)
;   fsm4_delete_file -> CF set = deleted, NC = failed/unsupported.
;
; M4 directory model: C_READDIR enumerates the board's CURRENT directory, so each
; listing first C_CD's to m4_path (absolute, rebuilt from the path string every time
; - like fs_albireo - so chdir/back never desync). Loads use an absolute C_OPEN path,
; so they need no C_CD. NOTE: the M4's C_READDIR prefixes directories with '>' inside
; the 8-char name field, so an 8-character directory name loses its last char in the
; listing (files are unaffected). The system dir is entered by the hardcoded
; "/GEOBENCH" path (fs_sysdir_enter), so boot + app loading never depend on the listing.
; ---------------------------------------------------------------------------

M4_DATA         equ   #FE00        ; DATAPORT  (write command bytes; lead byte then cmd)
M4_ACK          equ   #FC00        ; ACKPORT   (strobe = execute; board bus-holds until done)
M4_RESP         equ   #E800        ; rom_response (read while M4ROM/slot 6 is selected)
M4_ROMNUM       equ   6            ; M4ROM expansion slot
SCR_GET_MODE    equ   #BC11        ; firmware: A = current screen mode
VIDEO_MODE_HINT equ   #14FF        ; bit 7 valid, bits 0..1 = caller's active mode

M4C_OPEN        equ   #01          ; C_OPEN   (#4301)
M4C_READ2       equ   #12          ; C_READ2  (#4312) - unbuffered, gives actual size + EOF
M4C_CLOSE       equ   #04          ; C_CLOSE  (#4304)
M4C_CD          equ   #08          ; C_CD     (#4308)
M4C_TIME        equ   #24          ; C_TIME   (#4324) - "HH:MM:SS YYYY-MM-DD"
M4C_DIRARGS     equ   #25          ; C_DIRSETARGS (#4325)
M4C_READDIR     equ   #06          ; C_READDIR (#4306)
M4_OPENMODE     equ   #81          ; FA_READ | FA_REALMODE (dynamic fd, bypass AMSDOS fd)
M4_READCHUNK    equ   #0800        ; bytes per C_READ2 (fits the #E800..#F3FF response window)

M4_PATH_MAX     equ   64           ; current-dir path buffer ("" = root, "/GEOBENCH")
M4SV_LEN        equ   #1700        ; M4SAVE.MOD transfer area (mirrors floppysv/gbfat)
M4SV_NAME       equ   #1702
M4SV_RES        equ   #170D
M4SV_PATH       equ   #1710
M4SV_DATA       equ   #2200

; --- transport --------------------------------------------------------------
; m4_io_begin: DI + select M4ROM (slot 6) so DATAPORT/ACKPORT are serviced and the
; response window maps at #E800. Upper ROM stays enabled, preserving Mode 2 clients.
m4_ga_upper_on
                push  de
                push  hl
                ld    a,(VIDEO_MODE_HINT)
                bit   7,a
                jr    nz,m4ga_have
                call  SCR_GET_MODE
m4ga_have       and   #03
                or    #84                    ; upper ROM ON + lower ROM OFF
                ld    b,#7F
                ld    c,a
                out   (c),c
                pop   hl
                pop   de
                ret

m4_io_begin
                call  m4_ga_upper_on
                di
                ld    bc,#DF00
                ld    a,M4_ROMNUM
                out   (c),a
                ret
; m4_io_end: restore AMSDOS (slot 7) + EI.
m4_io_end
                ld    bc,#DF00
                ld    a,7
                out   (c),a
                ei
                ret
; m4_lead: start a command packet - BC = DATAPORT, write the #00 lead byte (as M4ROM does).
m4_lead
                ld    bc,M4_DATA
                out   (c),c                  ; lead byte = C = #00
                ret
; m4_go: strobe ACKPORT - blocks (board bus-hold) until the command completes.
m4_go
                ld    bc,M4_ACK
                out   (c),c
                ret

; --- M4ROM-faithful buffered command send (#174) -----------------------------
; M4ROM (M4ROM.s send_command) builds the packet in a buffer with buf[0] = the SIZE
; (count of bytes after it) and sends size+1 bytes via OUTI, THEN strobes ACK. We
; were sending a #00 lead byte instead; for the file/root commands that worked, but
; the real M4 may need the true size for the directory state. These helpers mirror
; M4ROM exactly: build the command, then m4_send writes [size][cmd_lo][#43][params].
;
; m4_cmd: A = cmd_lo. Begin a command in m4_cmdbuf -> buf[1]=cmd_lo, buf[2]=#43.
;         Returns HL = buf+3 (the parameter write pointer).
m4_cmd
                ld    (m4_cmdbuf+1),a
                ld    a,#43
                ld    (m4_cmdbuf+2),a
                ld    hl,m4_cmdbuf+3
                ret
; m4_pb: append A to (HL), advance HL.
m4_pb           ld    (hl),a
                inc   hl
                ret
; m4_pstr: append the NUL-terminated string at DE to (HL), INCLUDING the NUL (M4ROM strcpy's
; the path with its terminator). HL advanced past the NUL.
m4_pstr         ld    a,(de)
                ld    (hl),a
                inc   hl
                inc   de
                or    a
                jr    nz,m4_pstr
                ret
; m4_send: HL = end-of-buffer pointer. Page in M4ROM (slot 6) and send the packet exactly as
; M4ROM does - buf[0] = size (= HL-(buf+1)), then OUTI size+1 bytes, then strobe ACK. Slot 6 is
; left paged so the caller can read the response from #E800, then call m4_io_end.
m4_send
                ld    de,m4_cmdbuf+1
                or    a
                sbc   hl,de                    ; HL = size (bytes after buf[0])
                ld    a,l
                ld    (m4_cmdbuf),a            ; buf[0] = size
                call  m4_ga_upper_on
                di
                ld    bc,#DF00
                ld    a,M4_ROMNUM
                out   (c),a
                ld    a,(m4_cmdbuf)
                inc   a                        ; size+1 bytes total (incl. the size byte)
                ld    hl,m4_cmdbuf
                ld    bc,M4_DATA             ; B=#FE, C=#00
ms_loop         inc   b                        ; keep the port high byte #FE across OUTI
                outi
                dec   a
                jr    nz,ms_loop
                ld    bc,M4_ACK
                out   (c),c                  ; strobe
                ret
; m4_emit_str: stream the NUL-terminated string at HL to DATAPORT (BC=#FE00), NUL not sent.
m4_emit_str
                ld    a,(hl)
                or    a
                ret   z
                out   (c),a
                inc   hl
                jr    m4_emit_str
; m4_emit_83: stream fs_req_name to DATAPORT as a TRIMMED 8.3 name ("GBCFG.BIN", not
; "GBCFG   .BIN"). D is the counter (B is the port high byte #FE). BC preserved.
m4_emit_83
                ld    hl,fs_req_name
                ld    d,8
me83_nm         ld    a,(hl)
                cp    ' '
                jr    z,me83_ext
                out   (c),a
                inc   hl
                dec   d
                jr    nz,me83_nm
me83_ext        ld    hl,fs_req_name+8
                ld    a,(hl)
                cp    ' '
                ret   z
                ld    a,'.'
                out   (c),a
                ld    d,3
me83_e1         ld    a,(hl)
                cp    ' '
                ret   z
                out   (c),a
                inc   hl
                dec   d
                jr    nz,me83_e1
                ret

; m4_open_path: emit the active path prefix for C_OPEN. Normal file loads use the
; browse path; fs_load_cur_sys/fs_load_sys set m4_sysflag so modules/apps/assets
; open from /GBENCH without copying the whole browse path away and back.
m4_open_path
                ld    hl,m4_path
                ld    a,(m4_sysflag)
                or    a
                jr    z,mop_have
                ld    hl,m4_sysdir
mop_have        ld    a,(hl)
                or    a
                ret   z
                jp    m4_emit_str

; m4_cd_path: set the M4's current directory to m4_path, REBUILT from the root: a
; C_CD "/" then a relative C_CD for each "/<component>". A real M4 doesn't reliably
; C_CD an ABSOLUTE subdir path (C_CD "/GBENCH" corrupts its dir state so every later
; listing - even root - comes back empty), whereas root + relative components is solid.
; This mirrors the Albireo cd-first fix; the emulator tolerated the absolute form.
; (No result read - the emulator doesn't echo C_CD's status.)
m4_cd_path
                call  m4_cd_root             ; C_CD "/"
                ld    hl,m4_path
mcd_comp        ld    a,(hl)
                or    a
                ret   z                       ; end of path -> cwd is rebuilt
                inc   hl                       ; skip the '/'
                call  m4_io_begin            ; C_CD <component> (relative)
                call  m4_lead
                ld    a,M4C_CD
                out   (c),a
                ld    a,#43
                out   (c),a
mcd_emit        ld    a,(hl)
                or    a
                jr    z,mcd_emit_done
                cp    '/'
                jr    z,mcd_emit_done
                out   (c),a
                inc   hl
                jr    mcd_emit
mcd_emit_done   xor   a
                out   (c),a                  ; NUL terminator
                call  m4_go
                call  m4_io_end
                jr    mcd_comp                 ; next "/<component>"

; m4_cd_root: C_CD "/" (reset to the root directory).
m4_cd_root
                call  m4_io_begin
                call  m4_lead
                ld    a,M4C_CD
                out   (c),a
                ld    a,#43
                out   (c),a
                ld    a,'/'
                out   (c),a
                xor   a
                out   (c),a
                call  m4_go
                jp    m4_io_end

; --- present + directory ----------------------------------------------------
; fsm4_present (D0_PRESENT): list the current dir (root at boot) - if it yields an
; entry the M4 SD is present + readable. CF set = present.
fsm4_present
; fsm4_dir_first: cd to m4_path, open the dir iterator, return the first entry. Sent the
; M4ROM way (m4_send + size byte) with an EMPTY filter - M4ROM's |dir/cat sends just a NUL,
; and sending "*" instead breaks SUBDIR enumeration on real M4 hardware (the emulator
; tolerated it; root happened to work either way). Confirmed on real HW (#174).
fsm4_dir_first
                call  m4_cd_path
                ld    a,M4C_DIRARGS           ; C_DIRSETARGS, EMPTY filter (just the NUL byte)
                call  m4_cmd
                xor   a
                call  m4_pb
                call  m4_send
                call  m4_io_end
                jr    fsm4_dir_next

; fsm4_dir_next: C_READDIR one entry; decode into fs_ent_*. Skips nothing extra (the
; M4 already drops '.'/'..'). CF set = entry ready, NC = end of directory.
fsm4_dir_next
                ld    a,M4C_READDIR           ; C_READDIR with NO args (M4ROM-exact; a maxlen
                call  m4_cmd                  ; byte breaks subdir enumeration on real HW, #174)
                call  m4_send
                ld    a,(M4_RESP)            ; length byte: == 2 means end-of-directory
                cp    2
                jr    z,mdn_end
                call  m4_decode_entry        ; fill fs_ent_* from #E800+3..
                call  m4_io_end
                scf
                ret
mdn_end
                call  m4_io_end
                ret                           ; matching CP left carry clear

; m4_decode_entry: parse the C_READDIR entry (slot 6 still paged) into fs_ent_*.
;   #E800+3..+10 = 8-char name (space-padded); '>' at +3 marks a directory (its name
;                  is then +4.., 7 chars). +12..+14 = 3-char ext. +21..+22 = size (16-bit).
m4_decode_entry
                ld    a,(M4_RESP+3)          ; directory? ('>' prefix)
                cp    '>'
                jr    z,mde_dir
                ld    hl,M4_RESP+3           ; file: 8-char name straight across
                ld    de,fs_ent_name
                ld    bc,8
                ldir
                xor   a
                ld    (fs_ent_attr),a        ; attr 0 = a file
                jr    mde_ext
mde_dir
                ld    hl,M4_RESP+4           ; dir: name is +4.. (7 chars), then pad
                ld    de,fs_ent_name
                ld    bc,7
                ldir
                ld    a,' '
                ld    (de),a                  ; pad fs_ent_name[7]
                ld    a,#10                   ; attr bit4 = directory
                ld    (fs_ent_attr),a
mde_ext
                ld    hl,M4_RESP+12          ; 3-char extension
                ld    de,fs_ent_name+8
                ld    bc,3
                ldir
                ld    hl,(M4_RESP+21)        ; 16-bit size -> fs_ent_size (high word 0)
                ld    (fs_ent_size),hl
                ld    hl,0
                ld    (fs_ent_size+2),hl
                ret

; fsm4_free_kib: run M4SAVE.MOD with the #FFFE free-space operation. The module
; owns the M4 C_FREE command + ASCII parse so that logic does not live resident.
fsm4_free_kib
                ld    hl,#FFFE
                ld    (M4SV_LEN),hl
                ld    hl,m4save_modname
                call  run_data_module
                ld    a,(M4SV_RES)
                or    a
                ret   z
                ld    hl,(M4SV_LEN)
                scf
                ret

; --- file load --------------------------------------------------------------
; fsm4_load_file: C_OPEN the absolute path m4_path + "/" + fs_req_name, then C_READ2
; #0800-byte chunks into (fs_load_dst) until EOF (status #20), guarding fs_load_max.
; fs_ent_size := bytes read. CF set = loaded, NC = not found / too big.
fsm4_load_file
                ld    a,(FS_XFLAGS)
                and   1
                jp    nz,fsm4_load_chunk_mod
                call  m4_io_begin            ; --- C_OPEN <mode><path> ---
                call  m4_lead
                ld    a,M4C_OPEN
                out   (c),a
                ld    a,#43
                out   (c),a
                ld    a,M4_OPENMODE
                out   (c),a                  ; mode: FA_READ | dynamic fd
                call  m4_open_path           ; path prefix (browse path or /GBENCH)
mlf_slash       ld    a,'/'
                out   (c),a
                call  m4_emit_83             ; trimmed 8.3 name
                xor   a
                out   (c),a                  ; NUL
                call  m4_go
                ld    a,(M4_RESP+4)          ; open error (0 = OK)
                or    a
                jr    nz,mlf_nf_io
                ld    a,(M4_RESP+3)          ; file descriptor
                ld    (m4_fd),a
                call  m4_io_end
                ld    hl,0                    ; fs_ent_size = 0
                ld    (fs_ent_size),hl
                ld    (fs_ent_size+2),hl
mlf_loop
                call  m4_read_chunk          ; C_READ2 -> copy actual bytes, advance
                jr    nc,mlf_toobig          ; NC = chunk would overflow fs_load_max
                ; EOF = a PARTIAL chunk (actual < #0800). Detect it by the SIZE field, NOT the
                ; status byte: the emulator reports EOF status #14 but M4ROM/real HW use #20
                ; (cp #20 in M4ROM.s) - reading past EOF on real HW returns garbage and reboots.
                ld    a,(m4_actual+1)        ; actual-size high byte (#0800 -> #08)
                cp    #08
                jr    c,mlf_done             ; actual < #0800 -> last chunk
                jr    mlf_loop               ; actual == #0800 -> more data follows
mlf_done
                call  m4_close_fd
                scf
                ret
mlf_nf_io
                jp    m4_io_end               ; open-error OR left carry clear
mlf_toobig
                call  m4_close_fd
                or    a
                ret

; m4_read_chunk: C_READ2 m4_fd, #0800 -> guard against fs_load_max, copy the actual
; bytes from #E808 into (fs_load_dst), advance fs_load_dst + fs_ent_size.
; Returns CF set + A = M4 status (#20 = EOF), or NC = chunk overflows fs_load_max.
m4_read_chunk
                call  m4_io_begin
                call  m4_lead
                ld    a,M4C_READ2
                out   (c),a
                ld    a,#43
                out   (c),a
                ld    a,(m4_fd)
                out   (c),a
                xor   a
                out   (c),a                  ; count lo (both requests are aligned)
                ld    a,(FS_XFLAGS)
                rlca                         ; bit 7 selects GBAPICK's 1 KiB prefix
                ld    a,#04
                jr    c,mrc_count_ready
                add   a,a                    ; ordinary chunk: #0800
mrc_count_ready out   (c),a                  ; count hi (#04 probe, #08 ordinary)
                call  m4_go
                ld    hl,(M4_RESP+4)        ; actual bytes read this chunk
                ld    (m4_actual),hl
                ; guard: refuse if fs_ent_size + actual would exceed fs_load_max
                ld    de,(fs_ent_size)
                add   hl,de
                ld    de,(fs_load_max)
                or    a
                sbc   hl,de
                jr    z,mrc_ok               ; total == max: ok
                jr    c,mrc_ok               ; total <  max: ok
                call  m4_io_end             ; total > max: abort (NC)
                or    a
                ret
mrc_ok
                ld    hl,(m4_actual)         ; copy actual bytes #E808 -> (fs_load_dst)
                ld    a,h
                or    l
                jr    z,mrc_advance          ; zero-length chunk: nothing to copy
                ld    bc,(m4_actual)
                ld    hl,M4_RESP+8
                ld    de,(fs_load_dst)
                ldir
                ld    (fs_load_dst),de        ; ldir left DE past the copy
                ld    hl,(fs_ent_size)        ; fs_ent_size += actual
                ld    de,(m4_actual)
                add   hl,de
                ld    (fs_ent_size),hl
                jr    nc,mrc_advance
                ld    hl,(fs_ent_size+2)
                inc   hl
                ld    (fs_ent_size+2),hl
mrc_advance
                call  m4_io_end
                scf
                ret

; m4_close_fd: C_CLOSE m4_fd.
m4_close_fd
                call  m4_io_begin
                call  m4_lead
                ld    a,M4C_CLOSE
                out   (c),a
                ld    a,#43
                out   (c),a
                ld    a,(m4_fd)
                out   (c),a
                call  m4_go
                jp    m4_io_end

; fsm4_seed_clock: ask M4ROM for its NTP-backed time and seed the kernel's
; software clock. C_TIME returns ASCII at M4_RESP+3: "HH:MM:SS YYYY-MM-DD".
fsm4_seed_clock
                ld    a,M4C_TIME
                call  m4_cmd
                call  m4_send
                ld    hl,M4_RESP+3
                call  m4_parse_2d
                jr    nc,m4tc_done
                cp    24
                jr    nc,m4tc_done
                ld    d,a                      ; D = hour
                ld    a,(hl)
                cp    ':'
                jr    nz,m4tc_done
                inc   hl
                call  m4_parse_2d
                jr    nc,m4tc_done
                cp    60
                jr    nc,m4tc_done
                ld    e,a                      ; E = minute
                ld    a,(hl)
                cp    ':'
                jr    nz,m4tc_done
                inc   hl
                call  m4_parse_2d
                jr    nc,m4tc_done
                cp    60
                jr    nc,m4tc_done
                ld    (sw_sec),a
                ld    a,d
                ld    (sw_hour),a
                ld    a,e
                ld    (sw_min),a
                xor   a
                ld    (clk_frames),a
m4tc_done
                jp    m4_io_end

; m4_parse_2d: HL -> two ASCII digits. Returns CF set, A = binary value,
; HL advanced past the digits; NC if either byte is not a digit.
m4_parse_2d
                ld    a,(hl)
                sub   '0'
                cp    10
                ret   nc
                ld    b,a
                inc   hl
                ld    a,(hl)
                sub   '0'
                cp    10
                ret   nc
                ld    c,a
                ld    a,b
                add   a,a                     ; tens * 2
                ld    b,a
                add   a,a                     ; tens * 4
                add   a,a                     ; tens * 8
                add   a,b                     ; tens * 10
                add   a,c
                inc   hl
                scf
                ret

; --- file save --------------------------------------------------------------
; fsm4_save_file: resident stub. Stage the caller's bytes/name/current M4 path
; into low RAM, then load M4SAVE.MOD from /GBENCH and let that module perform the
; C_OPEN/C_WRITE/C_CLOSE sequence. CF set = saved, NC = failed. Delete uses the
; same module with M4SV_LEN=#FFFF so C_ERASEFILE stays out of the resident kernel.
fsm4_load_chunk_mod
                xor   a
                ld    (FS_XFLAGS),a
                ld    hl,#FFFC
                call  msv_common
                ret   nc
                ld    hl,(M4SV_LEN)
                jp    fs_chunk_to_dst

fsm4_delete_file
                ld    hl,#FFFF
                jr    msv_common

fsm4_save_file
                ld    hl,(fs_save_len)       ; low-RAM staging cap
                if PREEMPTIVE
                ld    de,#1A01
                else
                ld    de,#1C01
                endif
                or    a
                sbc   hl,de
                jr    nc,msv_fail
                ld    hl,(fs_save_src)
                ld    de,M4SV_DATA
                ld    bc,(fs_save_len)
                ld    a,b
                or    c
                jr    z,msv_nodata
                ldir
msv_nodata      ld    hl,(fs_save_len)
msv_common      ld    (M4SV_LEN),hl
                ld    hl,fs_req_name
                ld    de,M4SV_NAME
                ld    bc,11
                ldir
                ld    hl,m4_path
                ld    de,M4SV_PATH
                ld    bc,M4_PATH_MAX
                ldir
                ld    hl,m4save_modname
                call  run_data_module
                ld    a,(M4SV_RES)
                or    a
                ret   z
                scf
                ret
msv_fail
                or    a
                ret
m4save_modname db    "M4SAVE  MOD"

; --- #134 system-dir hooks (path-based, like Albireo) -----------------------
; The M4 is path-based: system loads run from the "/GBENCH" prefix regardless of
; the File Manager's browse path. A one-byte flag is enough because C_OPEN chooses
; the prefix; directory browsing keeps using m4_path.
fs_sys_resolve
                ret
fs_sysdir_enter
                ld    a,1
                ld    (m4_sysflag),a
                ret
fs_sysdir_leave
                xor   a
                ld    (m4_sysflag),a
                ret
m4_sysdir       db    "/GBENCH",0   ; #174: <=7 chars so the M4's '>'-prefixed dir listing
                                     ; round-trips it (an 8-char dir name loses its last char)

; --- chdir / back (edit the m4_path string, like fsalb_chdir/back) -----------
; fsm4_chdir (k_chdir): descend into the positioned folder by appending "/<name>" to
; m4_path. fs_ent_name holds the folder's 8.3 name. Refuses if the path would overflow.
fsm4_chdir
                ld    hl,m4_path
                ld    b,0
fmc_end         ld    a,(hl)
                or    a
                jr    z,fmc_atend
                inc   hl
                inc   b
                jr    fmc_end
fmc_atend       ld    a,b
                cp    M4_PATH_MAX-14
                ret   nc                       ; too deep -> ignore
                ld    (hl),'/'
                inc   hl
                ld    de,fs_ent_name           ; append up to 8 name chars (stop at space)
                ld    b,8
fmc_name        ld    a,(de)
                cp    ' '
                jr    z,fmc_ext
                ld    (hl),a
                inc   hl
                inc   de
                djnz  fmc_name
fmc_ext         ld    de,fs_ent_name+8         ; extension
                ld    a,(de)
                cp    ' '
                jr    z,fmc_done
                ld    (hl),'.'
                inc   hl
                ld    b,3
fmc_extc        ld    a,(de)
                cp    ' '
                jr    z,fmc_done
                ld    (hl),a
                inc   hl
                inc   de
                djnz  fmc_extc
fmc_done        ld    (hl),0
                ret

; fsm4_back (k_back): go up one level - truncate m4_path at its last '/'. Root is a no-op.
fsm4_back
                ld    hl,m4_path
                ld    de,0
fmb_scan        ld    a,(hl)
                or    a
                jr    z,fmb_done
                cp    '/'
                jr    nz,fmb_next
                ld    d,h
                ld    e,l
fmb_next        inc   hl
                jr    fmb_scan
fmb_done        ld    a,d
                or    e
                ret   z
                ex    de,hl
                ld    (hl),0
                ret

; --- state ------------------------------------------------------------------
m4_path         defs  M4_PATH_MAX  ; current dir as an M4 path (zero-init = root)
m4_sysflag      defb  0            ; nonzero while fs_load_sys opens from /GBENCH
m4_fd           defb  0            ; the open file's descriptor
m4_actual       defw  0            ; last C_READ2 byte count
m4_cmdbuf       defs  80           ; M4ROM-style command buffer ([0]=size, then cmd+params)

; Shared directory-context state the kernel manipulates generically (k_chdir/k_back,
; cross-drive copy). The M4 tracks the current dir by path and ignores fs_dir_clus -
; these just satisfy the kernel's refs.
fs_dir_clus     defs  4
fs_dir_sp       defb  0
fs_dir_stack    defs  16
fs_ent_clus     defs  4
