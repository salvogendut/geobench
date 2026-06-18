; cursor sprite bitmaps - 2 pre-shifted phases, mask+data INTERLEAVED per byte
; (mask,data,mask,data...) so the draw composite reads them with one advancing
; pointer (and (hl) / inc hl / or (hl)) - keeps the inner loop register-only so it
; stays ahead of the CRTC beam (#cursor-tear). Relocated out of the kernel (#65):
; assembled above kern_end, saved as DEFAULT.SPR, loaded into low RAM at CUR_LOW at
; boot. Layout at CUR_LOW: phase0 (shift 0) @ +0, phase2 (shift 2) @ +#80; each is
; cursor_arrow_w*cursor_arrow_h*2 = 128 bytes. See lib/cursor_arrow.asm.
cur_spr_data
                db    #BB,#40,#FF,#00,#FF,#00,#FF,#00      ; phase0 (shift 0) mask,data
                db    #99,#60,#FF,#00,#FF,#00,#FF,#00
                db    #88,#72,#FF,#00,#FF,#00,#FF,#00
                db    #88,#73,#77,#80,#FF,#00,#FF,#00
                db    #88,#73,#33,#C8,#FF,#00,#FF,#00
                db    #88,#73,#00,#FC,#FF,#00,#FF,#00
                db    #88,#73,#00,#FF,#77,#80,#FF,#00
                db    #88,#73,#00,#FF,#33,#C8,#FF,#00
                db    #88,#73,#00,#FF,#11,#EC,#FF,#00
                db    #88,#73,#00,#FE,#11,#E0,#FF,#00
                db    #88,#73,#00,#F2,#77,#80,#FF,#00
                db    #88,#72,#00,#F3,#77,#80,#FF,#00
                db    #99,#60,#88,#71,#33,#C8,#FF,#00
                db    #FF,#00,#CC,#31,#33,#C8,#FF,#00
                db    #FF,#00,#CC,#31,#33,#C8,#FF,#00
                db    #FF,#00,#EE,#10,#77,#80,#FF,#00
                db    #EE,#10,#FF,#00,#FF,#00,#FF,#00      ; phase2 (shift 2) mask,data
                db    #EE,#10,#77,#80,#FF,#00,#FF,#00
                db    #EE,#10,#33,#C8,#FF,#00,#FF,#00
                db    #EE,#10,#11,#EC,#FF,#00,#FF,#00
                db    #EE,#10,#00,#FE,#FF,#00,#FF,#00
                db    #EE,#10,#00,#FF,#33,#C0,#FF,#00
                db    #EE,#10,#00,#FF,#11,#EC,#FF,#00
                db    #EE,#10,#00,#FF,#00,#FE,#FF,#00
                db    #EE,#10,#00,#FF,#00,#FF,#77,#80
                db    #EE,#10,#00,#FF,#00,#F8,#77,#80
                db    #EE,#10,#00,#FC,#11,#E8,#FF,#00
                db    #EE,#10,#00,#F8,#11,#EC,#FF,#00
                db    #EE,#10,#66,#90,#00,#F6,#FF,#00
                db    #FF,#00,#FF,#00,#00,#F6,#FF,#00
                db    #FF,#00,#FF,#00,#00,#F6,#FF,#00
                db    #FF,#00,#FF,#00,#99,#60,#FF,#00
cur_spr_end
