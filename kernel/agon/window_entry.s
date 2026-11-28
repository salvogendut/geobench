        .module window_entry
        .globl _win_init, _win_open, _win_click, _win_close
        .area _CODE
        jp _win_init
        jp _win_open
        jp _win_click
        jp _win_close
        .globl _win_fill16, _win_backdrop
        jp _win_fill16
        jp _win_backdrop
_win_send::
        ld b,d
        ld c,e
        .db 0x49
        rst 0x18
        ret
_win_poll::
        jp 0x0009               ; AG_POLL_HELD
_win_sync::
        xor a
        jp 0xa009               ; resident pixel query at (319,0)
