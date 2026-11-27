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
