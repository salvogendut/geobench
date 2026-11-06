; Build the CPC cursor assets before gbkern.asm packages them on disk.
; Both cursor sources append PICEDITL.RAW, so this pass must run after
; tools/build_kernel.sh has assembled the low-RAM picture editor helper.

                org   #0000
                include "../lib/cursor_data.asm"
                include "../lib/cursor_hand_data.asm"

                save  "build/DEFAULT.SPR",cur_spr_data,cur_spr_end-cur_spr_data
                save  "build/HAND.SPR",cur_hand_data,cur_hand_end-cur_hand_data
