/* NETTEST.APP - small GEOBENCH network diagnostic (#261).
 *
 * Runs a fixed TCP probe through the public gb_net_* API:
 *   init -> DNS example.com -> open -> connect :80 -> HTTP GET -> recv.
 * It is intentionally simple so it can validate whichever backend the kernel
 * selects (Albireo/Net4CPC or M4 native) without Telnet UI/input noise.
 *
 * On PCW this app has no paged GBNET module yet; it speaks the PerryNet framed
 * protocol over the CPS8256 serial port, which is the PerryFi/PerryNet path in
 * 1985.
 */
#include "gb.h"
#ifdef GB_PCW
#include "gbperrynet.h"
#endif

#define DEF_X    8
#define DEF_Y    18
#define DEF_W    64
#define DEF_H    132
#define TITLE_H  14
#define NLINES   13
#define LINELEN  48

#define STEP_INIT    1
#define STEP_DNS     2
#define STEP_OPEN    3
#define STEP_CONNECT 4
#define STEP_SEND    5
#define STEP_RECV    6
#define STEP_NTP_DNS 7
#define STEP_UDP_OPEN 8
#define STEP_NTP_SEND 9
#define STEP_NTP_RECV 10
#define STEP_DONE    11
#define STEP_FAIL    12

static char lines[NLINES][LINELEN];
static unsigned char step, wait, opened, drawn;
static unsigned char ip[4];
static unsigned char rxbuf[128];

static const char host[] = "example.com";
#ifdef GB_PCW
static const char ntp_host[] = "time.google.com";
#endif
static const unsigned char netcfg[22] = {
    192,168,99,50,  255,255,255,0,  192,168,99,1,  8,8,8,8,
    0xDE,0xAD,0xBE,0xEF,0x26,0x1A
};
static const unsigned char request[] =
    "GET / HTTP/1.0\r\n"
    "Host: example.com\r\n"
    "User-Agent: GEOBENCH-NETTEST\r\n"
    "Connection: close\r\n\r\n";

static unsigned char nt_init(const unsigned char *cfg);
static unsigned char nt_resolve(const char *name, unsigned char *out_ip);
static unsigned char nt_open(void);
static unsigned char nt_connect(const unsigned char *addr, unsigned int port);
static unsigned char nt_send(const unsigned char *buf, unsigned int len);
static unsigned int nt_recv(unsigned char *buf, unsigned int max);
static unsigned char nt_connected(void);
static void nt_close(void);
static void log_rx(unsigned int n);
#ifdef GB_PCW
static unsigned char nt_udp_open(void);
static unsigned char nt_udp_send(const unsigned char *addr, unsigned int port,
                                 const unsigned char *buf, unsigned int len);
static unsigned int nt_udp_recv(unsigned char *buf, unsigned int max);
static void nt_udp_close(void);
#endif

static void copy(char *dst, const char *src)
{
    unsigned char i = 0;
    while (src[i] && i < LINELEN - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
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

static char *put_ip_byte(char *p, unsigned char v)
{
    unsigned char hundreds = 0, tens = 0;
    while (v >= 100) { v -= 100; hundreds++; }
    while (v >= 10) { v -= 10; tens++; }
    if (hundreds) *p++ = (char)('0' + hundreds);
    if (hundreds || tens) *p++ = (char)('0' + tens);
    *p++ = (char)('0' + v);
    *p = 0;
    return p;
}

static void draw_line(unsigned char n)
{
    unsigned char x = gb_wm_x();
    unsigned char y = gb_wm_y();
    unsigned char w = gb_wm_w();
    unsigned char yy = (unsigned char)(y + TITLE_H + 2 + n * 8);
    gb_fill((unsigned char)(x + 2), yy, (unsigned char)(w - 4), 8, 0);
    gb_text((unsigned char)(x + 2), yy, lines[n]);
}

static void log_line(unsigned char n, const char *s)
{
    if (n >= NLINES) return;
    copy(lines[n], s);
    if (drawn) draw_line(n);
}

static void log_ip(unsigned char n, const char *prefix)
{
    char t[LINELEN];
    char *p = t;
    unsigned char i = 0;
    while (prefix[i] && p < t + LINELEN - 1) *p++ = prefix[i++];
    p = put_ip_byte(p, ip[0]); *p++ = '.';
    p = put_ip_byte(p, ip[1]); *p++ = '.';
    p = put_ip_byte(p, ip[2]); *p++ = '.';
    p = put_ip_byte(p, ip[3]);
    *p = 0;
    log_line(n, t);
}

static void log_count(unsigned char n, const char *prefix, unsigned int count)
{
    char t[LINELEN];
    char *p = t;
    unsigned char i = 0;
    while (prefix[i] && p < t + LINELEN - 1) *p++ = prefix[i++];
    put_dec(p, count);
    log_line(n, t);
}

#ifdef GB_PCW
static char *put_hex2(char *p, unsigned char v)
{
    static const char h[] = "0123456789ABCDEF";
    *p++ = h[(v >> 4) & 15];
    *p++ = h[v & 15];
    *p = 0;
    return p;
}

static char *put2(char *p, unsigned char v)
{
    *p++ = (char)('0' + v / 10);
    *p++ = (char)('0' + v % 10);
    *p = 0;
    return p;
}

static void log_time(unsigned char n, unsigned char h, unsigned char m, unsigned char s)
{
    char t[LINELEN];
    char *p = t;
    copy(t, "ntp UTC: ");
    p += 9;
    p = put2(p, h); *p++ = ':';
    p = put2(p, m); *p++ = ':';
    p = put2(p, s);
    log_line(n, t);
}
#endif

#ifdef GB_PCW
#define PN_END             0xC0
#define PN_ESC             0xDB
#define PN_ESC_END         0xDC
#define PN_ESC_ESC         0xDD
#define PN_VERSION         0x01
#define PN_MAX_PAYLOAD     512
#define PN_FRAME_MAX       520
#define PN_RXQ_SIZE        256

#define PN_OP_HELLO        0x01
#define PN_OP_WIFI_CONNECT 0x12
#define PN_OP_WIFI_STATUS  0x14
#define PN_OP_DNS_RESOLVE  0x20
#define PN_OP_TCP_OPEN     0x30
#define PN_OP_TCP_CLOSE    0x31
#define PN_OP_TCP_SEND     0x32
#define PN_OP_UART_SET     0x51
#define PN_OP_UDP_OPEN     0x40
#define PN_OP_UDP_CLOSE    0x41
#define PN_OP_UDP_SEND     0x42
#define PN_OP_ACK          0x80
#define PN_OP_EVENT        0x81
#define PN_OP_TCP_DATA     0x82
#define PN_OP_UDP_DATA     0x83

#define PN_STATUS_OK       0x00
#define PN_EVT_WIFI_UP     0x02
#define PN_EVT_WIFI_DOWN   0x03
#define PN_EVT_TCP_CLOSED  0x11
#define PN_EVT_TCP_ERROR   0x12
#define PN_EVT_UDP_ERROR   0x20

static volatile unsigned char ser_io;
static unsigned char pcw_ser_inited;
static unsigned int pcw_serial_divisor = 13;
static unsigned char pn_seq;
static unsigned char pn_channel;
static unsigned char pn_udp_channel;
static unsigned char pn_wifi_up;
static unsigned char pn_conn;
static unsigned char pn_uart_profile;
static unsigned char pn_rxq[PN_RXQ_SIZE];
static unsigned char pn_rx_head, pn_rx_tail;
static unsigned char pn_udp_buf[64];
static unsigned int pn_udp_len;
static unsigned char pn_frame[PN_FRAME_MAX];
static unsigned int pn_in_len;
static unsigned char pn_in_started, pn_in_esc, pn_in_overflow;
static unsigned int pn_diag_rx_bytes;
static unsigned char pn_diag_bad_frames;
static unsigned char pn_diag_last_rr0;
static unsigned char pn_diag_last_op;
static unsigned char pn_diag_last_seq;
static unsigned char pn_diag_last_status;
static unsigned char pn_diag_ack_fail;
static unsigned char pn_diag_tx_timeout;
static volatile unsigned char soft_hour, soft_min, soft_sec;

#define PN_ACK_TIMEOUT 1
#define PN_ACK_EMPTY   2
#define PN_ACK_STATUS  3
#define SERIAL_TX_SPINS 60000

static void pn_diag_reset(void)
{
    pn_diag_rx_bytes = 0;
    pn_diag_bad_frames = 0;
    pn_diag_last_rr0 = 0;
    pn_diag_last_op = 0xFF;
    pn_diag_last_seq = 0;
    pn_diag_last_status = 0xFF;
    pn_diag_ack_fail = 0;
    pn_diag_tx_timeout = 0;
}

static void log_pn_diag(unsigned char n)
{
    char t[LINELEN];
    char *p = t;
    copy(t, "rr0=");
    p += 4;
    p = put_hex2(p, pn_diag_last_rr0);
    copy(p, " rx=");
    p += 4;
    p = put_dec(p, pn_diag_rx_bytes);
    copy(p, " bad=");
    p += 5;
    put_dec(p, pn_diag_bad_frames);
    log_line(n, t);
}

static void log_pn_probe(unsigned char n, const char *label)
{
    char t[LINELEN];
    char *p = t;
    unsigned char i = 0;
    while (label[i] && p < t + LINELEN - 1) *p++ = label[i++];
    copy(p, " rr0=");
    p += 5;
    p = put_hex2(p, pn_diag_last_rr0);
    copy(p, " rx=");
    p += 4;
    p = put_dec(p, pn_diag_rx_bytes);
    if (pn_diag_tx_timeout) copy(p, " tx");
    log_line(n, t);
}

static void log_pn_last(unsigned char n)
{
    char t[LINELEN];
    char *p = t;
    copy(t, "last op=");
    p += 8;
    p = put_hex2(p, pn_diag_last_op);
    copy(p, " seq=");
    p += 5;
    p = put_hex2(p, pn_diag_last_seq);
    copy(p, " st=");
    p += 4;
    put_hex2(p, pn_diag_last_status);
    log_line(n, t);
}

static void log_wifi_status(unsigned char n, unsigned char status, unsigned char connected)
{
    char t[LINELEN];
    char *p = t;
    copy(t, "wifi st=");
    p += 8;
    p = put_hex2(p, status);
    copy(p, " conn=");
    p += 6;
    *p++ = connected ? '1' : '0';
    *p = 0;
    log_line(n, t);
}

static void log_pn_ack_fail(const char *stage)
{
    char t[LINELEN];
    char *p = t;
    unsigned char i = 0;
    copy(t, "init ");
    p += 5;
    while (stage[i] && p < t + LINELEN - 1) *p++ = stage[i++];
    copy(p, ": ");
    p += 2;
    if (pn_diag_ack_fail == PN_ACK_STATUS) copy(p, "bad status");
    else if (pn_diag_ack_fail == PN_ACK_EMPTY) copy(p, "empty ack");
    else copy(p, "timeout");
    log_line(3, t);
    log_pn_diag(4);
    log_pn_last(5);
}

static void soft_poke(void) __naked
{ __asm
    ld   a, (_soft_hour)
    ld   b, a
    ld   a, (_soft_min)
    ld   c, a
    ld   a, (_soft_sec)
    ld   d, a
    call 0x802D          ; GB_RUN soft-clock setter on PCW
    ret
__endasm; }

static void set_soft_time(unsigned char h, unsigned char m, unsigned char s)
{
    soft_hour = h; soft_min = m; soft_sec = s; soft_poke();
}

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

static void serial_program_divisor(void)
{
    ser_io = 0x36; pit_out_ctrl();
    ser_io = (unsigned char)(pcw_serial_divisor & 0xFF); pit_out_tx();
    ser_io = (unsigned char)(pcw_serial_divisor >> 8); pit_out_tx();
    ser_io = 0x76; pit_out_ctrl();
    ser_io = (unsigned char)(pcw_serial_divisor & 0xFF); pit_out_rx();
    ser_io = (unsigned char)(pcw_serial_divisor >> 8); pit_out_rx();
}

static void serial_set_divisor(unsigned char div)
{
    pcw_serial_divisor = div;
    if (pcw_ser_inited) serial_program_divisor();
}

static void serial_hw_init(void)
{
    if (pcw_ser_inited) return;
    /* Direct-boot GEOBENCH cannot rely on CP/M SETSIO. Program CPS8256
     * channel A for 8N1. Baud is 2MHz PIT / 16 / divisor. */
    serial_program_divisor();
    ser_io = 0x18; ser_out_ctrl();
    dart_wr(4, 0x44);
    dart_wr(3, 0xC1);
    dart_wr(5, 0x68);                 /* TX enable + 8-bit TX, RTS/DTR inactive */
    dart_wr(1, 0x00);
    pcw_ser_inited = 1;
}

static unsigned char serial_present(void)
{
    serial_hw_init();
    ser_in_status();
    return (unsigned char)(ser_io != 0x7F && ser_io != 0xFF);
}

static unsigned char serial_put(unsigned char b)
{
    unsigned int guard = SERIAL_TX_SPINS;
    while (guard--) {
        ser_in_status();
        pn_diag_last_rr0 = ser_io;
        if (ser_io & 0x04) {
            ser_io = b;
            ser_out_data();
            return 1;
        }
    }
    pn_diag_tx_timeout = 1;
    return 0;
}

static unsigned int serial_recv(unsigned char *buf, unsigned int max)
{
    unsigned int n = 0;
    while (n < max) {
        ser_in_status();
        pn_diag_last_rr0 = ser_io;
        if (!(ser_io & 0x01)) break;
        ser_in_data();
        buf[n++] = ser_io;
        pn_diag_rx_bytes++;
    }
    return n;
}

static unsigned int serial_probe_rx(unsigned int spins)
{
    unsigned char b;
    unsigned int before = pn_diag_rx_bytes;
    while (spins--) serial_recv(&b, 1);
    return (unsigned int)(pn_diag_rx_bytes - before);
}

static void serial_drain(void)
{
    unsigned char b;
    unsigned int budget = 2048;
    unsigned int quiet = 60000;
    while (budget && quiet) {
        if (serial_recv(&b, 1)) {
            budget--;
            quiet = 60000;
        } else {
            quiet--;
        }
    }
}

static unsigned int pn_crc16(const unsigned char *data, unsigned int len)
{
    unsigned int crc = 0xFFFF;
    unsigned int i;
    unsigned char bit;
    for (i = 0; i < len; i++) {
        crc ^= (unsigned int)data[i] << 8;
        for (bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (unsigned int)((crc << 1) ^ 0x1021) : (unsigned int)(crc << 1);
    }
    return crc;
}

static unsigned char pn_slip_put(unsigned char b)
{
    if (b == PN_END) {
        if (!serial_put(PN_ESC)) return 0;
        return serial_put(PN_ESC_END);
    } else if (b == PN_ESC) {
        if (!serial_put(PN_ESC)) return 0;
        return serial_put(PN_ESC_ESC);
    }
    return serial_put(b);
}

static unsigned char pn_tx(unsigned char op, unsigned char channel,
                           const unsigned char *payload, unsigned int len)
{
    unsigned int i, crc, pos = 0;
    unsigned char seq = ++pn_seq;
    if (len > PN_MAX_PAYLOAD) return 0;
    pn_frame[pos++] = PN_VERSION;
    pn_frame[pos++] = op;
    pn_frame[pos++] = seq;
    pn_frame[pos++] = channel;
    pn_frame[pos++] = (unsigned char)(len & 0xFF);
    pn_frame[pos++] = (unsigned char)(len >> 8);
    for (i = 0; i < len; i++) pn_frame[pos++] = payload[i];
    crc = pn_crc16(pn_frame, pos);
    pn_frame[pos++] = (unsigned char)(crc & 0xFF);
    pn_frame[pos++] = (unsigned char)(crc >> 8);
    if (!serial_put(PN_END)) return 0;
    for (i = 0; i < pos; i++) {
        if (!pn_slip_put(pn_frame[i])) return 0;
    }
    if (!serial_put(PN_END)) return 0;
    return seq;
}

static void pn_queue_data(const unsigned char *buf, unsigned int len)
{
    unsigned int i;
    unsigned char next;
    for (i = 0; i < len; i++) {
        next = (unsigned char)(pn_rx_head + 1);
        if (next == pn_rx_tail) break;
        pn_rxq[pn_rx_head] = buf[i];
        pn_rx_head = next;
    }
}

static unsigned int pn_take_data(unsigned char *buf, unsigned int max)
{
    unsigned int n = 0;
    while (n < max && pn_rx_tail != pn_rx_head) {
        buf[n++] = pn_rxq[pn_rx_tail];
        pn_rx_tail = (unsigned char)(pn_rx_tail + 1);
    }
    return n;
}

static unsigned char pn_finish_frame(unsigned char *op, unsigned char *seq,
                                     unsigned char *channel, unsigned int *len)
{
    unsigned int payload_len, total, expected_crc, actual_crc;
    if (pn_in_overflow || pn_in_len < 8) return 0;
    if (pn_frame[0] != PN_VERSION) return 0;
    payload_len = (unsigned int)pn_frame[4] | ((unsigned int)pn_frame[5] << 8);
    total = 6 + payload_len + 2;
    if (payload_len > PN_MAX_PAYLOAD || total != pn_in_len) return 0;
    expected_crc = (unsigned int)pn_frame[total - 2] | ((unsigned int)pn_frame[total - 1] << 8);
    actual_crc = pn_crc16(pn_frame, (unsigned int)(total - 2));
    if (expected_crc != actual_crc) return 0;
    *op = pn_frame[1];
    *seq = pn_frame[2];
    *channel = pn_frame[3];
    *len = payload_len;
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
            if (pn_in_started && pn_in_len) {
                if (pn_finish_frame(op, seq, channel, len)) {
                    pn_in_len = 0;
                    pn_in_overflow = 0;
                    pn_in_esc = 0;
                    return 1;
                }
                pn_diag_bad_frames++;
            }
            pn_in_started = 1;
            pn_in_len = 0;
            pn_in_esc = 0;
            pn_in_overflow = 0;
            continue;
        }
        if (!pn_in_started) pn_in_started = 1;
        if (b == PN_ESC) {
            pn_in_esc = 1;
            continue;
        }
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

static void pn_handle_async(unsigned char op, unsigned char channel, unsigned int len)
{
    unsigned char event;
    unsigned int i, n;
    if (op == PN_OP_TCP_DATA && channel == pn_channel) {
        pn_queue_data(pn_frame + 6, len);
    } else if (op == PN_OP_UDP_DATA && channel == pn_udp_channel && len >= 6) {
        n = len - 6;
        if (n > sizeof(pn_udp_buf)) n = sizeof(pn_udp_buf);
        for (i = 0; i < n; i++) pn_udp_buf[i] = pn_frame[12 + i];
        pn_udp_len = n;
    } else if (op == PN_OP_EVENT && len) {
        event = pn_frame[6];
        if (channel == 0 && event == PN_EVT_WIFI_UP) pn_wifi_up = 1;
        if (channel == 0 && event == PN_EVT_WIFI_DOWN) pn_wifi_up = 0;
        if (channel == pn_channel && (event == PN_EVT_TCP_CLOSED || event == PN_EVT_TCP_ERROR))
            pn_conn = 0;
        if (channel == pn_udp_channel && event == PN_EVT_UDP_ERROR)
            pn_udp_channel = 0;
    }
}

static void pn_wait_async(unsigned int spins)
{
    unsigned char op, seq, channel;
    unsigned int len;
    while (spins--) {
        if (!pn_read_frame(&op, &seq, &channel, &len, 1)) continue;
        pn_diag_last_op = op;
        pn_diag_last_seq = seq;
        pn_handle_async(op, channel, len);
    }
}

static unsigned char pn_wait_ack(unsigned char want_seq, unsigned char *out,
                                 unsigned int *out_len, unsigned int spins)
{
    unsigned char op, seq, channel, status;
    unsigned int len, n, i;
    pn_diag_ack_fail = 0;
    while (spins--) {
        if (!pn_read_frame(&op, &seq, &channel, &len, 1)) continue;
        pn_diag_last_op = op;
        pn_diag_last_seq = seq;
        if (op == PN_OP_ACK && seq == want_seq) {
            if (!len) { pn_diag_ack_fail = PN_ACK_EMPTY; return 0; }
            status = pn_frame[6];
            pn_diag_last_status = status;
            if (status != PN_STATUS_OK) { pn_diag_ack_fail = PN_ACK_STATUS; return 0; }
            n = (unsigned int)(len - 1);
            if (out && out_len) {
                if (n > *out_len) n = *out_len;
                for (i = 0; i < n; i++) out[i] = pn_frame[7 + i];
                *out_len = n;
            }
            return 1;
        }
        pn_handle_async(op, channel, len);
    }
    pn_diag_ack_fail = PN_ACK_TIMEOUT;
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
    serial_set_divisor(divisor);
    pn_uart_settle();
    pn_in_len = 0;
    pn_in_started = pn_in_esc = pn_in_overflow = 0;
    pn_uart_profile = profile;
    return 1;
}

static void pn_uart_restore(void)
{
    if (pn_uart_profile == GB_PERRYNET_PROFILE_9600) return;
    if (!pn_uart_set(GB_PERRYNET_PROFILE_9600)) serial_set_divisor(13);
    pn_uart_profile = GB_PERRYNET_PROFILE_9600;
}

static unsigned char nt_init(const unsigned char *cfg)
{
    unsigned char seq;
    unsigned int out_len;
    unsigned char out[24];
    unsigned char tries, wifi_status = 0xFF, wifi_connected = 0;
    (void)cfg;
    pn_diag_reset();
    log_line(6, "serial: pcw setup");
    serial_hw_init();
    serial_probe_rx(60000);
    log_pn_probe(7, "idle");
    if (!serial_present()) {
        log_line(3, "serial: not present");
        log_pn_diag(4);
        return 0;
    }
    serial_drain();
    pn_diag_reset();
    pn_seq = 0;
    pn_channel = 0;
    pn_udp_channel = 0;
    pn_wifi_up = 0;
    pn_conn = 0;
    pn_uart_profile = GB_PERRYNET_PROFILE_9600;
    pn_rx_head = pn_rx_tail = 0;
    pn_udp_len = 0;
    pn_in_len = 0;
    pn_in_started = pn_in_esc = pn_in_overflow = 0;
    out_len = sizeof(out);
    log_line(3, "HELLO...");
    seq = pn_tx(PN_OP_HELLO, 0, 0, 0);
    if (!seq) { log_line(3, "init HELLO: tx fail"); log_pn_diag(4); return 0; }
    if (!pn_wait_ack(seq, out, &out_len, 60000)) { log_pn_ack_fail("HELLO"); return 0; }
    log_line(3, "HELLO ok");
    if (!pn_uart_set(gb_perrynet_profile())) { log_pn_ack_fail("UART"); return 0; }
    pn_diag_reset();
    log_line(4, "WIFI_STATUS...");
    out_len = sizeof(out);
    seq = pn_tx(PN_OP_WIFI_STATUS, 0, 0, 0);
    if (!seq) { log_line(4, "WIFI_STATUS tx fail"); log_pn_diag(5); return 0; }
    if (!pn_wait_ack(seq, out, &out_len, 60000)) { log_pn_ack_fail("WSTAT"); return 0; }
    if (out_len >= 2) {
        wifi_status = out[0];
        wifi_connected = out[1];
        if (wifi_connected) { log_line(4, "WIFI already up"); return 1; }
    }
    log_line(4, "WIFI_CONNECT...");
    out_len = sizeof(out);
    seq = pn_tx(PN_OP_WIFI_CONNECT, 0, 0, 0);
    if (!seq) { log_line(4, "init WIFI: tx fail"); log_pn_diag(5); return 0; }
    if (!pn_wait_ack(seq, out, &out_len, 60000)) { log_pn_ack_fail("WIFI"); return 0; }
    log_line(4, "WIFI_CONNECT ok");
    log_line(5, "WIFI wait...");
    for (tries = 0; tries < 60; tries++) {
        if (pn_wifi_up) { log_line(5, "WIFI ready event"); return 1; }
        out_len = sizeof(out);
        seq = pn_tx(PN_OP_WIFI_STATUS, 0, 0, 0);
        if (!seq) { log_line(5, "WIFI_STATUS tx fail"); log_pn_diag(6); return 0; }
        if (!pn_wait_ack(seq, out, &out_len, 60000)) { log_pn_ack_fail("WSTAT"); return 0; }
        if (out_len >= 2) {
            wifi_status = out[0];
            wifi_connected = out[1];
            if (wifi_connected) { log_line(5, "WIFI ready"); return 1; }
            log_wifi_status(6, wifi_status, wifi_connected);
        }
        pn_wait_async(120000);
    }
    log_line(5, "WIFI wait timeout");
    log_wifi_status(6, wifi_status, wifi_connected);
    log_pn_last(7);
    return 0;
}

static unsigned char nt_resolve(const char *name, unsigned char *out_ip)
{
    unsigned char seq;
    unsigned char out[4];
    unsigned int len = sizeof(out);
    unsigned int n = 0;
    while (name[n]) n++;
    seq = pn_tx(PN_OP_DNS_RESOLVE, 0, (const unsigned char *)name, n);
    if (!seq) { log_line(3, "DNS tx fail"); log_pn_diag(4); return 0; }
    if (!pn_wait_ack(seq, out, &len, 60000)) { log_pn_ack_fail("DNS"); return 0; }
    if (len < 4) {
        log_line(3, "DNS short ack");
        log_pn_diag(4);
        log_pn_last(5);
        return 0;
    }
    out_ip[0] = out[0]; out_ip[1] = out[1]; out_ip[2] = out[2]; out_ip[3] = out[3];
    return 1;
}

static unsigned char nt_open(void)
{
    if (pn_channel) nt_close();
    pn_rx_head = pn_rx_tail = 0;
    return 1;
}

static unsigned char nt_connect(const unsigned char *addr, unsigned int port)
{
    char hostbuf[16];
    char *p = hostbuf;
    unsigned char payload[22];
    unsigned char out[8];
    unsigned char seq;
    unsigned int len, host_len;
    p = put_ip_byte(p, addr[0]); *p++ = '.';
    p = put_ip_byte(p, addr[1]); *p++ = '.';
    p = put_ip_byte(p, addr[2]); *p++ = '.';
    p = put_ip_byte(p, addr[3]);
    host_len = 0;
    while (hostbuf[host_len]) host_len++;
    payload[0] = (unsigned char)host_len;
    for (len = 0; len < host_len; len++) payload[1 + len] = (unsigned char)hostbuf[len];
    payload[1 + host_len] = (unsigned char)(port & 0xFF);
    payload[2 + host_len] = (unsigned char)(port >> 8);
    payload[3 + host_len] = 1;
    len = sizeof(out);
    seq = pn_tx(PN_OP_TCP_OPEN, 0, payload, (unsigned int)(4 + host_len));
    if (!seq || !pn_wait_ack(seq, out, &len, 60000) || len < 1) return 0;
    pn_channel = out[0];
    pn_conn = (unsigned char)(pn_channel != 0);
    return pn_conn;
}

static unsigned char nt_send(const unsigned char *buf, unsigned int len)
{
    unsigned char seq;
    unsigned int out_len = 0;
    unsigned int chunk;
    if (!pn_channel) return 0;
    while (len) {
        chunk = len > PN_MAX_PAYLOAD ? PN_MAX_PAYLOAD : len;
        seq = pn_tx(PN_OP_TCP_SEND, pn_channel, buf, chunk);
        if (!seq || !pn_wait_ack(seq, 0, &out_len, 60000)) return 0;
        buf += chunk;
        len -= chunk;
    }
    return 1;
}

static unsigned int nt_recv(unsigned char *buf, unsigned int max)
{
    unsigned char op, seq, channel;
    unsigned int len;
    unsigned int n = pn_take_data(buf, max);
    if (n) return n;
    if (pn_read_frame(&op, &seq, &channel, &len, 3500)) {
        (void)seq;
        pn_handle_async(op, channel, len);
    }
    return pn_take_data(buf, max);
}

static unsigned char nt_connected(void)
{
    return pn_conn;
}

static unsigned char nt_udp_open(void)
{
    unsigned char seq;
    unsigned char payload[2];
    unsigned char out[3];
    unsigned int out_len = sizeof(out);
    payload[0] = 0; payload[1] = 0;             /* local port 0 = ephemeral */
    if (pn_udp_channel) nt_udp_close();
    pn_udp_len = 0;
    seq = pn_tx(PN_OP_UDP_OPEN, 0, payload, sizeof(payload));
    if (!seq || !pn_wait_ack(seq, out, &out_len, 60000) || out_len < 3) return 0;
    pn_udp_channel = out[0];
    return (unsigned char)(pn_udp_channel != 0);
}

static unsigned char nt_udp_send(const unsigned char *addr, unsigned int port,
                                 const unsigned char *buf, unsigned int len)
{
    unsigned char seq;
    unsigned char payload[6 + 48];
    unsigned int out_len = 0, i;
    if (!pn_udp_channel || len > 48) return 0;
    payload[0] = addr[0]; payload[1] = addr[1]; payload[2] = addr[2]; payload[3] = addr[3];
    payload[4] = (unsigned char)(port & 0xFF);
    payload[5] = (unsigned char)(port >> 8);
    for (i = 0; i < len; i++) payload[6 + i] = buf[i];
    seq = pn_tx(PN_OP_UDP_SEND, pn_udp_channel, payload, (unsigned int)(6 + len));
    return (unsigned char)(seq && pn_wait_ack(seq, 0, &out_len, 60000));
}

static unsigned int nt_udp_recv(unsigned char *buf, unsigned int max)
{
    unsigned char op, seq, channel;
    unsigned int len, i, n;
    if (!pn_udp_len && pn_read_frame(&op, &seq, &channel, &len, 3500)) {
        (void)seq;
        pn_handle_async(op, channel, len);
    }
    n = pn_udp_len;
    if (n > max) n = max;
    for (i = 0; i < n; i++) buf[i] = pn_udp_buf[i];
    pn_udp_len = 0;
    return n;
}

static void nt_udp_close(void)
{
    unsigned char seq;
    unsigned int out_len = 0;
    if (pn_udp_channel) {
        seq = pn_tx(PN_OP_UDP_CLOSE, pn_udp_channel, 0, 0);
        if (seq) (void)pn_wait_ack(seq, 0, &out_len, 8000);
    }
    pn_udp_channel = 0;
    pn_udp_len = 0;
}

static void nt_close(void)
{
    unsigned char seq;
    unsigned int out_len = 0;
    if (pn_channel) {
        seq = pn_tx(PN_OP_TCP_CLOSE, pn_channel, 0, 0);
        if (seq) (void)pn_wait_ack(seq, 0, &out_len, 8000);
    }
    pn_channel = 0;
    pn_conn = 0;
}

static unsigned char ntp_apply(const unsigned char *buf, unsigned int len)
{
    unsigned long sec, day;
    unsigned char h, m, s;
    if (len < 48) return 0;
    sec = ((unsigned long)buf[40] << 24) |
          ((unsigned long)buf[41] << 16) |
          ((unsigned long)buf[42] << 8) |
          (unsigned long)buf[43];
    sec -= 2208988800UL;                       /* NTP epoch -> Unix epoch */
    day = sec % 86400UL;
    h = (unsigned char)(day / 3600UL);
    day %= 3600UL;
    m = (unsigned char)(day / 60UL);
    s = (unsigned char)(day % 60UL);
    set_soft_time(h, m, s);
    log_time(11, h, m, s);
    return 1;
}
#else
static unsigned char nt_init(const unsigned char *cfg) { return gb_net_init(cfg); }
static unsigned char nt_resolve(const char *name, unsigned char *out_ip) { return gb_net_resolve(name, out_ip); }
static unsigned char nt_open(void) { return gb_net_open(); }
static unsigned char nt_connect(const unsigned char *addr, unsigned int port) { return gb_net_connect(addr, port); }
static unsigned char nt_send(const unsigned char *buf, unsigned int len) { return gb_net_send(buf, len); }
static unsigned int nt_recv(unsigned char *buf, unsigned int max) { return gb_net_recv(buf, max); }
static unsigned char nt_connected(void) { return gb_net_connected(); }
static void nt_close(void) { gb_net_close(); }
#endif

static void log_rx(unsigned int n)
{
    char t[LINELEN];
    char *p = t;
    unsigned int i = 0;
    t[0] = 'r'; t[1] = 'x'; t[2] = ':'; t[3] = ' ';
    p = t + 4;
    while (i < n && p < t + LINELEN - 1) {
        unsigned char c = rxbuf[i++];
        if (c == 13 || c == 10) break;
        if (c < 32 || c > 126) c = '.';
        *p++ = (char)c;
    }
    *p = 0;
    log_line(10, t);
}

static void fail(const char *msg)
{
    if (opened) nt_close();
#ifdef GB_PCW
    nt_udp_close();
    pn_uart_restore();
#endif
    opened = 0;
    log_line(11, msg);
    log_line(12, "click window to retry");
    step = STEP_FAIL;
}

static void reset_test(void)
{
    unsigned char i;
    for (i = 0; i < NLINES; i++) lines[i][0] = 0;
    opened = 0;
    wait = 0;
    log_line(0, "NETTEST: example.com:80");
    log_line(1, "Backend is selected by the kernel");
    log_line(2, "Starting...");
    step = STEP_INIT;
}

static void draw(void)
{
    unsigned char i;
    unsigned char x = gb_wm_x();
    unsigned char y = gb_wm_y();
    unsigned char w = gb_wm_w();
    unsigned char h = gb_wm_h();
    gb_fill((unsigned char)(x + 1), (unsigned char)(y + TITLE_H),
            (unsigned char)(w - 2), (unsigned char)(h - TITLE_H - 1), 0);
    drawn = 1;
    for (i = 0; i < NLINES; i++) draw_line(i);
}

static void tick(void)
{
    unsigned int n;
    if (step == STEP_INIT) {
        log_line(2, "init...");
        if (!nt_init(netcfg)) { fail("FAIL: net init"); return; }
        log_line(2, "init ok");
        step = STEP_DNS;
    } else if (step == STEP_DNS) {
        log_line(3, "dns example.com...");
        if (!nt_resolve(host, ip)) { fail("FAIL: DNS"); return; }
        log_ip(3, "dns ok: ");
        step = STEP_OPEN;
    } else if (step == STEP_OPEN) {
        log_line(4, "open socket...");
        if (!nt_open()) { fail("FAIL: open socket"); return; }
        opened = 1;
        log_line(4, "open ok");
        step = STEP_CONNECT;
    } else if (step == STEP_CONNECT) {
        log_line(5, "connect port 80...");
        if (!nt_connect(ip, 80)) { fail("FAIL: connect"); return; }
        log_line(5, "connect ok");
        step = STEP_SEND;
    } else if (step == STEP_SEND) {
        log_line(6, "send HTTP GET...");
        if (!nt_send(request, sizeof(request) - 1)) { fail("FAIL: send"); return; }
        log_line(6, "send ok");
        log_line(7, "recv...");
        wait = 0;
        step = STEP_RECV;
    } else if (step == STEP_RECV) {
        n = nt_recv(rxbuf, sizeof(rxbuf));
        if (n) {
            log_count(8, "recv bytes: ", n);
            log_rx(n);
            nt_close();
            opened = 0;
#ifdef GB_PCW
            log_line(11, "TCP PASS; ntp dns...");
            step = STEP_NTP_DNS;
#else
            log_line(11, "PASS");
            log_line(12, "click window to retry");
            step = STEP_DONE;
#endif
        } else if (!nt_connected()) {
            fail("FAIL: closed with no data");
        } else if (++wait > 180) {
            fail("FAIL: recv timeout");
        }
#ifdef GB_PCW
    } else if (step == STEP_NTP_DNS) {
        log_line(11, "dns time.google.com...");
        if (!nt_resolve(ntp_host, ip)) { fail("FAIL: NTP DNS"); return; }
        log_ip(11, "ntp ip: ");
        step = STEP_UDP_OPEN;
    } else if (step == STEP_UDP_OPEN) {
        log_line(12, "udp open...");
        if (!nt_udp_open()) { fail("FAIL: udp open"); return; }
        log_line(12, "udp open ok");
        step = STEP_NTP_SEND;
    } else if (step == STEP_NTP_SEND) {
        unsigned char i;
        for (i = 0; i < 48; i++) rxbuf[i] = 0;
        rxbuf[0] = 0x1B;                       /* client, NTPv3 */
        log_line(12, "ntp send...");
        if (!nt_udp_send(ip, 123, rxbuf, 48)) { nt_udp_close(); fail("FAIL: ntp send"); return; }
        wait = 0;
        step = STEP_NTP_RECV;
    } else if (step == STEP_NTP_RECV) {
        n = nt_udp_recv(rxbuf, sizeof(rxbuf));
        if (n) {
            nt_udp_close();
            if (!ntp_apply(rxbuf, n)) { fail("FAIL: ntp parse"); return; }
            pn_uart_restore();
            log_line(12, "PASS; click to retry");
            step = STEP_DONE;
        } else if (++wait > 180) {
            nt_udp_close();
            fail("FAIL: ntp timeout");
        }
#endif
    }
}

static void drag(void)
{
    unsigned char x = gb_wm_x();
    unsigned char y = gb_wm_y();
    if (gb_drag_window(&x, &y, gb_wm_w(), gb_wm_h())) {
        gb_wm_setpos(x, y);
        gb_restore_parent();
    }
}

static void proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  draw(); break;
        case GB_MSG_FRAME: tick(); break;
        case GB_MSG_CLICK:
            if (step == STEP_DONE || step == STEP_FAIL) reset_test();
            break;
        case GB_MSG_CLOSE:
            if (opened) nt_close();
#ifdef GB_PCW
            nt_udp_close();
            pn_uart_restore();
#endif
            gb_wm_close();
            break;
        case GB_MSG_DRAG:  drag(); break;
    }
}

static const gb_mwin_t nw = {
    DEF_X, DEF_Y, DEF_W, DEF_H, 0, 0, proc, "Net Test", 0
};

void main(void)
{
    drawn = 0;
    reset_test();
    gb_wm_managed(&nw);
    gb_restore_parent();
}
