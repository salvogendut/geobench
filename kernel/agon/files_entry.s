; SDCC's first two pointer arguments already arrive in HL and DE.
        .module files_entry
        .globl _browse, _dir_open, _dir_read, _dir_close, _file_key, _fm_document
        .globl _fm_reset, _fm_first, _fm_next, _fm_raw, _fm_isdir
        .globl _fm_down, _fm_up, _fm_target, _fm_end, _fm_icon, _fm_icons_done, _fm_icons_reset, _fm_text, _send_vdu
        .area _CODE
        jp _browse
        jp _fm_reset
        jp _fm_first
        jp _fm_next
        jp _fm_raw
        jp _fm_isdir
        jp _fm_down
        jp _fm_up
        jp _fm_target
        jp _fm_end
        jp _fm_icon
        jp _fm_icons_done
        jp _fm_icons_reset
        jp _fm_text
        jp _fm_document
        .globl _fm_selected
        jp _fm_selected
_managed_path:: jp 0xa036
_send_vdu::
        ld b,d
        ld c,e
        .db 0x49
        rst 0x18
        ret
_dir_open::
        ld a,#0x91
        jr mos
_dir_read::
        ld a,#0x93
        jr mos
_dir_close::
        ld a,#0x92
mos:
        .db 0x49
        rst 0x08
        ret
_file_key::
        jp 0x0003               ; AG_WAIT_INPUT in the bootstrap
