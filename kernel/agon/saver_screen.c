/* A saver covers the whole screen, unlike a managed window. Don't erase the
 * live display until every band has demonstrably been captured and plotted. */
typedef unsigned char u8;
extern void sys_send(const void *, unsigned);
extern u8 saver_pixel(u8 y);       /* read colour index at (319,y); 255 on timeout */

/* 0x46xx belongs to windows, 0x47xx to artwork, 0x48xx/0x49xx to icons. */
enum { ROWS = 30, BANDS = 240 / ROWS, ID_HI = 0x4a };
enum { MISSING, READY, NO_REPLY };
static const u8 hide[] = {23,27,12,23,27,15};
static const u8 show[] = {23,27,11,23,27,15};

static void plot(u8 band)
{
    u8 p[] = {23,27,0x20,band,ID_HI,23,27,3,0,0,band*ROWS,0};
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

static void point(u8 y, u8 pen)
{
    u8 p[] = {18,0,pen,25,69,63,1,y,0};   /* absolute point at x=319 */
    sys_send(p,sizeof p);
}

static u8 capture(u8 band, u8 extra)
{
    u8 top=band*ROWS, bottom=top+ROWS-1, original;
    u8 p[] = {25,4,0,0,top,0,25,4,63+extra,1,bottom+extra,0,
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

u8 saver_screen_save(void)
{
    u8 band,extra=0;
    sys_send(hide,sizeof hide);
    for (band=0; band<BANDS; ++band) {
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
    for (band=0; band<BANDS; ++band) plot(band);
    /* Wait for the last plot before releasing any bitmap that the VDP may
     * still be using. On timeout keep the fixed IDs until the next capture. */
    if (saver_pixel(239)!=255) clear_bands(BANDS);
    sys_send(show,sizeof show);
}
