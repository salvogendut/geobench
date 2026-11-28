/* A saver covers the whole screen, unlike a managed window. Don't erase the
 * live display until every band has demonstrably been captured and plotted. */
typedef unsigned char u8;
extern void sys_send(const void *, unsigned);
extern u8 saver_pixel(unsigned y); /* rightmost pixel; 255 on timeout */

/* 0x46xx belongs to windows, 0x47xx to artwork, 0x48xx/0x49xx to icons. */
enum { ROWS = 30, BANDS = 240 / ROWS, ID_HI = 0x4a };
enum { MISSING, READY, NO_REPLY };
static u8 bands;
static unsigned right;
static const u8 hide[] = {23,27,12,23,27,15};
static const u8 show[] = {23,27,11,23,27,15};

static void plot(u8 band)
{
    unsigned y=(unsigned)band*ROWS;
    u8 p[] = {23,27,0x20,band,ID_HI,23,27,3,0,0,y,y>>8};
    sys_send(p,sizeof p);
}

static void clear_bands(u8 count)
{
    u8 p[] = {23,0,0xa0,0,ID_HI,2};
    while (count) {
        p[3]=--count;
        sys_send(p,sizeof p);
    }
}

static void point(unsigned y, u8 pen)
{
    u8 p[] = {18,0,pen,25,69,right,right>>8,y,y>>8};
    sys_send(p,sizeof p);
}

static u8 capture(u8 band, u8 extra)
{
    unsigned top=(unsigned)band*ROWS, bottom=top+ROWS-1, x=right+extra, y=bottom+extra;
    u8 original;
    u8 p[] = {25,4,0,0,top,top>>8,25,4,x,x>>8,y,y>>8,
              23,27,0x21,band,ID_HI,0,0};
    sys_send(p,sizeof p);
    original=saver_pixel(bottom);
    if (original>=16) return NO_REPLY;
    /* A missing or one-pixel-short bitmap leaves the contrasting mark behind.
     * Repair the probe explicitly if the VDP could not restore it. */
    point(bottom,original ? 0 : 15);
    plot(band);
    if (saver_pixel(bottom)==original) return READY;
    point(bottom,original);
    return MISSING;
}

u8 saver_screen_save(u8 wide)
{
    u8 band,extra=0;
    bands=wide ? 2*BANDS : BANDS;
    right=wide ? 639 : 319;
    sys_send(hide,sizeof hide);
    for (band=0; band<bands; ++band) {
        u8 result=capture(band,extra);
        if (result==READY) continue;
        /* VDP through 2.5 uses exclusive capture endpoints. Try that
         * convention once, checking the actual bottom-right pixel again. */
        if (!band && result==MISSING && capture(band,1)==READY) { extra=1; continue; }
        clear_bands(band+1);
        sys_send(show,sizeof show);
        return 0;
    }
    return 1;
}

void saver_screen_restore(void)
{
    u8 band;
    for (band=0; band<bands; ++band) plot(band);
    /* Wait for the last plot before releasing any bitmap that the VDP may
     * still be using. On timeout keep the fixed IDs until the next capture. */
    if (saver_pixel((unsigned)bands*ROWS-1)!=255) clear_bands(bands);
    sys_send(show,sizeof show);
}
