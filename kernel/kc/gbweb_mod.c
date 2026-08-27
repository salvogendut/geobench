/* GBWEB.MOD - Browser source-cache and configuration helper.
 *
 * Browser fills the low-RAM staging block and calls the shared GB_UI entry with
 * op 6/7/9. The kernel dispatches those non-visual operations here, keeping the
 * already-full Browser application bank small. */
#include "gb.h"
#include "gbbrowser.h"

#define UI_OP          (*(volatile unsigned char *)0x1700)
#define UI_N           (*(volatile unsigned char *)0x1703)
#define UI_RES         (*(volatile unsigned char *)0x1704)
#define UI_NAME        ((char *)0x1708)
#define BUI_STAGE      ((char *)0x2B00)
#define BUI_PAGES      ((volatile unsigned char *)0x3900)
#define BUI_NPAGES     (*(volatile unsigned char *)0x3904)
#define BUI_TAIL       (*(volatile unsigned int  *)0x3905)
#define BUI_STAGE_LEN  (*(volatile unsigned int  *)0x3907)
#define BUI_FLAGS      (*(volatile unsigned char *)0x3909)
#define BUI_LOCAL_LEN  (*(volatile unsigned int  *)0x390A)
#define BUI_LOCAL_POS  (*(volatile unsigned int  *)0x390C)
#define BUI_LOCAL_OFS  ((volatile unsigned char *)0x390E)
#define BUI_CTRL       (*(volatile unsigned char *)0x3911)
#define BUI_PROXY      ((char *)0x3920)
#define BUI_PROXY_HOST ((char *)0x3980)
#define BUI_PROXY_PORT (*(volatile unsigned int *)0x39C0)
#define BUI_FORM_ACTION ((char *)0x39D0)
#define BUI_FORM_NAME   ((char *)0x3A00)
#define BUI_FORM_VALUE  ((char *)0x3A18)
#define BUI_FORM_URL    ((char *)0x3A48)
#define BUI_FORM_ACTIVE (*(volatile unsigned char *)0x3AA8)
#define BUI_REQ_PORT    (*(volatile unsigned int  *)0x3AAA)
#define BUI_REQ_ERROR   (*(volatile unsigned char *)0x3AAC)
#define BUI_CACHE_PAGE  (*(volatile unsigned char *)0x3ABF)
#define BUI_REQ_TEXT    ((char *)0x3AC8)
#define BUI_IMAGE_PAGE (*(volatile unsigned char *)0x3ADC)
#define BUI_SCREEN_MODE (*(volatile unsigned char *)0x3ADF)
#define BUI_PROXY_MAX  95
#define BUI_SOURCE_FULL 0x01
#define BUI_PROXY_ON   0x08
#define BUI_LOCAL_EOF  0x20
#define BUI_DOX_MODE   0x40
#define BUI_LOCAL_BUF  ((char *)0x2900)
#define BUI_URL_BASE   ((char *)0x2900)
#define BUI_URL_LINK   ((char *)0x2960)
#define BUI_URL_RESULT ((char *)0x29C0)
#define BROWSER_REQUEST ((char *)0x3300)
#define BROWSER_LINK    ((char *)0x34F0)
#define BROWSER_URL     ((char *)0x3542)
#define BROWSER_HOST    ((char *)0x35A2)
#define BROWSER_PATH    ((char *)0x35E2)
#define BROWSER_URL_MAX 95
#define BROWSER_HOST_MAX 63
#define BROWSER_PATH_MAX 95
#define BROWSER_REQUEST_MAX 255
#define FS_LOAD_OFS    ((volatile unsigned char *)0x144C)
#define FS_XFLAGS      (*(volatile unsigned char *)0x144F)

#define GB_FORM_EXTERNAL_STORAGE 1
#define GB_FORM_ACTION BUI_FORM_ACTION
#define GB_FORM_NAME   BUI_FORM_NAME
#define GB_FORM_VALUE  BUI_FORM_VALUE
#define GB_FORM_URL    BUI_FORM_URL
#define GB_FORM_ACTIVE BUI_FORM_ACTIVE
#include "gbform.h"

#define GB_URL_EXTERNAL_STORAGE 1
#define GB_URL_BASE   BUI_URL_BASE
#define GB_URL_LINK   BUI_URL_LINK
#define GB_URL_RESULT BUI_URL_RESULT
#include "gburl.h"

#define APP_NPAGES     (*(volatile unsigned char *)0x1437)
#define APP_PAGES      ((volatile unsigned char *)0x1438)
#define APP_BUSY       ((volatile unsigned char *)0x1440)
#define PIC_PAGE_K     (*(volatile unsigned char *)0x130B)
#define PIC_PAGE2_K    (*(volatile unsigned char *)0x1348)
#define PIC_PAGE3_K    (*(volatile unsigned char *)0x1291)
#define PIC_PAGE4_K    (*(volatile unsigned char *)0x1292)
#define FS_SAVE_LEN_K  (*(volatile unsigned int  *)0x14FD)

static unsigned char alloc_page(void)
{
    unsigned char i;
    for (i = 0; i < APP_NPAGES; i++) if (!APP_BUSY[i]) {
        APP_BUSY[i] = 1;
        return APP_PAGES[i];
    }
    return 0;
}

static void reset_image_cache(void)
{
    unsigned char i;
    BUI_IMAGE_CACHE_TAIL = BUI_IMAGE_DATA_OFF = BUI_IMAGE_EXPECTED = 0;
    BUI_IMAGE_CACHE_NEXT = 0;
    for (i = 0; i < BROWSER_IMAGE_CACHE_MAX; i++) {
        BUI_IMAGE_CACHE_META[(unsigned char)(i * BROWSER_IMAGE_CACHE_ENTRY_SIZE)] =
            (unsigned char)BROWSER_INVALID_OFFSET;
        BUI_IMAGE_CACHE_META[(unsigned char)(i * BROWSER_IMAGE_CACHE_ENTRY_SIZE + 1)] =
            (unsigned char)(BROWSER_INVALID_OFFSET >> 8);
    }
}

static void source_put(void)
{
    unsigned char page;
    char *src = BUI_STAGE;
    unsigned int left = BUI_STAGE_LEN, take;
    if (!BUI_STAGE_LEN || (BUI_FLAGS & BUI_SOURCE_FULL)) return;
    while (left) {
        if (!BUI_NPAGES || BUI_TAIL == 0x4000) {
            /* Leave one app page for BRSAVE.APP, which writes these borrowed
             * pages without paging this helper out underneath itself. Each
             * optional image-cache page reduces source capture accordingly. */
            if (BUI_NPAGES >= (BUI_IMAGE_PAGE2 ? 1 : (BUI_IMAGE_PAGE ? 2 : 3)) ||
                !(page = alloc_page())) {
                BUI_FLAGS |= BUI_SOURCE_FULL;
                break;
            }
            BUI_PAGES[BUI_NPAGES++] = page;
            BUI_TAIL = 0;
        }
        take = (unsigned int)(0x4000 - BUI_TAIL);
        if (take > left) take = left;
        PIC_PAGE_K = BUI_PAGES[BUI_NPAGES - 1];
        PIC_PAGE2_K = 0;
        gb_pic_edit_buf = (unsigned int)src;
        gb_pic_edit_off = BUI_TAIL;
        FS_SAVE_LEN_K = take;
        if (!gb_pic_edit(GB_PICEDIT_WRITE)) { BUI_FLAGS |= BUI_SOURCE_FULL; break; }
        BUI_TAIL += take;
        src += take;
        left -= take;
    }
    BUI_STAGE_LEN = 0;
}

static void source_free(void)
{
    /* A nonzero image page is possible only after Browser observed Screen 7.
     * Zero the extended picture context before each close below. */
    if (BUI_SCREEN_MODE == 7) PIC_PAGE3_K = PIC_PAGE4_K = 0;
    while (BUI_NPAGES) {
        PIC_PAGE_K = BUI_PAGES[--BUI_NPAGES];
        PIC_PAGE2_K = 0;
        gb_pic_close();
    }
    BUI_TAIL = BUI_STAGE_LEN = 0;
    BUI_FLAGS = 0;
    BUI_FORM_ACTIVE = 0;
    reset_image_cache();
}

static unsigned char key_at(const char *p)
{
    return (unsigned char)(p[0] == 'P' && p[1] == 'R' && p[2] == 'O' &&
                           p[3] == 'X' && p[4] == 'Y' && p[5] == '=');
}

static void cfg_proxy(void)
{
    char *cfg = (char *)0x1000, *p = cfg, *value = BUI_PROXY;
    unsigned int len = *(volatile unsigned int *)0x1200, pos, end, old, add, i;
    unsigned char vl = 0;
    while (value[vl] && vl < BUI_PROXY_MAX) vl++;
    pos = 0xFFFF;
    while ((unsigned int)(p - cfg) + 6 <= len) {
        if ((p == cfg || p[-1] == '\r' || p[-1] == '\n') && key_at(p)) {
            pos = (unsigned int)(p - cfg + 6); break;
        }
        p++;
    }
    if (pos == 0xFFFF) {
        if (len + 8 + vl > 512) return;
        cfg[len++] = 'P'; cfg[len++] = 'R'; cfg[len++] = 'O';
        cfg[len++] = 'X'; cfg[len++] = 'Y'; cfg[len++] = '=';
        for (i = 0; i < vl; i++) cfg[len++] = value[i];
        cfg[len++] = '\r'; cfg[len++] = '\n';
    } else {
        end = pos;
        while (end < len && cfg[end] != '\r' && cfg[end] != '\n') end++;
        old = end - pos;
        if (vl > old) {
            add = vl - old;
            if (len + add > 512) return;
            for (i = len; i > end; i--) cfg[i - 1 + add] = cfg[i - 1];
            len += add;
        } else if (vl < old) {
            add = old - vl;
            for (i = end; i < len; i++) cfg[i - add] = cfg[i];
            len -= add;
        }
        for (i = 0; i < vl; i++) cfg[pos + i] = value[i];
    }
    *(volatile unsigned int *)0x1200 = len;
}

static unsigned char save_cfg(void)
{
    unsigned char i;
    gb_set_drive(gb_boot_drive);
    for (i = 0; i < 4; i++) gb_back();
    gb_set_name("GEOBENCHCFG");
    return gb_fs_save((char *)0x1000, *(volatile unsigned int *)0x1200);
}

static void strip_http_prefix(void);

static void load_proxy(void)
{
    const char *cfg = (const char *)0x1000;
    unsigned int len = *(volatile unsigned int *)0x1200, i = 0;
    unsigned char n = 0;
    BUI_PROXY[0] = 0;
    while (i + 6 <= len) {
        if ((!i || cfg[i - 1] == '\r' || cfg[i - 1] == '\n') && key_at(cfg + i)) {
            i += 6;
            while (i < len && cfg[i] != '\r' && cfg[i] != '\n' && n < BUI_PROXY_MAX)
                BUI_PROXY[n++] = cfg[i++];
            break;
        }
        i++;
    }
    BUI_PROXY[n] = 0;
    strip_http_prefix();
}

static unsigned char lower(unsigned char c)
{
    return (unsigned char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
}

static unsigned char prefix(const char *s, const char *want)
{
    while (*want) if (lower((unsigned char)*s++) != (unsigned char)*want++) return 0;
    return 1;
}

static void strip_http_prefix(void)
{
    unsigned char i = 0;
    if (!prefix(BUI_PROXY, "http://")) return;
    do { BUI_PROXY[i] = BUI_PROXY[i + 7]; } while (BUI_PROXY[i++]);
}

static unsigned char parse_proxy(void)
{
    const char *p = BUI_PROXY;
    char *dst = BUI_PROXY_HOST;
    unsigned char n = 0, digit;
    BUI_PROXY_PORT = 80;
    BUI_CTRL &= (unsigned char)~BUI_PROXY_ON;
    if (!*p) return 1;
    if (prefix(p, "https://")) return 0;
    strip_http_prefix();
    p = BUI_PROXY;
    while (*p && *p != ':' && *p != '/' && n < 63) { *dst++ = *p++; n++; }
    *dst = 0;
    if (!n || (n == 63 && *p && *p != ':' && *p != '/')) return 0;
    if (*p == ':') {
        p++; BUI_PROXY_PORT = 0;
        if (*p < '0' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') {
            digit = (unsigned char)(*p++ - '0');
            if (BUI_PROXY_PORT > 6553 ||
                (BUI_PROXY_PORT == 6553 && digit > 5)) return 0;
            BUI_PROXY_PORT = (unsigned int)(BUI_PROXY_PORT * 10 + digit);
        }
        if (!*(volatile unsigned char *)0x39C0 &&
            !*(volatile unsigned char *)0x39C1) return 0;
    }
    if (*p == '/') p++;
    if (*p) return 0;
    BUI_CTRL |= BUI_PROXY_ON;
    return 1;
}

static char *put_dec(char *p, unsigned int v)
{
    unsigned int d = 10000;
    unsigned char started = 0, n;
    while (d) {
        n = 0;
        while (v >= d) { v -= d; n++; }
        if (n || started || d == 1) { *p++ = (char)('0' + n); started = 1; }
        d /= 10;
    }
    return p;
}

static unsigned char prepare_request(void)
{
    const char *target = UI_N ? BROWSER_LINK : BROWSER_URL;
    const char *p = target, *s;
    char *dst, *out = BROWSER_REQUEST;
    char *limit = BROWSER_REQUEST + BROWSER_REQUEST_MAX;
    unsigned char n = 0, digit;

    BUI_REQ_ERROR = 0;
    if (prefix(p, "https://")) { BUI_REQ_ERROR = 1; return 0; }
    if (!prefix(p, "http://")) { BUI_REQ_ERROR = 2; return 0; }
    p += 7;
    dst = BROWSER_HOST;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#' &&
           n < BROWSER_HOST_MAX) { *dst++ = *p++; n++; }
    *dst = 0;
    if (!n || (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#')) {
        BUI_REQ_ERROR = 3; return 0;
    }

    BUI_REQ_PORT = 80;
    if (*p == ':') {
        p++; BUI_REQ_PORT = 0;
        if (*p < '0' || *p > '9') { BUI_REQ_ERROR = 4; return 0; }
        while (*p >= '0' && *p <= '9') {
            digit = (unsigned char)(*p++ - '0');
            if (BUI_REQ_PORT > 6553 ||
                (BUI_REQ_PORT == 6553 && digit > 5)) {
                BUI_REQ_ERROR = 4; return 0;
            }
            BUI_REQ_PORT = (unsigned int)(BUI_REQ_PORT * 10 + digit);
        }
        if (!*(volatile unsigned char *)0x3AAA &&
            !*(volatile unsigned char *)0x3AAB) {
            BUI_REQ_ERROR = 4; return 0;
        }
    }
    if (*p && *p != '/' && *p != '?' && *p != '#') {
        BUI_REQ_ERROR = 5; return 0;
    }

    dst = BROWSER_PATH; n = 0;
    if (!*p || *p == '#') { *dst++ = '/'; n = 1; }
    else {
        if (*p == '?') { *dst++ = '/'; n = 1; }
        while (*p && *p != '#' && n < BROWSER_PATH_MAX) {
            *dst++ = *p++; n++;
        }
    }
    *dst = 0;
    if (*p && *p != '#') { BUI_REQ_ERROR = 6; return 0; }

#define ADD_TEXT(t) do { s = (t); while (*s && out < limit) *out++ = *s++; } while (0)
    ADD_TEXT("GET "); ADD_TEXT(BUI_PROXY[0] ? target : BROWSER_PATH);
    ADD_TEXT(" HTTP/1.0\r\nHost: "); ADD_TEXT(BROWSER_HOST);
    if (BUI_REQ_PORT != 80 && out < limit) {
        *out++ = ':'; out = put_dec(out, BUI_REQ_PORT);
    }
    if (BUI_PROXY[0]) {
        /* The explicit profile is sufficient for GB-proxy and leaves room for
         * a 95-byte absolute URL in Browser's fixed 256-byte request buffer. */
        ADD_TEXT("\r\nX-GB-DOX: geobench-1\r\nX-GBPC: ");
        ADD_TEXT(BUI_IMAGE_PAGE ? "7,1\r\n" : "1\r\n");
        BUI_CTRL |= BUI_DOX_MODE;
    } else {
        ADD_TEXT("\r\nUser-Agent: GB/1\r\n");
        BUI_CTRL &= (unsigned char)~BUI_DOX_MODE;
    }
    ADD_TEXT("Connection: close\r\n\r\n");
    if (out >= limit) {
        BROWSER_REQUEST[BROWSER_REQUEST_MAX] = 0;
        BUI_REQ_ERROR = 7;
        return 0;
    }
    *out = 0;
#undef ADD_TEXT
    if (!parse_proxy()) { BUI_REQ_ERROR = 8; return 0; }
    return 1;
}

static void request_error_text(void)
{
    const char *s;
    unsigned char i = 0;
    switch (BUI_REQ_ERROR) {
        case 1: s = "HTTPS unsupported"; break;
        case 2: s = "Use http://"; break;
        case 3: s = "Host too long"; break;
        case 4: s = "Invalid port number"; break;
        case 5: s = "Invalid URL"; break;
        case 6: s = "Path too long"; break;
        case 7: s = "Request too long"; break;
        default: s = "Invalid proxy"; break;
    }
    while (s[i] && i < 19) { BUI_REQ_TEXT[i] = s[i]; i++; }
    BUI_REQ_TEXT[i] = 0;
}

static void local_read(void)
{
    unsigned int n, off, i;
    if (BUI_CTRL & BUI_LOCAL_EOF) {
        BUI_LOCAL_LEN = BUI_LOCAL_POS = 0;
        UI_RES = 0;
        return;
    }
    FS_LOAD_OFS[0] = BUI_LOCAL_OFS[0];
    FS_LOAD_OFS[1] = BUI_LOCAL_OFS[1];
    FS_LOAD_OFS[2] = BUI_LOCAL_OFS[2];
    FS_XFLAGS = 0x01;
    n = gb_fs_load(BUI_LOCAL_BUF, 512);
    FS_XFLAGS = 0;
    for (i = 0; i < n; i++) if ((unsigned char)BUI_LOCAL_BUF[i] == 0x1A) {
        n = i;
        BUI_CTRL |= BUI_LOCAL_EOF;
        break;
    }
    BUI_LOCAL_LEN = n; BUI_LOCAL_POS = 0;
    if (!n) { UI_RES = 0; return; }
    off = (unsigned int)BUI_LOCAL_OFS[0] | ((unsigned int)BUI_LOCAL_OFS[1] << 8);
    off += n;
    if (off < n) BUI_LOCAL_OFS[2]++;
    BUI_LOCAL_OFS[0] = (unsigned char)off;
    BUI_LOCAL_OFS[1] = (unsigned char)(off >> 8);
}

static unsigned char launch_file(void)
{
    gb_get_name(UI_NAME);
    return (unsigned char)(UI_NAME[8] == 'H' && UI_NAME[9] == 'T' &&
                           UI_NAME[10] == 'M');
}

void main(void)
{
    UI_RES = 1;
    if (UI_OP == 6) source_put();
    else if (UI_OP == 7) source_free();
    else if (UI_OP == 9) {
        if (!parse_proxy()) UI_RES = 0;
        else { cfg_proxy(); UI_RES = save_cfg() ? 1 : 2; }
    }
    else if (UI_OP == 10) local_read();
    else if (UI_OP == 11) UI_RES = parse_proxy();
    else if (UI_OP == 12) load_proxy();
    else if (UI_OP == 13) UI_RES = launch_file();
    else if (UI_OP == 14)
        UI_RES = gb_form_process(UI_N, *(const char **)UI_NAME);
    else if (UI_OP == 15) UI_RES = gb_form_build_url();
    else if (UI_OP == 16) {
        if (BUI_SCREEN_MODE == 7) PIC_PAGE3_K = PIC_PAGE4_K = 0;
        if (BUI_CACHE_PAGE) {
            PIC_PAGE_K = BUI_CACHE_PAGE;
            PIC_PAGE2_K = 0;
            gb_pic_close();
            BUI_CACHE_PAGE = 0;
        }
        if (BUI_IMAGE_PAGE) {
            PIC_PAGE_K = BUI_IMAGE_PAGE;
            PIC_PAGE2_K = PIC_PAGE3_K = PIC_PAGE4_K = 0;
            gb_pic_close();
            BUI_IMAGE_PAGE = 0;
        }
        if (BUI_IMAGE_PAGE2) {
            PIC_PAGE_K = BUI_IMAGE_PAGE2;
            PIC_PAGE2_K = PIC_PAGE3_K = PIC_PAGE4_K = 0;
            gb_pic_close();
            BUI_IMAGE_PAGE2 = 0;
        }
        BUI_SCREEN_MODE = 0;
    }
    else if (UI_OP == 17) {
        BUI_SCREEN_MODE = UI_N;
        if (!BUI_IMAGE_PAGE && BUI_CACHE_PAGE) BUI_IMAGE_PAGE = alloc_page();
        if (!BUI_IMAGE_PAGE2 && BUI_IMAGE_PAGE) BUI_IMAGE_PAGE2 = alloc_page();
        reset_image_cache();
    }
    else if (UI_OP == 18) UI_RES = gb_url_resolve();
    else if (UI_OP == 19) {
        UI_RES = prepare_request();
        if (!UI_RES) request_error_text();
    }
    else if (UI_OP == 20) {
        BUI_SCREEN_MODE = UI_N;
        BUI_CACHE_PAGE = alloc_page();
        BUI_IMAGE_PAGE = BUI_CACHE_PAGE ? alloc_page() : 0;
        BUI_IMAGE_PAGE2 = BUI_IMAGE_PAGE ? alloc_page() : 0;
        reset_image_cache();
        UI_RES = (unsigned char)(BUI_CACHE_PAGE != 0);
    }
    else UI_RES = 0;
}
