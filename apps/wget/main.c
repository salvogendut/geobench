/* WGET.APP - small graphical HTTP downloader for GEOBENCH.
 *
 * CPC uses the paged GBNET service (Net4CPC/W5100 or M4ROM). PCW uses the
 * PerryNet binary protocol over the CPS8256 serial port. Response data is
 * accumulated in a small app buffer and written with the filesystem's
 * create/append protocol, so the download size is not limited by app RAM. */
#include "gb.h"
#ifdef GB_PCW
#include "gbperrynet.h"
#endif

#define URL_MAX       95
#define HOST_MAX      63
#define PATH_MAX      95
#define REQUEST_MAX   223
#define NETBUF_SIZE   192
#define FILEBUF_SIZE  384
#define HEADER_MAX    111
#define FS_LOAD_OFS   ((volatile unsigned char *)0x144C)
#define FS_XFLAGS     (*(volatile unsigned char *)0x144F)

#ifdef GB_PCW
#define WIN_X 5
#define WIN_Y 42
#define WIN_W 80
#define WIN_H 154
#else
#define WIN_X 4
#define WIN_Y 24
#define WIN_W 72
#define WIN_H 154
#endif

#define URL_Y       22
#define FIELD_Y     34
#define FIELD_H     13
#define DEST_Y      58
#define DESTVAL_Y   70
#define BUTTON_Y    88
#define BUTTON_H    15
#define STATUS_Y    112
#define PROGRESS_Y  132

#define MAX_REDIRECTS 4

#define ST_IDLE     0
#define ST_INIT     1
#define ST_RESOLVE  2
#define ST_CONNECT  3
#define ST_SEND     4
#define ST_RECV     5
#define ST_PREPARE  6

static char url[URL_MAX + 1];
static char host[HOST_MAX + 1];
static char path[PATH_MAX + 1];
static char request[REQUEST_MAX + 1];
static char header_line[HEADER_MAX + 1];
static char status_buf[24];
static char progress_buf[28];
static char dest_text[15];
static char url_view[43];
static char dest_name[11];
static unsigned char netbuf[NETBUF_SIZE];
static unsigned char filebuf[FILEBUF_SIZE];

static const char *status_text;
static unsigned int port;
static unsigned int file_used;
static unsigned long bytes_done;
static unsigned long expected;
static unsigned long chunk_left;
#ifndef GB_PCW
static unsigned long resume_from;
static unsigned long range_start;
static unsigned long range_total;
#endif
static unsigned int status_code;
static unsigned int idle_frames;
static unsigned char state;
static unsigned char dest_drive;
static unsigned char drive_mask;
static unsigned char editing;
static unsigned char drawn;
static unsigned char file_created;
static unsigned char have_length;
static unsigned char header_done;
static unsigned char line_len;
static unsigned char line_overflow;
static unsigned char first_header;
static unsigned char chunked;
static unsigned char chunk_state;
static unsigned char chunk_have_digit;
static unsigned char progress_div;
static unsigned char socket_open;
static unsigned char probe_pending;
static unsigned char transport_rx_status;
static unsigned char redirect_count;
static unsigned char have_location;
#ifndef GB_PCW
static unsigned char have_content_range;
static unsigned char range_total_known;
static unsigned char range_unsatisfied;
#endif

static const unsigned char netcfg[22] = {
    192,168,99,50, 255,255,255,0, 192,168,99,1, 8,8,8,8,
    0xDE,0xAD,0xBE,0xEF,0x00,0xFF
};

static unsigned char lower(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
    return c;
}

static unsigned char upper(unsigned char c)
{
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - ('a' - 'A'));
    return c;
}

static unsigned char ci_prefix(const char *s, const char *p)
{
    while (*p) {
        if (!*s) return 0;
        if (lower((unsigned char)*s++) != lower((unsigned char)*p++)) return 0;
    }
    return 1;
}

static unsigned char ci_contains(const char *s, const char *word)
{
    while (*s) {
        if (ci_prefix(s, word)) return 1;
        s++;
    }
    return 0;
}

static char *put_dec(char *p, unsigned int v)
{
    unsigned int d = 10000;
    unsigned char started = 0;
    while (d) {
        unsigned char n = 0;
        while (v >= d) { v -= d; n++; }
        if (n || started || d == 1) {
            *p++ = (char)('0' + n);
            started = 1;
        }
        d /= 10;
    }
    *p = 0;
    return p;
}

static char *put_text(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

#ifndef GB_PCW
static void resume_decimal_add(unsigned int amount)
{
    while (amount--) {
        unsigned char i = 0;
        while (status_buf[i]) i++;
        while (i) {
            i--;
            if (status_buf[i] < '9') { status_buf[i]++; break; }
            status_buf[i] = '0';
        }
        if (!i && status_buf[0] == '0') {
            unsigned char n = 0;
            while (status_buf[n]) n++;
            while (n) { status_buf[n] = status_buf[n - 1]; n--; }
            status_buf[0] = '1';
        }
    }
}
#endif

static unsigned int text_len(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static void draw_status(void);
static void draw_content(void);
static void fail_download(const char *s);
static void reset_response(void);

static void set_status(const char *s)
{
    status_text = s;
    if (drawn) draw_status();
}

static unsigned char drive_letter(unsigned char drive)
{
#ifdef GB_MSX2
    return gb_msx_drive_letter(drive);
#else
    if (drive == GB_DRIVE_A) return 'A';
    if (drive == GB_DRIVE_B) return 'B';
    return 'C';
#endif
}

static void format_dest(void)
{
    unsigned char i, n = 0;
    dest_text[n++] = (char)drive_letter(dest_drive);
    dest_text[n++] = ':';
    for (i = 0; i < 8 && dest_name[i] != ' '; i++) dest_text[n++] = dest_name[i];
    if (dest_name[8] != ' ') {
        dest_text[n++] = '.';
        for (i = 8; i < 11 && dest_name[i] != ' '; i++) dest_text[n++] = dest_name[i];
    }
    dest_text[n] = 0;
}

static unsigned char name_char(unsigned char c)
{
    c = upper(c);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '-' || c == '$') return c;
    return '_';
}

static void derive_name(void)
{
    const char *start = path;
    const char *end = path;
    const char *dot = 0;
    const char *p;
    unsigned char i, n;

    while (*end && *end != '?' && *end != '#') end++;
    p = end;
    while (p > path && p[-1] != '/') p--;
    start = p;
    for (p = start; p < end; p++) if (*p == '.') dot = p;
    for (i = 0; i < 11; i++) dest_name[i] = ' ';
    if (start == end) {
        const char *fallback = "INDEXHTM";
        for (i = 0; i < 5; i++) dest_name[i] = fallback[i];
        for (i = 0; i < 3; i++) dest_name[8 + i] = fallback[5 + i];
        format_dest();
        return;
    }
    if (!dot || dot == start) dot = end;
    n = 0;
    for (p = start; p < dot && n < 8; p++) dest_name[n++] = (char)name_char((unsigned char)*p);
    if (!n) { dest_name[0] = 'F'; dest_name[1] = 'I'; dest_name[2] = 'L'; dest_name[3] = 'E'; }
    if (dot < end) {
        n = 8;
        for (p = dot + 1; p < end && n < 11; p++) dest_name[n++] = (char)name_char((unsigned char)*p);
    }
    format_dest();
}

static unsigned char parse_url(void)
{
    const char *p = url;
    unsigned char n = 0;
    unsigned int pv = 0;

    if (ci_prefix(p, "https://")) { set_status("HTTPS is not supported"); return 0; }
    if (!ci_prefix(p, "http://")) { set_status("URL must start with http://"); return 0; }
    p += 7;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#' && n < HOST_MAX)
        host[n++] = *p++;
    host[n] = 0;
    if (!n || (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#')) {
        set_status("Host name is too long"); return 0;
    }
    port = 80;
    if (*p == ':') {
        p++;
        if (*p < '0' || *p > '9') { set_status("Invalid port number"); return 0; }
        pv = 0;
        while (*p >= '0' && *p <= '9') {
            unsigned char digit = (unsigned char)(*p++ - '0');
            if (pv > 6553 || (pv == 6553 && digit > 5)) {
                set_status("Invalid port number"); return 0;
            }
            pv = (unsigned int)(pv * 10 + digit);
        }
        if (!pv) { set_status("Invalid port number"); return 0; }
        port = (unsigned int)pv;
    }
    if (*p && *p != '/' && *p != '?' && *p != '#') { set_status("Invalid URL"); return 0; }
    n = 0;
    if (!*p || *p == '#') path[n++] = '/';
    else {
        if (*p == '?') path[n++] = '/';
        while (*p && *p != '#' && n < PATH_MAX) path[n++] = *p++;
    }
    path[n] = 0;
    if (*p && *p != '#') { set_status("URL path is too long"); return 0; }
    derive_name();
    return 1;
}

static unsigned char redirect_copy(char *dst, const char *src, unsigned char max)
{
    unsigned char n = 0;
    while (*src && *src != '#') {
        if (n >= max) return 0;
        dst[n++] = *src++;
    }
    dst[n] = 0;
    return 1;
}

/* Resolve the common Location forms without allocating another URL buffer.
 * request[] holds the Location value after the response request has been sent;
 * header_line[] is free once the blank line terminates the headers. */
static unsigned char resolve_redirect(void)
{
    const char *loc = request;
    const char *p;
    const char *base_end = path;
    unsigned char n = 0;

    header_line[0] = 0;
    if (!*loc) return 0;
    if (ci_prefix(loc, "http://") || ci_prefix(loc, "https://")) {
        if (!redirect_copy(url, loc, URL_MAX)) return 0;
        return parse_url();
    } else if (loc[0] == '/' && loc[1] == '/') {
        url[0] = 'h'; url[1] = 't'; url[2] = 't'; url[3] = 'p'; url[4] = ':';
        if (!redirect_copy(url + 5, loc, URL_MAX - 5)) return 0;
        return parse_url();
    } else if (*loc == '/') {
        if (!redirect_copy(header_line, loc, PATH_MAX)) return 0;
    } else if (*loc == '?') {
        p = path;
        while (*p && *p != '?') {
            if (n >= PATH_MAX) return 0;
            header_line[n++] = *p++;
        }
        header_line[n] = 0;
        if (!redirect_copy(header_line + n, loc, PATH_MAX - n)) return 0;
    } else if (*loc == '#') {
        if (!redirect_copy(header_line, path, PATH_MAX)) return 0;
    } else {
        p = path;
        while (*p && *p != '?') {
            if (*p == '/') base_end = p + 1;
            p++;
        }
        for (p = path; p < base_end; p++) {
            if (n >= PATH_MAX) return 0;
            header_line[n++] = *p;
        }
        header_line[n] = 0;
        if (!redirect_copy(header_line + n, loc, PATH_MAX - n)) return 0;
    }
    for (n = 0; header_line[n]; n++) path[n] = header_line[n];
    path[n] = 0;
    derive_name();
    return 1;
}

static unsigned char build_request(void)
{
    char *p = request;
    char *limit = request + REQUEST_MAX;
    const char *s;
#define ADD_TEXT(t) do { s = (t); while (*s && p < limit) *p++ = *s++; } while (0)
    ADD_TEXT("GET ");
    ADD_TEXT(path);
    ADD_TEXT(" HTTP/1.0\r\nHost: ");
    ADD_TEXT(host);
    if (port != 80) {
        if (p < limit) *p++ = ':';
        p = put_dec(p, port);
    }
    ADD_TEXT("\r\nUser-Agent: GEOBENCH-WGET/0.1\r\nAccept: */*");
#ifndef GB_PCW
    if (resume_from) {
        ADD_TEXT("\r\nRange: bytes=");
        ADD_TEXT(status_buf);
        if (p < limit) *p++ = '-';
    }
#endif
    ADD_TEXT("\r\nConnection: close\r\n\r\n");
    if (p >= limit) { request[REQUEST_MAX] = 0; return 0; }
    *p = 0;
    return 1;
#undef ADD_TEXT
}

static void select_root(unsigned char drive)
{
    unsigned char i;
    gb_set_drive(drive);
    for (i = 0; i < 8; i++) gb_back();
}

/* ---- transport ----------------------------------------------------------- */
#ifdef GB_PCW
static volatile unsigned char ser_io;
static unsigned char pcw_ser_inited;

static void ser_in_data(void) __naked
{ __asm
    in a,(0xE0)
    ld (_ser_io),a
    ret
__endasm; }
static void ser_out_data(void) __naked
{ __asm
    ld a,(_ser_io)
    out (0xE0),a
    ret
__endasm; }
static void ser_in_status(void) __naked
{ __asm
    xor a
    out (0xE1),a
    in a,(0xE1)
    ld (_ser_io),a
    ret
__endasm; }
static void ser_out_ctrl(void) __naked
{ __asm
    ld a,(_ser_io)
    out (0xE1),a
    ret
__endasm; }
static void pit_out_tx(void) __naked
{ __asm
    ld a,(_ser_io)
    out (0xE4),a
    ret
__endasm; }
static void pit_out_rx(void) __naked
{ __asm
    ld a,(_ser_io)
    out (0xE5),a
    ret
__endasm; }
static void pit_out_ctrl(void) __naked
{ __asm
    ld a,(_ser_io)
    out (0xE7),a
    ret
__endasm; }

static void dart_wr(unsigned char reg, unsigned char val)
{
    ser_io = reg; ser_out_ctrl();
    ser_io = val; ser_out_ctrl();
}

static void serial_set_divisor(unsigned char div)
{
    ser_io = 0x36; pit_out_ctrl(); ser_io = div; pit_out_tx(); ser_io = 0; pit_out_tx();
    ser_io = 0x76; pit_out_ctrl(); ser_io = div; pit_out_rx(); ser_io = 0; pit_out_rx();
}

static void serial_hw_init(void)
{
    if (pcw_ser_inited) return;
    serial_set_divisor(13);
    ser_io = 0x18; ser_out_ctrl();
    dart_wr(4, 0x44); dart_wr(3, 0xC1); dart_wr(5, 0x68); dart_wr(1, 0x00);
    pcw_ser_inited = 1;
}

static unsigned char serial_present(void)
{
    serial_hw_init();
    ser_in_status();
    return (unsigned char)(ser_io != 0x7F && ser_io != 0xFF);
}

static void serial_drain(void)
{
    unsigned int budget = 2048, quiet = 60000;
    serial_hw_init();
    while (budget && quiet) {
        ser_in_status();
        if (ser_io & 1) { ser_in_data(); budget--; quiet = 60000; }
        else quiet--;
    }
}

static unsigned int serial_recv(unsigned char *buf, unsigned int max)
{
    unsigned int n = 0;
    while (n < max) {
        ser_in_status();
        if (!(ser_io & 1)) break;
        ser_in_data(); buf[n++] = ser_io;
    }
    return n;
}

static unsigned char serial_send(const unsigned char *buf, unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++) {
        unsigned int guard = 60000;
        unsigned char ready = 0;
        while (guard--) { ser_in_status(); if (ser_io & 4) { ready = 1; break; } }
        if (!ready) return 0;
        ser_io = buf[i]; ser_out_data();
    }
    return 1;
}

#define PN_END             0xC0
#define PN_ESC             0xDB
#define PN_ESC_END         0xDC
#define PN_ESC_ESC         0xDD
#define PN_VERSION         0x01
#define PN_MAX_PAYLOAD     256  /* one HTTP request or one 64-byte receive ACK */
#define PN_FRAME_MAX       (6 + PN_MAX_PAYLOAD + 2)
#define PN_PULL_CHUNK      64
#define PN_OP_TCP_OPEN     0x30
#define PN_OP_TCP_CLOSE    0x31
#define PN_OP_TCP_SEND     0x32
#define PN_OP_TCP_RECV     0x35
#define PN_OP_UART_SET     0x51
#define PN_OP_ACK          0x80
#define PN_OP_EVENT        0x81
#define PN_STATUS_OK       0x00
#define PN_STATUS_BAD_CHANNEL 0x04
#define PN_STATUS_IO_ERROR 0x08
#define PN_EVT_TCP_CLOSED  0x11
#define PN_EVT_TCP_ERROR   0x12

static unsigned char pn_seq, pn_channel, pn_conn, pn_uart_profile, pn_last_status;
static unsigned char pn_frame[PN_FRAME_MAX];
static unsigned int pn_in_len;
static unsigned char pn_in_started, pn_in_esc, pn_in_overflow;

static unsigned int pn_crc16(const unsigned char *data, unsigned int len)
{
    unsigned int crc = 0xFFFF, i;
    unsigned char bit;
    for (i = 0; i < len; i++) {
        crc ^= (unsigned int)data[i] << 8;
        for (bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (unsigned int)((crc << 1) ^ 0x1021)
                                 : (unsigned int)(crc << 1);
    }
    return crc;
}

static void pn_put(unsigned char b) { serial_send(&b, 1); }

static void pn_slip_put(unsigned char b)
{
    if (b == PN_END) { pn_put(PN_ESC); pn_put(PN_ESC_END); }
    else if (b == PN_ESC) { pn_put(PN_ESC); pn_put(PN_ESC_ESC); }
    else pn_put(b);
}

static unsigned char pn_tx(unsigned char op, unsigned char channel,
                           const unsigned char *payload, unsigned int len)
{
    unsigned int i, crc, pos = 0;
    unsigned char seq = ++pn_seq;
    if (len > PN_MAX_PAYLOAD) return 0;
    pn_frame[pos++] = PN_VERSION; pn_frame[pos++] = op; pn_frame[pos++] = seq;
    pn_frame[pos++] = channel; pn_frame[pos++] = (unsigned char)len;
    pn_frame[pos++] = (unsigned char)(len >> 8);
    for (i = 0; i < len; i++) pn_frame[pos++] = payload[i];
    crc = pn_crc16(pn_frame, pos);
    pn_frame[pos++] = (unsigned char)crc; pn_frame[pos++] = (unsigned char)(crc >> 8);
    pn_put(PN_END);
    for (i = 0; i < pos; i++) pn_slip_put(pn_frame[i]);
    pn_put(PN_END);
    return seq;
}

static unsigned char pn_finish_frame(unsigned char *op, unsigned char *seq,
                                     unsigned char *channel, unsigned int *len)
{
    unsigned int payload_len, total, expected_crc;
    if (pn_in_overflow || pn_in_len < 8 || pn_frame[0] != PN_VERSION) return 0;
    payload_len = (unsigned int)pn_frame[4] | ((unsigned int)pn_frame[5] << 8);
    total = 6 + payload_len + 2;
    if (payload_len > PN_MAX_PAYLOAD || total != pn_in_len) return 0;
    expected_crc = (unsigned int)pn_frame[total - 2] | ((unsigned int)pn_frame[total - 1] << 8);
    if (expected_crc != pn_crc16(pn_frame, (unsigned int)(total - 2))) return 0;
    *op = pn_frame[1]; *seq = pn_frame[2]; *channel = pn_frame[3]; *len = payload_len;
    return 1;
}

static unsigned char pn_read_frame(unsigned char *op, unsigned char *seq,
                                   unsigned char *channel, unsigned int *len,
                                   unsigned int spins)
{
    unsigned char b;
    while (spins--) {
        if (!serial_recv(&b, 1)) continue;
        if (b == PN_END) {
            if (pn_in_started && pn_in_len && pn_finish_frame(op, seq, channel, len)) {
                pn_in_len = 0; pn_in_overflow = pn_in_esc = 0; return 1;
            }
            pn_in_started = 1; pn_in_len = 0; pn_in_esc = pn_in_overflow = 0;
            continue;
        }
        if (!pn_in_started) pn_in_started = 1;
        if (b == PN_ESC) { pn_in_esc = 1; continue; }
        if (pn_in_esc) {
            if (b == PN_ESC_END) b = PN_END;
            else if (b == PN_ESC_ESC) b = PN_ESC;
            else { pn_in_esc = 0; continue; }
            pn_in_esc = 0;
        }
        if (pn_in_len < PN_FRAME_MAX) pn_frame[pn_in_len++] = b;
        else pn_in_overflow = 1;
    }
    return 0;
}

static void pn_async(unsigned char op, unsigned char channel, unsigned int len)
{
    if (op == PN_OP_EVENT && len && channel == pn_channel &&
        (pn_frame[6] == PN_EVT_TCP_CLOSED || pn_frame[6] == PN_EVT_TCP_ERROR)) pn_conn = 0;
}

static unsigned char pn_wait_ack(unsigned char want_seq, unsigned char *out,
                                 unsigned int *out_len, unsigned int spins)
{
    unsigned char op, seq, channel, status;
    unsigned int len, n, i;
    pn_last_status = 0xFF;
    while (spins--) {
        if (!pn_read_frame(&op, &seq, &channel, &len, 1)) continue;
        if (op == PN_OP_ACK && seq == want_seq) {
            if (!len) return 0;
            status = pn_frame[6]; pn_last_status = status;
            if (status != PN_STATUS_OK) return 0;
            n = (unsigned int)(len - 1);
            if (out && out_len) {
                if (n > *out_len) n = *out_len;
                for (i = 0; i < n; i++) out[i] = pn_frame[7 + i];
                *out_len = n;
            }
            return 1;
        }
        if (op == PN_OP_EVENT) pn_async(op, channel, len);
    }
    return 0;
}

static void pn_uart_settle(void) __naked
{ __asm
    ld bc,#0x8000
1$: dec bc
    ld a,b
    or c
    jr nz,1$
    ret
__endasm; }

static unsigned char pn_uart_set(unsigned char profile)
{
    unsigned char payload[5], seq, divisor;
    payload[0] = 0x00; payload[1] = 0x4B; divisor = 7;
    if (profile == GB_PERRYNET_PROFILE_9600) {
        payload[0] = 0x80; payload[1] = 0x25; divisor = 13;
    } else if (profile == GB_PERRYNET_PROFILE_41667) {
        payload[1] = 0x96; divisor = 3;
    }
    payload[2] = payload[3] = payload[4] = 0;
    seq = pn_tx(PN_OP_UART_SET, 0, payload, 5);
    if (!seq || !pn_wait_ack(seq, 0, 0, 60000)) return 0;
    serial_set_divisor(divisor); pn_uart_settle();
    pn_in_len = 0; pn_in_started = pn_in_esc = pn_in_overflow = 0;
    pn_uart_profile = profile;
    return 1;
}

static void pn_uart_restore(void)
{
    if (pn_uart_profile == GB_PERRYNET_PROFILE_9600) return;
    if (!pn_uart_set(GB_PERRYNET_PROFILE_9600)) serial_set_divisor(13);
    pn_uart_profile = GB_PERRYNET_PROFILE_9600;
}

static unsigned char tr_init(void)
{
    if (!serial_present()) return 0;
    serial_drain();
    return 1;
}

static unsigned char tr_resolve(void) { return 1; }

static unsigned char tr_connect(void)
{
    unsigned char payload[HOST_MAX + 5], out[8], seq, n = 0;
    unsigned int out_len = sizeof(out);
    while (host[n]) n++;
    pn_seq = pn_channel = pn_conn = 0;
    pn_uart_profile = GB_PERRYNET_PROFILE_9600;
    pn_in_len = 0; pn_in_started = pn_in_esc = pn_in_overflow = 0;
    if (!pn_uart_set(gb_perrynet_profile())) return 0;
    payload[0] = n;
    { unsigned char i; for (i = 0; i < n; i++) payload[i + 1] = (unsigned char)host[i]; }
    payload[n + 1] = (unsigned char)port; payload[n + 2] = (unsigned char)(port >> 8);
    payload[n + 3] = 3;                    /* TCP_NODELAY + host-pulled RX */
    seq = pn_tx(PN_OP_TCP_OPEN, 0, payload, (unsigned int)(n + 4));
    if (!seq || !pn_wait_ack(seq, out, &out_len, 60000) || out_len < 1) {
        pn_uart_restore(); return 0;
    }
    pn_channel = out[0]; pn_conn = (unsigned char)(pn_channel != 0);
    if (!pn_conn) pn_uart_restore();
    return pn_conn;
}

static unsigned char tr_send(const unsigned char *buf, unsigned int len)
{
    unsigned char seq;
    unsigned int out_len = 0;
    seq = pn_tx(PN_OP_TCP_SEND, pn_channel, buf, len);
    return (unsigned char)(seq && pn_wait_ack(seq, 0, &out_len, 60000));
}

static unsigned int tr_recv(unsigned char *buf, unsigned int max)
{
    unsigned char seq, req[2];
    unsigned int out_len;
    if (!pn_channel || !pn_conn) { transport_rx_status = GB_NET_RX_CLOSED; return 0; }
    if (!max) { transport_rx_status = GB_NET_RX_IDLE; return 0; }
    if (max > PN_PULL_CHUNK) max = PN_PULL_CHUNK;
    req[0] = (unsigned char)max; req[1] = (unsigned char)(max >> 8);
    out_len = max;
    seq = pn_tx(PN_OP_TCP_RECV, pn_channel, req, 2);
    if (!seq || !pn_wait_ack(seq, buf, &out_len, 12000)) {
        if (pn_last_status == PN_STATUS_BAD_CHANNEL) {
            pn_conn = 0;
            transport_rx_status = GB_NET_RX_CLOSED;
        } else if (pn_last_status == 0xFF) {
            transport_rx_status = GB_NET_RX_TIMEOUT;
        } else {
            if (pn_last_status == PN_STATUS_IO_ERROR) pn_conn = 0;
            transport_rx_status = GB_NET_RX_ERROR;
        }
        return 0;
    }
    transport_rx_status = out_len ? GB_NET_RX_DATA : GB_NET_RX_IDLE;
    return out_len;
}

static void tr_close(void)
{
    unsigned char seq;
    unsigned int out_len = 0;
    if (pn_channel) {
        seq = pn_tx(PN_OP_TCP_CLOSE, pn_channel, 0, 0);
        if (seq) (void)pn_wait_ack(seq, 0, &out_len, 8000);
    }
    pn_channel = pn_conn = 0; transport_rx_status = GB_NET_RX_CLOSED; pn_uart_restore();
}
#else
static unsigned char ip[4];

static unsigned char parse_ip(const char *s, unsigned char *out)
{
    unsigned char part;
    unsigned int v;
    for (part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return 0;
        v = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned int)(*s++ - '0'); if (v > 255) return 0; }
        out[part] = (unsigned char)v;
        if (part < 3) { if (*s++ != '.') return 0; }
    }
    return (unsigned char)(*s == 0);
}

static unsigned char tr_init(void) { return gb_net_init(netcfg); }
static unsigned char tr_resolve(void)
{
    if (parse_ip(host, ip)) return 1;
    return gb_net_resolve(host, ip);
}
static unsigned char tr_connect(void)
{
    if (!gb_net_open()) return 0;
    socket_open = 1;
    return gb_net_connect(ip, port);
}
static unsigned char tr_send(const unsigned char *buf, unsigned int len) { return gb_net_send(buf, len); }
static unsigned int tr_recv(unsigned char *buf, unsigned int max)
{
    unsigned int n = gb_net_recv(buf, max);
    transport_rx_status = gb_net_recv_status();
    return n;
}
static void tr_close(void)
{
    if (socket_open) gb_net_close();
    socket_open = 0; transport_rx_status = GB_NET_RX_CLOSED;
}
#endif

/* ---- HTTP and file stream ------------------------------------------------ */
static unsigned char flush_file(void)
{
    if (!file_used && file_created) return 1;
    select_root(dest_drive);
    gb_set_name(dest_name);
    FS_XFLAGS = file_created ? 0x06 : 0x04;
    if (!gb_fs_save((char *)filebuf, file_used)) { FS_XFLAGS = 0; return 0; }
    FS_XFLAGS = 0;
    file_created = 1;
    file_used = 0;
    return 1;
}

static unsigned char body_write(const unsigned char *buf, unsigned int len)
{
    while (len--) {
        filebuf[file_used++] = *buf++;
        bytes_done++;
        if (file_used == FILEBUF_SIZE && !flush_file()) {
            fail_download("Disk write failed"); return 0;
        }
    }
    return 1;
}

static void finish_download(void)
{
    if (have_length && bytes_done != expected) {
        fail_download("Incomplete download"); return;
    }
    if (!file_created || file_used) {
        if (!flush_file()) { fail_download("Disk write failed"); return; }
    }
    tr_close();
    state = ST_IDLE; editing = 1;
    status_text = "Download complete";
    if (drawn) draw_content();
}

static void fail_download(const char *s)
{
    if (socket_open) tr_close();
    FS_XFLAGS = 0;
    state = ST_IDLE; editing = 1;
    status_text = s;
    if (drawn) draw_content();
}

/* Compile the shared parser against WGET's proven scalar layout.  Declaring a
 * second state block here changes the CPC data map and failed runtime QA. */
#define GB_HTTP_EXTERNAL_STATE 1
#define GB_HTTP_EXTERNAL_HELPERS 1
#define GB_HTTP_HEADER_LINE header_line
#define GB_HTTP_LOCATION request
#define GB_HTTP_LOCATION_MAX URL_MAX
#define GB_HTTP_TRAILER_MAX HEADER_MAX
#define GB_HTTP_INVALID_RESPONSE() fail_download("Invalid HTTP response")
#define GB_HTTP_INVALID_STATUS() fail_download("Invalid HTTP status")
#define GB_HTTP_BODY_WRITE(buf, len) body_write((buf), (len))
#define GB_HTTP_FINISH() finish_download()
#define GB_HTTP_BAD_CHUNK() fail_download("Bad chunked response")
#define GB_HTTP_BAD_CHUNK_SIZE() fail_download("Bad chunk size")
#define GB_HTTP_CHUNK_TOO_LARGE() fail_download("Chunk is too large")
#define gb_http_content_length expected
#define gb_http_chunk_left chunk_left
#define gb_http_status_code status_code
#define gb_http_line_len line_len
#define gb_http_first_header first_header
#define gb_http_have_length have_length
#define gb_http_chunked chunked
#define gb_http_chunk_state chunk_state
#define gb_http_chunk_have_digit chunk_have_digit
#define gb_http_have_location have_location
#define gb_http_lower lower
#define gb_http_ci_prefix ci_prefix
#define gb_http_ci_contains ci_contains
#ifndef GB_PCW
#define GB_HTTP_ENABLE_RANGE 1
#define gb_http_range_start range_start
#define gb_http_range_total range_total
#define gb_http_have_content_range have_content_range
#define gb_http_range_total_known range_total_known
#define gb_http_range_unsatisfied range_unsatisfied
#endif
#include "gbhttp.h"

static void headers_complete(void)
{
    unsigned char is_redirect = (unsigned char)(status_code == 301 || status_code == 302 ||
                                                status_code == 303 || status_code == 307 ||
                                                status_code == 308);
    header_done = 1;
    if (is_redirect) {
        if (have_location != 1) {
            fail_download(have_location == 2 ? "Redirect URL is too long" :
                                              "Redirect has no Location");
            return;
        }
        if (redirect_count >= MAX_REDIRECTS) {
            fail_download("Too many redirects"); return;
        }
        tr_close();
        if (!resolve_redirect()) { fail_download("Invalid redirect URL"); return; }
        redirect_count++;
        reset_response();
        state = ST_PREPARE;
        set_status("Following redirect...");
        if (drawn) draw_content();
        return;
    }
#ifndef GB_PCW
    if (status_code == 416 && resume_from && have_content_range &&
        range_unsatisfied && range_total_known && range_total == resume_from) {
        bytes_done = expected = resume_from;
        have_length = file_created = 1;
        finish_download();
        return;
    }
#endif
    if (status_code < 200 || status_code >= 300) {
        char *p = status_buf;
        p = put_text(p, "HTTP error "); put_dec(p, status_code);
        fail_download(status_buf); return;
    }
#ifdef GB_PCW
    if (status_code == 206) {
        fail_download("Unexpected partial response"); return;
    }
#endif
#ifndef GB_PCW
    if (status_code == 206) {
        unsigned long body_length = expected;
        if (!resume_from || !have_content_range || range_unsatisfied ||
            !range_total_known || range_total <= resume_from ||
            range_start != resume_from) {
            fail_download("Invalid resume response"); return;
        }
        if (have_length && body_length != range_total - resume_from) {
            fail_download("Range length mismatch"); return;
        }
        bytes_done = resume_from;
        file_created = 1;
        expected = range_total; have_length = 1;
        set_status("Resuming download...");
    } else {
        /* A valid 200 means the server ignored Range. The first flush creates
         * the destination afresh, so the old partial is not appended twice. */
        bytes_done = 0;
        file_created = 0;
    }
#endif
    if (chunked && status_code != 206) have_length = 0;
    if (chunked) { chunk_state = 0; chunk_left = 0; chunk_have_digit = 0; }
    if (status_code != 206) set_status("Downloading...");
    if (have_length && expected == 0) finish_download();
}

static void process_rx(const unsigned char *buf, unsigned int len)
{
    unsigned int i = 0, start;
    while (i < len && state == ST_RECV) {
        if (!header_done) {
            unsigned char c = buf[i++];
            if (c == '\r') continue;
            if (c == '\n') {
                if (line_overflow) { fail_download("HTTP header is too long"); return; }
                if (!line_len) { headers_complete(); continue; }
                gb_http_process_header_line();
                line_len = 0;
                if (state != ST_RECV) return;
            } else if (line_len < HEADER_MAX) header_line[line_len++] = (char)c;
            else line_overflow = 1;
            continue;
        }
        if (chunked) {
            if (!gb_http_chunk_byte(buf[i++])) return;
            continue;
        }
        start = i;
        if (have_length) {
            unsigned long remain = expected - bytes_done;
            unsigned int take = (unsigned int)(len - i);
            if (remain < take) take = (unsigned int)remain;
            i = (unsigned int)(i + take);
            if (!body_write(buf + start, take)) return;
            if (bytes_done >= expected) { finish_download(); return; }
        } else {
            i = len;
            if (!body_write(buf + start, (unsigned int)(len - start))) return;
        }
    }
}

static void reset_response(void)
{
    file_used = 0; bytes_done = expected = chunk_left = 0;
    status_code = 0; idle_frames = 0; line_len = 0;
    file_created = have_length = header_done = line_overflow = chunked = 0;
    first_header = 1; chunk_state = chunk_have_digit = 0;
    progress_div = 0; socket_open = have_location = 0;
#ifndef GB_PCW
    resume_from = range_start = range_total = 0;
    have_content_range = range_total_known = range_unsatisfied = 0;
    status_buf[0] = '0'; status_buf[1] = 0;
#endif
}

static void start_download(void)
{
    if (!url[0]) { set_status("Enter an HTTP URL"); return; }
    if (!parse_url()) return;
    reset_response();
    redirect_count = 0;
    select_root(dest_drive);
    editing = 0; state = ST_PREPARE;
#ifdef GB_PCW
    set_status("Preparing download...");
#else
    set_status("Checking partial file...");
#endif
}

static void cancel_download(void)
{
    if (state == ST_IDLE) return;
    fail_download("Download cancelled");
}

static void prepare_tick(void)
{
#ifndef GB_PCW
    unsigned int got;
    select_root(dest_drive);
    gb_set_name(dest_name);
    FS_LOAD_OFS[0] = (unsigned char)resume_from;
    FS_LOAD_OFS[1] = (unsigned char)(resume_from >> 8);
    FS_LOAD_OFS[2] = (unsigned char)(resume_from >> 16);
    FS_XFLAGS = 0x01;
    got = gb_fs_load(gb_copybuf, GB_COPYMAX);
    FS_XFLAGS = 0;
    if (got > GB_COPYMAX) got = GB_COPYMAX;
    if (got) {
        if (resume_from > 0xFFFFFFUL - got) {
            fail_download("Partial file is too large"); return;
        }
        resume_from += got;
        resume_decimal_add(got);
        bytes_done = resume_from;
        if (drawn) draw_status();
        if (got == GB_COPYMAX) return;
    }
#endif
    if (!build_request()) { fail_download("URL is too long"); return; }
    state = ST_INIT;
    set_status("Initializing network...");
}

static void network_tick(void)
{
    unsigned int n = 0;
    unsigned char burst;
    if (state == ST_PREPARE) { prepare_tick(); return; }
    if (state == ST_INIT) {
        if (!tr_init()) { fail_download("Network hardware not found"); return; }
        state = ST_RESOLVE; set_status("Resolving host..."); return;
    }
    if (state == ST_RESOLVE) {
        if (!tr_resolve()) { fail_download("DNS lookup failed"); return; }
        state = ST_CONNECT; set_status("Connecting..."); return;
    }
    if (state == ST_CONNECT) {
        if (!tr_connect()) { fail_download("Connection failed"); return; }
        socket_open = 1; state = ST_SEND; set_status("Sending request..."); return;
    }
    if (state == ST_SEND) {
        if (!tr_send((const unsigned char *)request, text_len(request))) {
            fail_download("Request failed"); return;
        }
        state = ST_RECV; idle_frames = 0; set_status("Waiting for response..."); return;
    }
    if (state != ST_RECV) return;
#ifdef GB_PCW
    burst = 4;
#else
    burst = 2;
#endif
    while (burst-- && state == ST_RECV) {
        n = tr_recv(netbuf, sizeof(netbuf));
        if (!n) break;
        idle_frames = 0;
        process_rx(netbuf, n);
    }
    if (state != ST_RECV) return;
    if (n) {
        if (++progress_div >= 8) { progress_div = 0; if (drawn) draw_status(); }
        return;
    }
    if (transport_rx_status == GB_NET_RX_CLOSED) {
        if (!header_done) fail_download("Connection closed early");
        else if (chunked) fail_download("Incomplete chunked response");
        else if (have_length && bytes_done != expected) fail_download("Incomplete download");
        else finish_download();
        return;
    }
    if (transport_rx_status == GB_NET_RX_ERROR) {
        fail_download("Network receive failed"); return;
    }
    if (++idle_frames > 900) {
        fail_download(transport_rx_status == GB_NET_RX_TIMEOUT ?
                      "Network transport timeout" : "Network timeout");
    }
}

/* ---- GUI ----------------------------------------------------------------- */
static void draw_url_field(void)
{
    unsigned char x = gb_wm_x(), y = gb_wm_y();
    unsigned char n = 0, start, maxchars = 42;
    while (url[n]) n++;
    start = n > maxchars ? (unsigned char)(n - maxchars) : 0;
    n = 0;
    while (url[start] && n < maxchars) url_view[n++] = url[start++];
    url_view[n] = 0;
    gb_field((unsigned char)(x + 3), (unsigned char)(y + FIELD_Y),
             (unsigned char)(gb_wm_w() - 6), FIELD_H, url_view,
             editing ? GB_WIDGET_FOCUSED : 0);
}

static void format_progress(void)
{
    char *p = progress_buf;
    unsigned int done_k = (unsigned int)(bytes_done >> 10);
    unsigned int exp_k = (unsigned int)(expected >> 10);
    if (bytes_done < 1024UL) {
        p = put_dec(p, (unsigned int)bytes_done); p = put_text(p, " bytes");
    } else {
        p = put_dec(p, done_k); p = put_text(p, " KiB");
    }
    if (have_length) {
        p = put_text(p, " / ");
        if (expected < 1024UL) { p = put_dec(p, (unsigned int)expected); put_text(p, " bytes"); }
        else { p = put_dec(p, exp_k); put_text(p, " KiB"); }
    }
}

static void draw_status(void)
{
    unsigned char x = gb_wm_x(), y = gb_wm_y(), w = gb_wm_w();
    gb_curhide();
    gb_fill((unsigned char)(x + 3), (unsigned char)(y + STATUS_Y),
            (unsigned char)(w - 6), 34, 0);
    gb_text((unsigned char)(x + 3), (unsigned char)(y + STATUS_Y), status_text);
    format_progress();
    gb_text((unsigned char)(x + 3), (unsigned char)(y + PROGRESS_Y), progress_buf);
    gb_curshow();
}

static void draw_content(void)
{
    unsigned char x = gb_wm_x(), y = gb_wm_y(), w = gb_wm_w();
    gb_curhide();
    gb_fill((unsigned char)(x + 1), (unsigned char)(y + 14),
            (unsigned char)(w - 2), (unsigned char)(gb_wm_h() - 15), 0);
    gb_text((unsigned char)(x + 3), (unsigned char)(y + URL_Y), "URL");
    draw_url_field();
    gb_text((unsigned char)(x + 3), (unsigned char)(y + DEST_Y), "Destination");
    gb_text((unsigned char)(x + 3), (unsigned char)(y + DESTVAL_Y), dest_text);
    gb_button((unsigned char)(x + 3), (unsigned char)(y + BUTTON_Y),
              20, BUTTON_H, "Drive...", 0);
    gb_button((unsigned char)(x + 28), (unsigned char)(y + BUTTON_Y),
              24, BUTTON_H, state == ST_IDLE ? "Download" : "Cancel", 0);
    gb_curshow();
    drawn = 1;
    draw_status();
}

static void choose_drive(void)
{
    const char *items[3];
    unsigned char map[3], n = 0, sel;
#ifdef GB_MSX2
    static char labels[3][7] = { "Disk A", "Disk B", "Disk C" };
    unsigned char i;
    for (i = 0; i < GB_MSX_DRIVE_COUNT; i++) {
        labels[i][5] = (char)gb_msx_drive_letter(i);
        items[n] = labels[i]; map[n++] = i;
    }
#else
    if (drive_mask & GB_DRV_C) { items[n] = "Disk C (card)"; map[n++] = GB_DRIVE_C; }
    if (drive_mask & GB_DRV_A) { items[n] = "Disk A"; map[n++] = GB_DRIVE_A; }
    if (drive_mask & GB_DRV_B) { items[n] = "Disk B"; map[n++] = GB_DRIVE_B; }
#endif
    if (!n) { set_status("No writable drive found"); return; }
    sel = gb_popup((unsigned char)(gb_wm_x() + 3),
                   (unsigned char)(gb_wm_y() + BUTTON_Y + BUTTON_H), items, n);
    if (sel != 0xFF) { dest_drive = map[sel]; format_dest(); }
    gb_restore_parent();
}

static void handle_click(void)
{
    unsigned char x = gb_wm_x(), y = gb_wm_y();
    unsigned char mx = gb_mx(), my = gb_my();
    if (state == ST_IDLE &&
        gb_widget_hit((unsigned char)(x + 3), (unsigned char)(y + FIELD_Y),
                      (unsigned char)(gb_wm_w() - 6), FIELD_H, mx, my)) {
        editing = 1; gb_curhide(); draw_url_field(); gb_curshow(); return;
    }
    if (state == ST_IDLE &&
        gb_widget_hit((unsigned char)(x + 3), (unsigned char)(y + BUTTON_Y),
                      20, BUTTON_H, mx, my)) {
        choose_drive();
        return;
    }
    if (gb_widget_hit((unsigned char)(x + 28), (unsigned char)(y + BUTTON_Y),
                      24, BUTTON_H, mx, my)) {
        if (state == ST_IDLE) start_download(); else cancel_download();
        draw_content();
    }
}

static void handle_keys(void)
{
    unsigned char c, count = 8, n = 0;
    if (state != ST_IDLE || !editing) return;
    while (url[n]) n++;
    while (count-- && (c = gb_getkey()) != 0) {
        if (c == 0x0D) { start_download(); draw_content(); return; }
        if ((c == 8 || c == 0x7F) && n) {
            url[--n] = 0; gb_curhide(); draw_url_field(); gb_curshow();
        }
        else if (c >= 32 && c < 127 && n < URL_MAX) {
            url[n++] = (char)c; url[n] = 0;
            gb_curhide(); draw_url_field(); gb_curshow();
        }
    }
}

static unsigned char drive_available(unsigned char drive)
{
#ifdef GB_MSX2
    return (unsigned char)(drive < GB_MSX_DRIVE_COUNT);
#else
    if (drive == GB_DRIVE_C) return (unsigned char)((drive_mask & GB_DRV_C) != 0);
    if (drive == GB_DRIVE_A) return (unsigned char)((drive_mask & GB_DRV_A) != 0);
    return (unsigned char)((drive_mask & GB_DRV_B) != 0);
#endif
}

static void frame_tick(void)
{
    if (probe_pending) {
        set_status("Checking drives...");
        drive_mask = gb_drives();
        if (!drive_available(dest_drive)) {
#ifdef GB_MSX2
            dest_drive = gb_boot_drive;
#else
            if (drive_mask & GB_DRV_C) dest_drive = GB_DRIVE_C;
            else if (drive_mask & GB_DRV_A) dest_drive = GB_DRIVE_A;
            else dest_drive = GB_DRIVE_B;
#endif
        }
        format_dest();
        probe_pending = 0;
        editing = 1;
        status_text = drive_mask ? "Enter an HTTP URL" : "No writable drive found";
        if (drawn) draw_content();
        return;
    }
    handle_keys();
    network_tick();
}

static void drag_window(void)
{
    unsigned char x = gb_wm_x(), y = gb_wm_y();
    if (gb_drag_window(&x, &y, gb_wm_w(), gb_wm_h())) {
        gb_wm_setpos(x, y); gb_restore_parent();
    }
}

static void wget_proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  draw_content(); break;
        case GB_MSG_CLICK: handle_click(); break;
        case GB_MSG_FRAME: frame_tick(); break;
        case GB_MSG_CLOSE: cancel_download(); gb_wm_close(); break;
        case GB_MSG_DRAG:  drag_window(); break;
    }
}

static const gb_mwin_t wget_window = {
    WIN_X, WIN_Y, WIN_W, WIN_H, 0, 0, wget_proc, "Web Download"
};

void main(void)
{
    unsigned char i;
    for (i = 0; i < 11; i++) dest_name[i] = ' ';
    dest_name[0] = 'I'; dest_name[1] = 'N'; dest_name[2] = 'D'; dest_name[3] = 'E'; dest_name[4] = 'X';
    dest_name[8] = 'H'; dest_name[9] = 'T'; dest_name[10] = 'M';
    url[0] = 0;
#ifdef GB_PCW
    dest_drive = GB_DRIVE_A;
#elif defined(GB_MSX2)
    dest_drive = gb_boot_drive;
#else
    dest_drive = GB_DRIVE_C;
#endif
    drive_mask = 0;
    format_dest();
    status_text = "Checking drives...";
    state = ST_IDLE; editing = 0; drawn = 0; probe_pending = 1;
    gb_wm_managed(&wget_window);
    for (i = 64; i; i--) if (!gb_getkey()) break;
    gb_restore_parent();
}
