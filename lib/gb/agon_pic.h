/* Agon GBPC guests use independent VDP bitmap IDs and MOS read handles.
 * Include after defining the guest-local handle byte. */
static void send(const void *p, unsigned len) __naked
{
    (void)p; (void)len;
__asm
    ld b,d
    ld c,e
    .db 0x49
    rst 0x18
    ret
__endasm;
}
static unsigned char open_read(const char *path) __naked
{
    (void)path;
__asm
    ld c,#1
    ld a,#0x0a
    .db 0x49
    rst 0x08
    ret
__endasm;
}
static unsigned read_file(void *dst, unsigned size) __naked
{
    (void)dst; (void)size;
__asm
    push de
    .db 0x5b,0x11,0,0,0
    pop de
    ld a,(_handle)
    ld c,a
    ld a,#0x1a
    .db 0x49
    rst 0x08
    ret
__endasm;
}
static void close_file(void) __naked
{
__asm
    ld a,(_handle)
    ld c,a
    ld a,#0x0b
    .db 0x49
    rst 0x08
    ret
__endasm;
}
static unsigned word(const unsigned char *p) { return p[0] | ((unsigned)p[1] << 8); }
static unsigned char picture_palette(unsigned char *out,const unsigned char *inks)
{
    static const unsigned char level[]={0,2,3};
    static const unsigned char extras[]={18,2,24,8,20,15,16,11,21,5,13,22};
    unsigned char i,ink;
    for(i=0;i<16;i++) {
        ink=i<4 ? inks[i] : extras[i-4];
        if(ink>26) return 0;
        out[i]=0xc0 | (level[ink%3]<<4) | (level[ink/9]<<2) | level[(ink/3)%3];
    }
    return 1;
}
