;; pack_modules.asm - extra .dsk packaging pass for paged kernel modules.
;;
;; FLOPPYSV.MOD grew beyond the slack in gbkern.asm's high packaging region.
;; It is not resident in that image; the incbin existed only so RASM could save
;; it into build/gbkern.dsk. Keep that catalogue write in its own pass.
                ifndef TITLEBAR_TILE
TITLEBAR_TILE   equ   0
                endif
                org   #4000
flsv_img        incbin "../build/FLOPPYSV.RAW"
flsv_imgend
splashd_img     incbin "../build/SPLASHD.BIN"     ; DEBUG=TRUE bootsplash with build id
splashd_imgend
gbweb_img       incbin "../build/GBWEB.RAW"
gbweb_imgend
gbapick_img     incbin "../build/GBAPICK.RAW"
gbapick_imgend
                if TITLEBAR_TILE
gbtitle_img     incbin "../build/GBTITLE.RAW"
gbtitle_imgend
                endif
                save  "FLOPPYSV.MOD",flsv_img,flsv_imgend-flsv_img,DSK,"build/gbkern.dsk"
                save  "SPLASHD.MOD",splashd_img,splashd_imgend-splashd_img,DSK,"build/gbkern.dsk"
                save  "GBWEB.MOD",gbweb_img,gbweb_imgend-gbweb_img,DSK,"build/gbkern.dsk"
                save  "GBAPICK.MOD",gbapick_img,gbapick_imgend-gbapick_img,DSK,"build/gbkern.dsk"
                if TITLEBAR_TILE
                save  "GBTITLE.MOD",gbtitle_img,gbtitle_imgend-gbtitle_img,DSK,"build/gbkern.dsk"
                endif
