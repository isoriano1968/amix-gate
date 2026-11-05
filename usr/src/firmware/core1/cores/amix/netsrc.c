/*
 * netsrc.c -- core 1 fetches a stream itself: HTTP over lwIP (c1netif.c,
 * core 0's rings) into a byte buffer, libavformat demuxes from it through
 * its own AVIOContext, and the packets go into the player's queues
 * (player.c) instead of the 68k's ring.  The 68k only sends the URL and the
 * commands: no TCP in AMIX, no packets over Zorro III.
 *
 * ZZAV_CMD_OPEN_URL (zzav.h): connect, probe, seek to ZZAV_W_START_MS, set
 * the stream words as the 68k does for its own ring, say ZZAV_READY; the
 * 68k sets the overlay up and sends ZZAV_CMD_OPEN as always.  The same URL
 * again only seeks (the connection and the probe are kept).
 *
 * HTTP/1.1 with "Range: bytes=N-" on every request: a 206 says the server
 * seeks (and Content-Range gives the size), so libavformat's seeks outside
 * the buffer become a new request from there.  A 200 (amixcast.py's live
 * MPEG-TS) plays from where it starts and seeks only in what is buffered.
 * Chunked bodies, redirects, a connection lost mid-way (asked again from
 * where it broke, if the server seeks).
 *
 * Nothing here may wait in the player's loop: av_read_frame runs only with
 * LOW_WATER bytes ahead (or the stream's end in), and a read that still has
 * to wait keeps the network and the sound going (wait_idle).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libavformat/avformat.h>
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "zzav.h"
#include "bench.h"
#include "player.h"
#include "c1netif.h"

#define BUF_SIZE        (16 << 20)      /* bytes held: seconds at any bitrate core 1 decodes */
#define KEEP_BEHIND     (1 << 20)       /* kept behind the reader: libavformat looks back */
#define LOW_WATER       (512 << 10)     /* av_read_frame only with this much ahead */
#define NEAR_AHEAD      (512 << 10)     /* a seek this far past what came: wait for it */
#define AVIO_BUF        (64 << 10)
#define WAIT_S          20              /* no byte for this long: the stream is gone */
#define REDIRECTS       5

uint32_t pl_features(void)
{
    return ZZAV_FEAT_URL;
}

static volatile uint32_t *blk;
static uint32_t get(int w) { return __builtin_bswap32(blk[w]); }
static void put(int w, uint32_t v) { blk[w] = __builtin_bswap32(v); }

/* ---- waiting: the network, crypto jobs and the sound go on ---- */

static int cancelled(void)
{
    return get(ZZAV_W_CMD) == ZZAV_CMD_CLOSE;
}

static void wait_idle(void)
{
    hb_service();               /* amix.c: zzcrypto's jobs and c1net_service */
    player_keep();
}

/* ---- the buffer: file bytes [lo, hi), the reader at pos ---- */

static uint8_t *buf;
static int64_t lo, hi, pos;
static uint32_t kb_total, rate_bytes;
static uint64_t rate_t;

static int64_t keep_from(void)
{
    int64_t b = (pos < hi ? pos : hi) - KEEP_BEHIND;

    return b > lo ? b : lo;
}

static int64_t room(void)
{
    return BUF_SIZE - (hi - keep_from());
}

static void body_in(const uint8_t *d, int64_t n)
{
    rate_bytes += (uint32_t)n;
    while (n > 0) {
        int64_t off = hi % BUF_SIZE, k = BUF_SIZE - off;

        if (k > n)
            k = n;
        memcpy(buf + off, d, (size_t)k);
        hi += k;
        d += k;
        n -= k;
    }
    if (hi - lo > BUF_SIZE)
        lo = hi - BUF_SIZE;
}

/* ---- HTTP ---- */

enum { H_IDLE, H_CONNECT, H_HEAD, H_BODY, H_DONE, H_ERROR };
enum { C_SIZE, C_DATA, C_CRLF };

static struct http {
    struct tcp_pcb *pcb;
    int state, status, chunked, ended_ok, redirect, err;
    char host[128], path[1024];
    int port;
    ip_addr_t addr;
    char head[2048];
    int hn;
    int64_t req_off, clen, got, total, skip;
    int ranges;
    char loc[1024];
    int cstate, cn;
    int64_t cleft;
    char cline[32];
    char req[1400];
} h;

static char *i64s(char *p, int64_t v)
{
    char t[24];
    int n = 0;

    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    do
        t[n++] = (char)('0' + v % 10);
    while ((v /= 10) > 0);
    while (n)
        *p++ = t[--n];
    *p = 0;
    return p;
}

static int hdr(const char *line, const char *name, const char **val)
{
    size_t n = strlen(name);

    if (strncasecmp(line, name, n) || line[n] != ':')
        return 0;
    line += n + 1;
    while (*line == ' ' || *line == '\t')
        line++;
    *val = line;
    return 1;
}

static void parse_head(void)
{
    char *l = h.head, *e;
    const char *v;

    h.status = 0;
    h.clen = -1;
    h.chunked = 0;
    h.loc[0] = 0;
    if (!strncmp(l, "HTTP/", 5) && (e = strchr(l, ' ')) != NULL)
        h.status = atoi(e + 1);
    put(ZZAV_W_HTTP, (uint32_t)h.status);
    while ((e = strstr(l, "\r\n")) != NULL && e != l) {
        *e = 0;
        if (hdr(l, "Content-Length", &v))
            h.clen = strtoll(v, NULL, 10);
        else if (hdr(l, "Transfer-Encoding", &v))
            h.chunked = strstr(v, "chunked") != NULL;
        else if (hdr(l, "Location", &v)) {
            strncpy(h.loc, v, sizeof h.loc - 1);
            h.loc[sizeof h.loc - 1] = 0;
        } else if (hdr(l, "Content-Range", &v) && (v = strchr(v, '/')) != NULL && v[1] != '*')
            h.total = strtoll(v + 1, NULL, 10);
        l = e + 2;
    }
    if (h.chunked)
        h.clen = -1;
    if (h.status >= 300 && h.status < 400 && h.loc[0]) {
        h.redirect = 1;
        h.state = H_DONE;
        return;
    }
    if (h.status == 206)
        h.ranges = 1;
    else if (h.status == 200) {
        h.ranges = 0;                   /* the Range was not taken: from the start */
        h.total = h.clen;
        h.skip = h.req_off;
    } else {
        h.state = H_ERROR;
        h.err = ZZAV_NETERR_HTTP;
        return;
    }
    h.state = H_BODY;
    h.cstate = C_SIZE;
    h.cn = 0;
    if (h.clen == 0) {
        h.state = H_DONE;
        h.ended_ok = 1;
    }
}

static void body(const uint8_t *d, int64_t n)
{
    int64_t k = n;

    if (h.skip) {
        k = n < h.skip ? n : h.skip;
        h.skip -= k;
        d += k;
        n -= k;
        k = n;
    }
    if (n <= 0)
        return;
    if (h.clen >= 0 && h.got + k > h.clen)
        k = h.clen - h.got;
    body_in(d, k);
    h.got += k;
    if (h.clen >= 0 && h.got >= h.clen) {
        h.state = H_DONE;
        h.ended_ok = 1;
    }
}

static void feed(const uint8_t *d, int n)
{
    while (n > 0) {
        if (h.state == H_HEAD) {
            if (h.hn < (int)sizeof h.head - 1) {
                h.head[h.hn++] = (char)*d;
                h.head[h.hn] = 0;
            }
            d++;
            n--;
            if (h.hn >= 4 && !memcmp(h.head + h.hn - 4, "\r\n\r\n", 4))
                parse_head();
            else if (h.hn >= (int)sizeof h.head - 1) {
                h.state = H_ERROR;
                h.err = ZZAV_NETERR_HTTP;
            }
        } else if (h.state == H_BODY && !h.chunked) {
            body(d, n);
            n = 0;
        } else if (h.state == H_BODY && h.cstate == C_DATA) {
            int64_t k = n < h.cleft ? n : h.cleft;

            body(d, k);
            h.cleft -= k;
            d += k;
            n -= (int)k;
            if (!h.cleft)
                h.cstate = C_CRLF;
        } else if (h.state == H_BODY) {         /* a chunk's size line, or the CRLF after its data */
            char c = (char)*d++;

            n--;
            if (h.cstate == C_CRLF) {
                if (c == '\n') {
                    h.cstate = C_SIZE;
                    h.cn = 0;
                }
            } else if (c == '\n') {
                h.cline[h.cn] = 0;
                h.cleft = strtoll(h.cline, NULL, 16);
                h.cn = 0;
                if (h.cleft <= 0) {
                    h.state = H_DONE;
                    h.ended_ok = 1;
                } else
                    h.cstate = C_DATA;
            } else if (h.cn < (int)sizeof h.cline - 1)
                h.cline[h.cn++] = c;
        } else
            break;                      /* done or broken: the rest is not ours */
    }
}

static void h_drop(struct tcp_pcb *pcb)
{
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_sent(pcb, NULL);
}

static err_t h_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct pbuf *q;

    (void)arg;
    if (!p || err != ERR_OK) {          /* the server closed */
        if (p)
            pbuf_free(p);
        if (h.state == H_BODY && h.clen < 0 && !h.chunked)
            h.ended_ok = 1;             /* no length: the close is the end */
        if (h.state != H_ERROR)
            h.state = H_DONE;
        h_drop(pcb);
        if (tcp_close(pcb) != ERR_OK) {
            tcp_abort(pcb);
            h.pcb = NULL;
            return ERR_ABRT;
        }
        h.pcb = NULL;
        return ERR_OK;
    }
    if (h.state == H_BODY && p->tot_len > room())
        return ERR_MEM;                 /* full: lwIP offers it again, the window stays shut */
    for (q = p; q; q = q->next)
        feed(q->payload, q->len);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void h_err(void *arg, err_t err)
{
    (void)arg;
    h.pcb = NULL;                       /* freed by lwIP */
    if (h.state != H_DONE) {
        h.state = H_ERROR;
        h.err = err == ERR_TIMEOUT ? ZZAV_NETERR_TIMEOUT : ZZAV_NETERR_CONNECT;
    }
}

static err_t h_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    char *p = h.req;

    (void)arg;
    if (err != ERR_OK) {
        h.state = H_ERROR;
        h.err = ZZAV_NETERR_CONNECT;
        return err;
    }
    p += sprintf(p, "GET %s HTTP/1.1\r\nHost: %s", h.path, h.host);
    if (h.port != 80)
        p += sprintf(p, ":%d", h.port);
    p += sprintf(p, "\r\nUser-Agent: z3660-core1\r\nAccept: */*\r\nRange: bytes=");
    p = i64s(p, h.req_off);
    p += sprintf(p, "-\r\nConnection: close\r\n\r\n");
    h.state = H_HEAD;
    if (tcp_write(pcb, h.req, (u16_t)(p - h.req), TCP_WRITE_FLAG_COPY) != ERR_OK) {
        h.state = H_ERROR;
        h.err = ZZAV_NETERR_CONNECT;
        return ERR_OK;
    }
    tcp_output(pcb);
    return ERR_OK;
}

static void h_stop(void)
{
    if (h.pcb) {
        h_drop(h.pcb);
        tcp_abort(h.pcb);
        h.pcb = NULL;
    }
}

/* a request from off: the buffer starts there, unless off is where it ends */
static void h_start(int64_t off)
{
    h_stop();
    if (off != hi)
        lo = hi = off;
    h.req_off = off;
    h.state = H_CONNECT;
    h.hn = 0;
    h.head[0] = 0;
    h.got = 0;
    h.skip = 0;
    h.ended_ok = h.redirect = h.err = 0;
    h.pcb = tcp_new();
    if (!h.pcb) {
        h.state = H_ERROR;
        h.err = ZZAV_NETERR_MEM;
        return;
    }
    tcp_arg(h.pcb, NULL);
    tcp_recv(h.pcb, h_recv);
    tcp_err(h.pcb, h_err);
    if (tcp_connect(h.pcb, &h.addr, (u16_t)h.port, h_connected) != ERR_OK) {
        h_drop(h.pcb);
        tcp_abort(h.pcb);
        h.pcb = NULL;
        h.state = H_ERROR;
        h.err = ZZAV_NETERR_CONNECT;
    }
}

/* waits until cond() or the time; 0, or a ZZAV_NETERR_ / -1 if CLOSEd */
static int wait_for(int (*cond)(void), int secs)
{
    const uint64_t t0 = pl_ticks();

    while (!cond()) {
        if (cancelled())
            return -1;
        if (pl_ticks() - t0 > (uint64_t)secs * pl_tick_hz)
            return ZZAV_NETERR_TIMEOUT;
        wait_idle();
    }
    return 0;
}

static int past_head(void) { return h.state >= H_BODY; }
static int have_addr(void) { return c1net_addr() != 0; }

static int dns_done, dns_ok;

static void dns_cb(const char *name, const ip_addr_t *a, void *arg)
{
    (void)name;
    (void)arg;
    if (a) {
        h.addr = *a;
        dns_ok = 1;
    }
    dns_done = 1;
}

static int dns_finished(void) { return dns_done; }

/* http://host[:port]/path into h; relative ("/path") keeps the host */
static int parse_url(const char *url)
{
    const char *p, *s;
    size_t n;

    if (url[0] == '/') {
        strncpy(h.path, url, sizeof h.path - 1);
        h.path[sizeof h.path - 1] = 0;
        return 0;
    }
    if (strncasecmp(url, "http://", 7))
        return ZZAV_NETERR_URL;
    p = url + 7;
    s = p + strcspn(p, ":/");
    n = (size_t)(s - p);
    if (!n || n >= sizeof h.host)
        return ZZAV_NETERR_URL;
    memcpy(h.host, p, n);
    h.host[n] = 0;
    h.port = 80;
    if (*s == ':') {
        h.port = atoi(s + 1);
        s += strcspn(s, "/");
    }
    if (h.port <= 0 || h.port > 65535)
        return ZZAV_NETERR_URL;
    strncpy(h.path, *s ? s : "/", sizeof h.path - 1);
    h.path[sizeof h.path - 1] = 0;
    if (ip4addr_aton(h.host, ip_2_ip4(&h.addr)))
        return 0;
    dns_done = dns_ok = 0;
    switch (dns_gethostbyname(h.host, &h.addr, dns_cb, NULL)) {
    case ERR_OK:
        return 0;
    case ERR_INPROGRESS:
        if (wait_for(dns_finished, 10))
            return ZZAV_NETERR_DNS;
        return dns_ok ? 0 : ZZAV_NETERR_DNS;
    default:
        return ZZAV_NETERR_DNS;
    }
}

/* ---- libavformat's reads and seeks ---- */

static int retries;

static int r_read(void *opaque, uint8_t *b, int size)
{
    uint64_t t0 = pl_ticks();
    int64_t n, off;

    (void)opaque;
    while (pos >= hi) {
        if (h.state == H_DONE || h.state == H_ERROR) {
            if (h.ended_ok || (h.total >= 0 && pos >= h.total))
                return AVERROR_EOF;
            if (!h.ranges || retries >= 3)      /* broke off: ask again from here, if we can */
                return h.state == H_ERROR ? AVERROR(EIO) : AVERROR_EOF;
            retries++;
            h_start(pos);
            t0 = pl_ticks();
        }
        if (cancelled())
            return AVERROR_EXIT;
        if (pl_ticks() - t0 > (uint64_t)WAIT_S * pl_tick_hz)
            return AVERROR(ETIMEDOUT);
        wait_idle();
    }
    retries = 0;
    off = pos % BUF_SIZE;
    n = hi - pos;
    if (n > size)
        n = size;
    if (n > BUF_SIZE - off)
        n = BUF_SIZE - off;
    memcpy(b, buf + off, (size_t)n);
    pos += n;
    return (int)n;
}

static int64_t r_seek(void *opaque, int64_t offset, int whence)
{
    int64_t t;

    (void)opaque;
    if (whence & AVSEEK_SIZE)
        return h.total >= 0 ? h.total : AVERROR(ENOSYS);
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET: t = offset; break;
    case SEEK_CUR: t = pos + offset; break;
    case SEEK_END:
        if (h.total < 0)
            return AVERROR(ENOSYS);
        t = h.total + offset;
        break;
    default:
        return AVERROR(EINVAL);
    }
    if (t < 0)
        return AVERROR(EINVAL);
    if (t >= lo && t <= hi) {
        pos = t;
        return t;
    }
    if (t > hi && t - hi <= NEAR_AHEAD && h.state >= H_CONNECT && h.state <= H_BODY) {
        pos = t;                        /* it is on its way */
        return t;
    }
    if (!h.ranges)
        return AVERROR(EPIPE);
    h_start(t);
    pos = t;
    return t;
}

static int interrupted(void *opaque)
{
    (void)opaque;
    return cancelled();
}

/* ---- the stream ---- */

static char cur_url[ZZAV_URL_MAX];
static AVFormatContext *fmt;
static AVIOContext *avio;
static AVPacket *pkt;
static int vs = -1, as = -1, src_eof;
static double vtb_ms, atb_ms;
static int64_t last_ms;

static void close_stream(void)
{
    h_stop();
    if (fmt)
        avformat_close_input(&fmt);
    if (avio) {
        av_freep(&avio->buffer);
        avio_context_free(&avio);
    }
    cur_url[0] = 0;
    vs = as = -1;
}

static void put_stream_words(void)
{
    AVStream *v = vs >= 0 ? fmt->streams[vs] : NULL, *a = as >= 0 ? fmt->streams[as] : NULL;

    put(ZZAV_W_VCODEC, v ? ZZAV_H264 : ZZAV_NONE);
    put(ZZAV_W_VEXTRA_LEN, 0);
    if (v) {
        AVCodecParameters *c = v->codecpar;

        if (c->extradata_size > 0 && c->extradata_size <= (int)ZZAV_EXTRA_MAX) {
            memcpy((void *)(pl_board + ZZAV_VEXTRA_OFF), c->extradata, (size_t)c->extradata_size);
            put(ZZAV_W_VEXTRA_LEN, (uint32_t)c->extradata_size);
        }
        put(ZZAV_W_VTB_NUM, (uint32_t)v->time_base.num);
        put(ZZAV_W_VTB_DEN, (uint32_t)v->time_base.den);
        put(ZZAV_W_VW, (uint32_t)c->width);
        put(ZZAV_W_VH, (uint32_t)c->height);
        put(ZZAV_W_SAR_NUM, (uint32_t)(c->sample_aspect_ratio.num > 0 ? c->sample_aspect_ratio.num : 0));
        put(ZZAV_W_SAR_DEN, (uint32_t)(c->sample_aspect_ratio.num > 0 ? c->sample_aspect_ratio.den : 0));
        vtb_ms = 1000.0 * v->time_base.num / v->time_base.den;
    }
    put(ZZAV_W_ACODEC, a ? ZZAV_AAC : ZZAV_NONE);
    put(ZZAV_W_AEXTRA_LEN, 0);
    if (a) {
        AVCodecParameters *c = a->codecpar;

        if (c->extradata_size > 0 && c->extradata_size <= (int)ZZAV_EXTRA_MAX) {
            memcpy((void *)(pl_board + ZZAV_AEXTRA_OFF), c->extradata, (size_t)c->extradata_size);
            put(ZZAV_W_AEXTRA_LEN, (uint32_t)c->extradata_size);
        }
        put(ZZAV_W_ATB_NUM, (uint32_t)a->time_base.num);
        put(ZZAV_W_ATB_DEN, (uint32_t)a->time_base.den);
        put(ZZAV_W_ARATE, (uint32_t)c->sample_rate);
        put(ZZAV_W_ACH, (uint32_t)c->channels);
        atb_ms = 1000.0 * a->time_base.num / a->time_base.den;
    }
    put(ZZAV_W_DUR_MS, fmt->duration > 0 ? (uint32_t)(fmt->duration / 1000) : 0);
    put(ZZAV_W_T0_MS, fmt->start_time != AV_NOPTS_VALUE && fmt->start_time > 0 ?
        (uint32_t)(fmt->start_time / 1000) : 0);
    put(ZZAV_W_SEEKABLE, (uint32_t)h.ranges);
    put(ZZAV_W_EOF, 0);
}

/* ms after the stream's start; again: also to 0 (the last play left it at its end).
   < 0 if it cannot (a live stream: it plays from where it is) */
static int seek_to(uint32_t ms, int again)
{
    int64_t ts = (int64_t)ms * 1000;
    int r = 0;

    if (fmt->start_time != AV_NOPTS_VALUE)
        ts += fmt->start_time;
    if (ms || again)
        r = av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
    src_eof = 0;
    last_ms = 0;
    return r;
}

static int open_stream(const char *url)
{
    int r, i, redirects = 0;

    if ((r = parse_url(url)) != 0)
        return r;
    for (;;) {
        h.total = -1;
        h.ranges = 0;
        pos = 0;
        h_start(0);
        if ((r = wait_for(past_head, WAIT_S)) != 0)
            return r < 0 ? r : h.state == H_ERROR ? h.err : r;
        if (h.state == H_ERROR)
            return h.err;
        if (!h.redirect)
            break;
        if (++redirects > REDIRECTS)
            return ZZAV_NETERR_HTTP;
        if ((r = parse_url(h.loc)) != 0)
            return r;
    }
    avio = avio_alloc_context(av_malloc(AVIO_BUF), AVIO_BUF, 0, NULL, r_read, NULL, r_seek);
    fmt = avformat_alloc_context();
    if (!avio || !avio->buffer || !fmt)
        return ZZAV_NETERR_MEM;
    avio->seekable = h.ranges ? AVIO_SEEKABLE_NORMAL : 0;
    fmt->pb = avio;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    fmt->interrupt_callback.callback = interrupted;
    fmt->probesize = 2 << 20;
    fmt->max_analyze_duration = 3 * AV_TIME_BASE;
    if (avformat_open_input(&fmt, url, NULL, NULL) < 0) {
        fmt = NULL;                     /* freed by it */
        return cancelled() ? -1 : ZZAV_NETERR_FORMAT;
    }
    if (avformat_find_stream_info(fmt, NULL) < 0 && cancelled())
        return -1;
    vs = as = -1;
    for (i = 0; i < (int)fmt->nb_streams; i++) {
        AVCodecParameters *c = fmt->streams[i]->codecpar;

        if (vs < 0 && c->codec_id == AV_CODEC_ID_H264 && c->width > 0)
            vs = i;
        else if (as < 0 && c->codec_id == AV_CODEC_ID_AAC && c->sample_rate > 0 && c->channels > 0)
            as = i;
    }
    for (i = 0; i < (int)fmt->nb_streams; i++)
        fmt->streams[i]->discard = i == vs || i == as ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    if (vs < 0 && as < 0)
        return ZZAV_NETERR_STREAMS;
    return 0;
}

void pl_open_url(void)
{
    static char url[ZZAV_URL_MAX];
    int r;

    blk = (volatile uint32_t *)(pl_board + ZZAV_OFF);
    put(ZZAV_W_STATE, ZZAV_OPENING);
    put(ZZAV_W_ERR, 0);
    put(ZZAV_W_HTTP, 0);
    for (r = ZZAV_W_BUF_MS; r <= ZZAV_W_NET_KB; r++)
        put(r, 0);
    memcpy(url, (const void *)(pl_board + ZZAV_URL_OFF), sizeof url);
    url[sizeof url - 1] = 0;
    if (!pkt)
        pkt = av_packet_alloc();
    if (!buf)
        buf = malloc(BUF_SIZE);
    if (!buf || !pkt) {
        r = ZZAV_NETERR_MEM;
        goto fail;
    }
    if (fmt && !strcmp(url, cur_url) && seek_to(get(ZZAV_W_START_MS), 1) >= 0) {
        put_stream_words();                     /* the same stream: only a seek */
        put(ZZAV_W_STATE, ZZAV_READY);
        return;
    }
    close_stream();
    if ((r = wait_for(have_addr, 15)) != 0) {
        r = r < 0 ? r : ZZAV_NETERR_NONET;
        goto fail;
    }
    kb_total = rate_bytes = 0;
    rate_t = pl_ticks();
    if ((r = open_stream(url)) != 0)
        goto fail;
    strcpy(cur_url, url);
    seek_to(get(ZZAV_W_START_MS), 0);
    put_stream_words();
    put(ZZAV_W_STATE, ZZAV_READY);
    return;
fail:
    close_stream();
    if (r < 0) {                        /* CLOSEd while opening */
        put(ZZAV_W_STATE, ZZAV_IDLE);
        return;
    }
    put(ZZAV_W_ERR, (uint32_t)r);
    put(ZZAV_W_STATE, ZZAV_ERROR);
}

/* the player's loop: packets into its queues while they take them, never waiting */
void pl_net_pull(void)
{
    uint64_t t = pl_ticks();
    int n = 0, r;

    if (t - rate_t >= pl_tick_hz) {     /* the speed, once a second */
        put(ZZAV_W_NET_KBS, (uint32_t)((uint64_t)rate_bytes * pl_tick_hz / (t - rate_t) / 1024));
        kb_total += rate_bytes >> 10;
        put(ZZAV_W_NET_KB, kb_total);
        rate_bytes &= 1023;
        rate_t = t;
    }
    while (!src_eof && player_room() && n++ < 8) {
        if (hi - pos < LOW_WATER && h.state >= H_CONNECT && h.state <= H_BODY)
            break;                      /* more is coming: no read that would wait */
        r = av_read_frame(fmt, pkt);
        if (r == AVERROR(EAGAIN))
            break;
        if (r < 0) {
            if (r == AVERROR_EXIT)
                break;
            src_eof = 1;
            player_end();
            break;
        }
        if (pkt->stream_index == vs || pkt->stream_index == as) {
            AVPacket *p = av_packet_alloc();
            const int video = pkt->stream_index == vs;

            if (pkt->pts != AV_NOPTS_VALUE)
                last_ms = (int64_t)(pkt->pts * (video ? vtb_ms : atb_ms));
            if (p) {
                av_packet_move_ref(p, pkt);
                player_queue(video, p);
            }
        }
        av_packet_unref(pkt);
    }
    {
        int64_t b = last_ms - (int64_t)get(ZZAV_W_CLOCK_MS);

        put(ZZAV_W_BUF_MS, b > 0 ? (uint32_t)b : 0);
    }
}
