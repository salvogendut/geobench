/* Browser transport: GBNET on CPC, PerryNet binary protocol on PCW.
 * The including unit provides host, port, socket_open, transport_rx_status,
 * and (on CPC) netcfg. */
#ifndef BROWSER_TR_HOST
#define BROWSER_TR_HOST host
#define BROWSER_TR_PORT port
#endif
#ifdef GB_PCW
#include "gbperrynet.h"

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

#define PN_END                 0xC0
#define PN_ESC                 0xDB
#define PN_ESC_END             0xDC
#define PN_ESC_ESC             0xDD
#define PN_VERSION             0x01
#define PN_MAX_PAYLOAD         256
#define PN_FRAME_MAX           (6 + PN_MAX_PAYLOAD + 2)
#define PN_PULL_CHUNK          64
#define PN_OP_TCP_OPEN         0x30
#define PN_OP_TCP_CLOSE        0x31
#define PN_OP_TCP_SEND         0x32
#define PN_OP_TCP_RECV         0x35
#define PN_OP_UART_SET         0x51
#define PN_OP_ACK              0x80
#define PN_OP_EVENT            0x81
#define PN_STATUS_OK           0x00
#define PN_STATUS_BAD_CHANNEL  0x04
#define PN_STATUS_IO_ERROR     0x08
#define PN_EVT_TCP_CLOSED      0x11
#define PN_EVT_TCP_ERROR       0x12

static unsigned char pn_seq, pn_channel, pn_conn, pn_uart_profile, pn_last_status;
#ifdef BROWSER_PCW_FRAME_BUFFER
#define pn_frame BROWSER_PCW_FRAME_BUFFER
#else
static unsigned char pn_frame[PN_FRAME_MAX];
#endif
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
    while (BROWSER_TR_HOST[n]) n++;
    pn_seq = pn_channel = pn_conn = 0;
    pn_uart_profile = GB_PERRYNET_PROFILE_9600;
    pn_in_len = 0; pn_in_started = pn_in_esc = pn_in_overflow = 0;
    if (!pn_uart_set(gb_perrynet_profile())) return 0;
    payload[0] = n;
    { unsigned char i; for (i = 0; i < n; i++) payload[i + 1] = (unsigned char)BROWSER_TR_HOST[i]; }
    payload[n + 1] = (unsigned char)BROWSER_TR_PORT;
    payload[n + 2] = (unsigned char)(BROWSER_TR_PORT >> 8);
    payload[n + 3] = 3;
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
            pn_conn = 0; transport_rx_status = GB_NET_RX_CLOSED;
        } else if (pn_last_status == 0xFF) transport_rx_status = GB_NET_RX_TIMEOUT;
        else {
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
    pn_channel = pn_conn = 0;
    socket_open = 0;
    transport_rx_status = GB_NET_RX_CLOSED;
    pn_uart_restore();
}
#else
static unsigned char ip[4];
static unsigned char ip_part, ip_octet, ip_digit, ip_twice, ip_eight;

static unsigned char parse_ip(const char *s, unsigned char *out)
{
    for (ip_part = 0; ip_part < 4; ip_part++) {
        if (*s < '0' || *s > '9') return 0;
        ip_octet = 0;
        while (*s >= '0' && *s <= '9') {
            ip_digit = (unsigned char)(*s++ - '0');
            if (ip_octet > 25 || (ip_octet == 25 && ip_digit > 5)) return 0;
            ip_twice = (unsigned char)(ip_octet + ip_octet);
            ip_eight = (unsigned char)(ip_twice + ip_twice);
            ip_eight = (unsigned char)(ip_eight + ip_eight);
            ip_octet = (unsigned char)(ip_eight + ip_twice + ip_digit);
        }
        out[ip_part] = ip_octet;
        if (ip_part < 3 && *s++ != '.') return 0;
    }
    return (unsigned char)(*s == 0);
}

static unsigned char tr_init(void) { return gb_net_init(netcfg); }
static unsigned char tr_resolve(void)
{
    if (parse_ip(BROWSER_TR_HOST, ip)) return 1;
    return gb_net_resolve(BROWSER_TR_HOST, ip);
}
static unsigned char tr_connect(void)
{
    if (!gb_net_open()) return 0;
    socket_open = 1;
    return gb_net_connect(ip, BROWSER_TR_PORT);
}
static unsigned char tr_send(const unsigned char *buf, unsigned int len)
{
    return gb_net_send(buf, len);
}
static unsigned int tr_recv(unsigned char *buf, unsigned int max)
{
    unsigned int n = gb_net_recv(buf, max);
    transport_rx_status = gb_net_recv_status();
    return n;
}
static void tr_close(void)
{
    if (socket_open) gb_net_close();
    socket_open = 0;
    transport_rx_status = GB_NET_RX_CLOSED;
}
#endif
