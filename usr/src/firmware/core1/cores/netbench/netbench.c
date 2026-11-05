/*
 * netbench.c -- core 1's own network on its own (lwIP over core 0's rings,
 * c1netif.c), in h264bench's harness: the first step towards core 1 fetching
 * video streams itself.  A job (tools/c1net.c on AMIX) is a line of text; the
 * answer is a line of text written back over it at HB_DATA_OFF:
 *   info                       address, MAC, the rings' counters
 *   get a.b.c.d port /path     HTTP/1.0 GET, the body counted and dropped: speed
 * Between jobs the harness keeps the network going (hb_service): DHCP, ARP,
 * ping.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/tcp.h"
#include "lwip/ip4_addr.h"
#include "bench.h"
#include "zzav.h"
#include "player.h"
#include "c1netif.h"

#define RTG_BASE        0x18000000UL
#define ANSWER_MAX      1024
#define GET_TIMEOUT_S   120

const char hb_core_name[16] = "netbench";

void hb_service(void)
{
    c1net_service();
}

/* the harness's player and FFmpeg's log level: not in this core */
int player_run(void)
{
    ((volatile uint32_t *)(RTG_BASE + ZZAV_OFF))[ZZAV_W_CMD] = 0;
    ((volatile uint32_t *)(RTG_BASE + ZZAV_OFF))[ZZAV_W_STATE] = __builtin_bswap32(ZZAV_ERROR);
    return -1;
}

void av_log_set_level(int level)
{
    (void)level;
}

/* the GET under way */
static struct get {
    struct tcp_pcb *pcb;
    const char *path, *host;
    uint64_t t0, t_first, t_end;
    uint32_t bytes;             /* after the header */
    int done, err;
    char head[160];             /* the status line */
    int hn, in_body, crlf;      /* header parsing: bytes of head, body reached, \r\n\r\n matched */
} g;

static err_t get_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct pbuf *q;

    (void)arg;
    if (!p || err != ERR_OK) {          /* the server closed: the end */
        if (p)
            pbuf_free(p);
        g.t_end = pl_ticks();
        g.done = 1;
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_close(pcb);
        g.pcb = NULL;
        return ERR_OK;
    }
    if (!g.t_first)
        g.t_first = pl_ticks();
    for (q = p; q; q = q->next) {
        const char *c = q->payload;
        u16_t i = 0;

        while (!g.in_body && i < q->len) {      /* the header: keep the status line */
            const char ch = c[i++];

            if (g.hn < (int)sizeof g.head - 1 && !strchr(g.head, '\r'))
                g.head[g.hn++] = ch;
            g.crlf = (ch == (g.crlf & 1 ? '\n' : '\r')) ? g.crlf + 1 : ch == '\r';
            if (g.crlf == 4)
                g.in_body = 1;
        }
        g.bytes += q->len - i;
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void get_error(void *arg, err_t err)
{
    (void)arg;
    g.pcb = NULL;                       /* freed by lwIP */
    g.err = err;
    g.t_end = pl_ticks();
    g.done = 1;
}

static err_t get_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    char req[300];
    int n;

    (void)arg;
    if (err != ERR_OK) {
        get_error(NULL, err);
        return err;
    }
    n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: z3660-core1\r\n\r\n", g.path, g.host);
    tcp_write(pcb, req, (u16_t)n, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    return ERR_OK;
}

static int get(char *answer, const char *host, int port, const char *path, void (*progress)(uint32_t))
{
    ip4_addr_t a;
    uint64_t ms, first_ms;
    char *e;

    if (!c1net_addr()) {
        c1net_info(answer, ANSWER_MAX);
        return -1;
    }
    if (!ip4addr_aton(host, &a)) {
        snprintf(answer, ANSWER_MAX, "not an address: %s", host);
        return -1;
    }
    memset(&g, 0, sizeof g);
    g.host = host;
    g.path = path;
    g.pcb = tcp_new();
    if (!g.pcb) {
        snprintf(answer, ANSWER_MAX, "tcp_new failed");
        return -1;
    }
    tcp_recv(g.pcb, get_recv);
    tcp_err(g.pcb, get_error);
    g.t0 = pl_ticks();
    if (tcp_connect(g.pcb, &a, (u16_t)port, get_connected) != ERR_OK) {
        tcp_abort(g.pcb);
        snprintf(answer, ANSWER_MAX, "tcp_connect failed");
        return -1;
    }
    while (!g.done) {
        c1net_service();
        if (progress)
            progress(g.bytes >> 10);
        if (pl_ticks() - g.t0 > (uint64_t)GET_TIMEOUT_S * pl_tick_hz) {
            if (g.pcb)
                tcp_abort(g.pcb);
            g.err = ERR_TIMEOUT;
            g.t_end = pl_ticks();
            break;
        }
    }
    ms = (g.t_end - g.t0) / (pl_tick_hz / 1000);
    first_ms = g.t_first ? (g.t_first - g.t0) / (pl_tick_hz / 1000) : 0;
    if ((e = strchr(g.head, '\r')) != NULL)
        *e = 0;
    snprintf(answer, ANSWER_MAX, "%lu bytes in %lu ms = %lu KB/s, first data after %lu ms, %s%s%s",
             (unsigned long)g.bytes, (unsigned long)ms,
             (unsigned long)(ms ? (uint64_t)g.bytes * 1000 / 1024 / ms : 0), (unsigned long)first_ms,
             g.head[0] ? g.head : "no answer", g.err ? ", error " : "",
             g.err == ERR_TIMEOUT ? "timeout" : g.err == ERR_RST ? "reset" : g.err == ERR_ABRT ? "aborted" :
             g.err == ERR_CONN ? "not connected" : g.err ? "other" : "");
    return g.err ? -1 : (int)(g.bytes >> 10);
}

/* a job: the command line in data[0..size); the answer over it at HB_DATA_OFF */
int hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
              void (*progress)(uint32_t frames), struct hb_result *r)
{
    static char cmd[512], answer[ANSWER_MAX];
    char host[64], path[300];
    int port, ret = 0;
    uint64_t t0 = clock();

    (void)flags;
    memset(r, 0, sizeof *r);
    if (size >= sizeof cmd)
        size = sizeof cmd - 1;
    memcpy(cmd, data, size);
    cmd[size] = 0;
    cmd[strcspn(cmd, "\r\n")] = 0;
    if (!strcmp(cmd, "info"))
        c1net_info(answer, sizeof answer);
    else if (sscanf(cmd, "get %63s %d %299s", host, &port, path) == 3)
        ret = get(answer, host, port, path, progress);
    else {
        snprintf(answer, sizeof answer, "commands: info | get a.b.c.d port /path");
        ret = -1;
    }
    r->frames = ret > 0 ? (uint32_t)ret : 0;    /* KB, for armbench */
    r->checksum = c1net_addr();
    r->errcode = ret < 0 ? ret : 0;
    r->ticks = clock() - t0;
    strcpy((char *)(RTG_BASE + HB_DATA_OFF), answer);
    return ret < 0 ? -1 : 0;
}
