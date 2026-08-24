;; pack_comp4.asm - COMPANION floppy, pass 4 of 5 (#363/#365/#367).
;;
;; These network/utility apps use their own fresh RASM address space and append
;; to the image after Paint, Telnet, Xaos and the screensavers.
                org   #4000
wget_img        incbin "../build/WGET.RAW"
wget_imgend
                save  "WGET.APP",wget_img,wget_imgend-wget_img,DSK,"build/companion.dsk"
browser_img     incbin "../build/BROWSER.RAW"
browser_imgend
                save  "BROWSER.APP",browser_img,browser_imgend-browser_img,DSK,"build/companion.dsk"
gbimg_img       incbin "../build/GBIMG.RAW"     ; Browser-only layout/image helper (#476)
gbimg_imgend
                save  "GBIMG.MOD",gbimg_img,gbimg_imgend-gbimg_img,DSK,"build/companion.dsk"
shell_img       incbin "../build/SHELL.RAW"
shell_imgend
                save  "SHELL.APP",shell_img,shell_imgend-shell_img,DSK,"build/companion.dsk"
