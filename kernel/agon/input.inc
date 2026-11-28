; Private Agon bootstrap entry, callable from the resident runtime and browser.
AG_WAIT_INPUT equ 3                  ; returns ASCII; Space is a primary click
AG_LOAD_CALC  equ 6                  ; HL = selected absolute path
AG_POLL_HELD  equ 9                  ; inner drag poll, never consumes keys
AG_LOAD_PREVIEW equ 12               ; load the associated preview guest
AG_FILES      equ #8800              ; resident MOS adapter jump table
AG_FILEMODE   equ #17ef              ; 0 Calculator, 1 File Manager, 2 picture preview
AG_ICON_ARGS  equ #17f0              ; slot, x (byte columns), y, half
AG_CLICK_TIME equ #17f4              ; MOS centiseconds at the primary press
AG_TEXT_ARGS  equ #17f8              ; y, x (columns), paper, ink, right limit (columns)
AG_DOC_PATH   equ #17fd              ; word: selected absolute MOS path for preview
AG_WM         equ #900               ; resident window placement / save-under services
AG_LINES      equ 240
AG_SYSTEM     equ #a000             ; System menu, clock and saver services
AG_TEXT_FOCUS equ AG_SYSTEM+51
AG_PROBE      equ #3b20             ; resident worker and input FIFO, before #3e00 clipboard
AG_NOW        equ #17e8             ; low word of the MOS centisecond counter
AG_MOTION     equ #17ea             ; pointer activity serial, including fine nudges
AG_STATUS_ROW  equ AG_LINES/8-1
