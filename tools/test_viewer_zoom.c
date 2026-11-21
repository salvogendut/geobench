/* cc -std=c99 tools/test_viewer_zoom.c -o /tmp/test-viewer-zoom && /tmp/test-viewer-zoom */
#include <assert.h>
#include <stdio.h>
#include "../apps/viewer/zoom.h"

static unsigned char pixel(const unsigned char *row, unsigned int x, int mode)
{
    if (mode == 7) return (row[x/2] >> ((1-x%2)*4)) & 15;
    unsigned char b = row[x/4] >> (3-x%4);
    return ((b >> 4) & 1) | ((b & 1) << 1);
}

int main(void)
{
    unsigned char src[257], dst[9];
    for (unsigned i = 0; i < sizeof(src); ++i) src[i] = (unsigned char)(i * 173 + 53);
    for (int mode = 1; mode <= 7; mode += 6) {
        unsigned ppb = mode == 7 ? 2 : 4;
        for (unsigned z = HALF; z <= QUAD; ++z) {
            unsigned end = z == HALF ? (sizeof(src)+1)/2 : sizeof(src) << (z-NORMAL);
            for (unsigned pos = 0; pos < end; ++pos) {
                unsigned n = end-pos < sizeof(dst) ? end-pos : sizeof(dst);
                sample_row(dst, src, sizeof(src), pos, n, z, mode);
                for (unsigned j = 0; j < n*ppb; ++j) {
                    unsigned x = pos*ppb+j;
                    unsigned sx = z == HALF ? x*2 : x >> (z-NORMAL);
                    assert(pixel(dst, j, mode) ==
                           (sx < sizeof(src)*ppb ? pixel(src, sx, mode) : 0));
                }
            }
        }
    }
    puts("Viewer zoom: both pixel formats and all four scales passed.");
}
