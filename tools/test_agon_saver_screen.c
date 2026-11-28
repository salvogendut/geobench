/* Exercise the VDP capture failure that a desktop emulator with ample RAM
 * doesn't reproduce. Model only the saver packets, not the whole VDP. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
extern u8 saver_screen_save(u8);
extern void saver_screen_restore(void);
static u8 screen[480][640], original[480][640], bits[16][30][640];
static int width[16], height[16], exclusive, fail_band=-1, visible, timeout, cols, rows;

u8 saver_pixel(unsigned y) { assert(y<(unsigned)rows); return timeout ? 255 : screen[y][cols-1]; }
static int word(const u8 *p) { return p[0]+256*p[1]; }

void sys_send(const void *packet, unsigned len)
{
    const u8 *p=packet;
    int band, w, h, i;
    switch (p[0]) {
    case 25: /* capture between two graphics cursor positions */
        assert(len==19 && p[16]==0x4a && p[15]<rows/30);
        band=p[15];
        w=p[8]+256*p[9]+1-exclusive;
        h=word(p+10)-word(p+4)+1-exclusive;
        width[band]=height[band]=0;
        if (band==fail_band) break;
        assert(w<=cols && h<=30 && word(p+4)+h<=rows);
        width[band]=w; height[band]=h;
        for (i=0;i<h;++i) memcpy(bits[band][i],screen[word(p+4)+i],w);
        break;
    case 18: /* probe or repair a point */
        assert(len==9 && p[4]==69 && word(p+5)==cols-1);
        screen[word(p+7)][cols-1]=p[2];
        break;
    case 23:
        if (p[1]==0) {
            assert(len==6 && p[4]==0x4a && p[3]<rows/30);
            width[p[3]]=height[p[3]]=0;
        } else if (p[2]==0x20) {
            assert(len==12 && p[4]==0x4a && p[3]<rows/30);
            band=p[3];
            for (i=0;i<height[band];++i)
                memcpy(screen[word(p+10)+i],bits[band][i],width[band]);
        } else {
            assert(len==6 && (p[2]==11 || p[2]==12));
            visible=p[2]==11;
        }
        break;
    default: assert(0);
    }
}

static void exercise(u8 wide)
{
    int x,y,i;
    cols=wide ? 640 : 320; rows=wide ? 480 : 240; timeout=0;
    for (y=0;y<rows;++y) for (x=0;x<cols;++x) original[y][x]=(x+y)%16;
    memcpy(screen,original,sizeof screen);
    for (exclusive=0;exclusive<=1;++exclusive) {
        for (fail_band=0;fail_band<rows/30;++fail_band) {
            assert(!saver_screen_save(wide) && visible);
            assert(!memcmp(screen,original,sizeof screen));
            for (i=0;i<rows/30;++i) assert(!width[i]);
        }
        fail_band=-1;
        assert(saver_screen_save(wide) && !visible);
        for(y=0;y<rows;++y) memset(screen[y],9,cols);
        saver_screen_restore();
        assert(visible && !memcmp(screen,original,sizeof screen));
    }
    exclusive=0; timeout=1;
    assert(!saver_screen_save(wide) && visible);
    assert(!memcmp(screen,original,sizeof screen));
}
int main(void)
{
    exercise(0); exercise(1);
    puts("Agon saver: 320x240 and 640x480 capture, allocation failures and timeout OK");
    return 0;
}
