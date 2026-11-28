/* The card catalogue describes the target ABI, not a trust boundary. Keep
 * raw GBAP binaries unmodified and never enter a partial or oversized read. */
#include <string.h>
typedef unsigned char u8;
extern u8 ag_read_open(const char *), ag_close(u8);
extern unsigned ag_read(void *, unsigned);
u8 ag_handle;
static u8 catalog[6+32*5+1];
static unsigned word(const u8 *p) { return p[0] | (unsigned)p[1]<<8; }

u8 ag_load(const char *path)
{
    unsigned n, bytes, crc=0xffff, i;
    u8 *image=(u8 *)0x4000, *p, bit;
    ag_handle=ag_read_open("/GBENCH/AGON.CAT");
    if (!ag_handle) return 255;
    n=ag_read(catalog,sizeof catalog);
    ag_close(ag_handle);
    if (n<6 || memcmp(catalog,"AGAC\1",5) || !catalog[5] ||
        catalog[5]>32 || n!=6+5*catalog[5]) return 255;
    ag_handle=ag_read_open(path);
    if (!ag_handle) return 255;
    memset(image,0,0x4000); /* raw images contain no initial contents for BSS */
    bytes=ag_read(image,0x3f01);
    ag_close(ag_handle);
    if (!bytes || bytes>0x3f00) return 255;
    for (i=0;i<bytes;i++) {
        crc^=(unsigned)image[i]<<8;
        for (bit=8;bit;--bit) crc=crc&0x8000 ? (crc<<1)^0x1021 : crc<<1;
    }
    for (p=catalog+6; p<catalog+n; p+=5)
        if (p[0]<=6 && word(p+1)==bytes && word(p+3)==crc) return *p;
    return 255;
}
