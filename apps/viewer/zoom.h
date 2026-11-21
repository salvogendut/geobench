enum { HALF, NORMAL, DOUBLE, QUAD };

/* Resample horizontally in packed bytes. Mode 1 stores four pixels in
   two bit planes; mode 7 stores two palette indices high nibble first. */
static unsigned char half_pair(unsigned char a, unsigned char b, unsigned char mode)
{
    if (mode == 7) return (a & 0xf0) | (b >> 4);
    a = (a & 0x88) | ((a & 0x22) << 1);
    b = (b & 0x88) | ((b & 0x22) << 1);
    return a | (b >> 2);
}

static void sample_row(unsigned char *dst, const unsigned char *src,
                       unsigned int stride, unsigned int pos, unsigned int count,
                       unsigned char level, unsigned char mode)
{
    unsigned char b, t, shift;
    unsigned int i;
    if (level == HALF) {
        i = pos * 2;
        while (count--) {
            b = i < stride ? src[i] : 0;
            ++i;
            t = i < stride ? src[i] : 0;
            ++i;
            *dst++ = half_pair(b, t, mode);
        }
        return;
    }
    shift = level - NORMAL;
    while (count--) {
        i = pos >> shift;
        b = i < stride ? src[i] : 0;
        if (shift && mode == 7) {
            t = (pos & (1u << (shift - 1))) ? (b & 15) : (b >> 4);
            b = (t << 4) | t;
        } else if (level == DOUBLE) {
            if (pos & 1) b <<= 2;
            b = (b & 0x88) | ((b & 0x44) >> 1);
            b |= b >> 1;
        } else if (level == QUAD) {
            b = (b << (pos & 3)) & 0x88;
            b |= b >> 1;
            b |= b >> 2;
        }
        *dst++ = b;
        ++pos;
    }
}
