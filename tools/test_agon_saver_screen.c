/* Exercise the VDP capture failure that a desktop emulator with ample RAM
 * doesn't reproduce. Model only the saver packets, not the whole VDP. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
extern u8 saver_screen_save(void);
extern void saver_screen_restore(void);
static u8 screen[240][320], original[240][320], bits[8][30][320];
static int width[8], height[8], exclusive, fail_band=-1, visible, timeout;

u8 saver_pixel(u8 y) { return timeout ? 255 : screen[y][319]; }

void sys_send(const void *packet, unsigned len)
{
    const u8 *p=packet;
    int band, w, h, i;
    switch (p[0]) {
    case 25: /* capture between two graphics cursor positions */
        assert(len==19 && p[16]==0x4a && p[15]<8);
        band=p[15];
        w=p[8]+256*p[9]+1-exclusive;
        h=p[10]-p[4]+1-exclusive;
        width[band]=height[band]=0;
        if (band==fail_band) break;
        assert(w<=320 && h<=30 && p[4]+h<=240);
        width[band]=w; height[band]=h;
        for (i=0;i<h;++i) memcpy(bits[band][i],screen[p[4]+i],w);
        break;
    case 18: /* probe or repair a point */
        assert(len==9 && p[4]==69 && p[5]+256*p[6]==319);
        screen[p[7]][319]=p[2];
        break;
    case 23:
        if (p[1]==0) {
            assert(len==6 && p[4]==0x4a && p[3]<8);
            width[p[3]]=height[p[3]]=0;
        } else if (p[2]==0x20) {
            assert(len==12 && p[4]==0x4a && p[3]<8);
            band=p[3];
            for (i=0;i<height[band];++i)
                memcpy(screen[p[10]+i],bits[band][i],width[band]);
        } else {
            assert(len==6 && (p[2]==11 || p[2]==12));
            visible=p[2]==11;
        }
        break;
    default: assert(0);
    }
}

int main(void)
{
    int x,y,i;
    for (y=0;y<240;++y) for (x=0;x<320;++x) original[y][x]=(x+y)%16;
    memcpy(screen,original,sizeof screen);
    for (exclusive=0;exclusive<=1;++exclusive) {
        for (fail_band=0;fail_band<8;++fail_band) {
            assert(!saver_screen_save() && visible);
            assert(!memcmp(screen,original,sizeof screen));
            for (i=0;i<8;++i) assert(!width[i]);
        }
        fail_band=-1;
        assert(saver_screen_save() && !visible);
        memset(screen,9,sizeof screen);
        saver_screen_restore();
        assert(visible && !memcmp(screen,original,sizeof screen));
    }
    exclusive=0; timeout=1;
    assert(!saver_screen_save() && visible);
    assert(!memcmp(screen,original,sizeof screen));
    puts("Agon saver: inclusive/exclusive capture, allocation failures and timeout OK");
    return 0;
}
