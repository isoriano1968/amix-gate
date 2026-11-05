/*
 * player.c -- core 1's side of zzav.h: decode the packets the 68k puts into
 * the ring (H.264 with FFmpeg, AAC with FFmpeg), write the pictures into the
 * RTG overlay at their time and the sound into the board's HDMI ring.
 *
 * The clock is the sound: the audio DMA plays the ring round and round on
 * its own, so the media time is the time of the sample the DMA is at.  Core
 * 1 keeps the ring ZZAV_LEAD_MS ahead of it -- also while a frame decodes,
 * from FFmpeg's draw_horiz_band (a 720p frame can take longer than the
 * ring's 160 ms).  Without sound the clock is the ARM's timer.
 *
 * Platform (arm_main.c on core 1, tests/zzav_host.c on the PC): pl_board,
 * pl_ticks/pl_tick_hz and pl_audio_pos.
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libavutil/channel_layout.h>
#include "zzav.h"
#include "bench.h"
#include "player.h"

#define RATE                48000
#define FRAME_BYTES         4               /* 16-bit stereo */
#define PERIOD              3840            /* bytes, 20 ms: the firmware's */
#define LEAD                (PERIOD * 6)    /* written ahead of the DMA: 120 ms */
#define LEAD_MIN            (PERIOD * 2)    /* below this, silence goes in */
#define PCM_FRAMES          (RATE * 2)      /* decoded sound held: 2 s */
/* packets held on core 1 (from the heap): the amix core, with its 242 MiB,
   builds with more (cores/amix/Makefile) to ride out the network's dips */
#ifndef VQ_BYTES_MAX
#define VQ_BYTES_MAX        (6 << 20)       /* video */
#endif
#ifndef AQ_MAX
#define AQ_MAX              4000            /* sound, packets */
#endif

static volatile uint32_t *blk;
static uint32_t get(int w) { return __builtin_bswap32(blk[w]); }
static void put(int w, uint32_t v) { blk[w] = __builtin_bswap32(v); }

static uint64_t now_ticks(void) { return pl_ticks(); }
static double secs(uint64_t t) { return (double)t / (double)pl_tick_hz; }

/* packet queues */
struct pkt { struct pkt *next; AVPacket *p; };
struct pq { struct pkt *head, *tail; long n, bytes; };
static struct pq vq, aq;

static void pq_put(struct pq *q, AVPacket *p)
{
    struct pkt *e = malloc(sizeof *e);

    if (!e) {
        av_packet_free(&p);
        return;
    }
    e->p = p;
    e->next = NULL;
    if (q->tail)
        q->tail->next = e;
    else
        q->head = e;
    q->tail = e;
    q->n++;
    q->bytes += p->size;
}

static AVPacket *pq_get(struct pq *q)
{
    struct pkt *e = q->head;
    AVPacket *p;

    if (!e)
        return NULL;
    q->head = e->next;
    if (!q->head)
        q->tail = NULL;
    q->n--;
    q->bytes -= e->p->size;
    p = e->p;
    free(e);
    return p;
}

static void pq_flush(struct pq *q)
{
    AVPacket *p;

    while ((p = pq_get(q)) != NULL)
        av_packet_free(&p);
}

static AVCodecContext *vctx, *actx;
static AVFrame *vframe, *aframe;
static double vtb, atb;
static uint32_t rd, ring_size;
static int eof_in, vflushed, vdone, aflushed, adone;
static int src_net, running;            /* packets from netsrc.c, not the ring; in player_run */

/* ---- the ring ---- */

static uint32_t be32p(const volatile uint8_t *p)
{
    return __builtin_bswap32(*(const volatile uint32_t *)p);
}

/* take what the 68k has put into the ring, as far as the queues allow */
static void pull_ring(void)
{
    const volatile uint8_t *ring = pl_board + ZZAV_RING_OFF;
    uint32_t wr = get(ZZAV_W_WR);
    int n = 0;

    while (rd != wr && vq.bytes < VQ_BYTES_MAX && aq.n < AQ_MAX && n++ < 64) {
        uint32_t w0 = be32p(ring + rd), size = be32p(ring + rd + 4);
        uint64_t pts = (uint64_t)be32p(ring + rd + 8) << 32 | be32p(ring + rd + 12);
        int type = (int)(w0 >> 24);
        AVPacket *p;

        if (type == ZZAV_PKT_WRAP) {
            rd = 0;
            continue;
        }
        if (size > ring_size || rd + ZZAV_PKT_HDR + size > ring_size) {
            put(ZZAV_W_ERR, 0x100);     /* a broken record: stop reading */
            put(ZZAV_W_STATE, ZZAV_ERROR);
            rd = wr;
            break;
        }
        if ((type == ZZAV_PKT_VIDEO && vctx) || (type == ZZAV_PKT_AUDIO && actx)) {
            p = av_packet_alloc();
            if (p && av_new_packet(p, (int)size) == 0) {
                memcpy(p->data, (const void *)(ring + rd + ZZAV_PKT_HDR), size);
                p->pts = pts == ZZAV_NOPTS ? AV_NOPTS_VALUE : (int64_t)pts;
                p->dts = AV_NOPTS_VALUE;
                if (w0 & ZZAV_PKT_KEY)
                    p->flags |= AV_PKT_FLAG_KEY;
                pq_put(type == ZZAV_PKT_VIDEO ? &vq : &aq, p);
                put(type == ZZAV_PKT_VIDEO ? ZZAV_W_VPKTS : ZZAV_W_APKTS,
                    get(type == ZZAV_PKT_VIDEO ? ZZAV_W_VPKTS : ZZAV_W_APKTS) + 1);
            } else
                av_packet_free(&p);
        }
        rd += (ZZAV_PKT_HDR + size + 3) & ~3UL;
        if (rd >= ring_size)
            rd = 0;
        put(ZZAV_W_RD, rd);
    }
    if (rd == wr && get(ZZAV_W_EOF))
        eof_in = 1;
}

/* ---- packets from core 1's own stream (cores/amix/netsrc.c) ---- */

int player_room(void)
{
    return vq.bytes < VQ_BYTES_MAX && aq.n < AQ_MAX;
}

/* a packet in the stream's time base: the player's from now on */
void player_queue(int video, AVPacket *p)
{
    if ((video && !vctx) || (!video && !actx)) {
        av_packet_free(&p);
        return;
    }
    pq_put(video ? &vq : &aq, p);
    put(video ? ZZAV_W_VPKTS : ZZAV_W_APKTS, get(video ? ZZAV_W_VPKTS : ZZAV_W_APKTS) + 1);
}

void player_end(void)
{
    eof_in = 1;
}

static int audio_on;                    /* below, with the sound */
static void audio_service(void);

/* while netsrc waits for the network inside a read: the sound goes on */
void player_keep(void)
{
    if (running && audio_on)
        audio_service();
}

/* ---- sound ---- */

static uint32_t asamples;
static int16_t *pcm;                    /* decoded, resampled to 48 kHz stereo */
static long pcm_head, pcm_n;
static double pcm_t0;                   /* media time of the sample at pcm_head */
static int pcm_valid;
static double rs_pos, rs_step;          /* the resampler */
static float rs_prev[2];

/* the ring as written: segments of media (or silence) by absolute byte count */
#define NSEG 64
static struct seg { uint64_t abs; uint32_t len; double t0; int media; } seg[NSEG];
static int seg_n, seg_first;
static uint64_t wr_abs, dma_abs;
static uint32_t dma_last;
static uint64_t t_pass, idle_ticks;      /* see the main loop */
static double clock_hold;               /* the clock while only silence plays */
static int audio_on, volume = 256, media_started;

static void seg_add(uint64_t abs, uint32_t len, double t0, int media)
{
    struct seg *s;

    if (seg_n) {                        /* continues the last one: extend it */
        s = &seg[(seg_first + seg_n - 1) % NSEG];
        if (s->abs + s->len == abs && s->media == media &&
            (!media || (s->t0 + (double)s->len / (RATE * FRAME_BYTES) - t0 < 0.001 &&
                        s->t0 + (double)s->len / (RATE * FRAME_BYTES) - t0 > -0.001))) {
            s->len += len;
            return;
        }
    }
    if (seg_n == NSEG) {                /* the oldest has long played */
        seg_first = (seg_first + 1) % NSEG;
        seg_n--;
    }
    s = &seg[(seg_first + seg_n) % NSEG];
    s->abs = abs;
    s->len = len;
    s->t0 = t0;
    s->media = media;
    seg_n++;
}

static void dma_update(void)
{
    uint32_t raw = pl_audio_pos(), pos = raw & ~3UL;

    put(ZZAV_W_DMA_RAW, raw);
    if (pos >= ZZAV_AUDIO_SIZE)
        pos = 0;
    if (pos < dma_last) {
        dma_abs += ZZAV_AUDIO_SIZE - dma_last + pos;
        put(ZZAV_W_DMA_JUMPS, get(ZZAV_W_DMA_JUMPS) + 1);
    }
    else
        dma_abs += pos - dma_last;
    dma_last = pos;
    while (seg_n > 1 && seg[seg_first].abs + seg[seg_first].len <= dma_abs) {
        seg_first = (seg_first + 1) % NSEG;
        seg_n--;
    }
}

/* the media time the DMA plays now; -1 before any sound of the file */
static double audio_clock(void)
{
    int i;

    for (i = 0; i < seg_n; i++) {
        struct seg *s = &seg[(seg_first + i) % NSEG];

        if (dma_abs >= s->abs && dma_abs < s->abs + s->len) {
            if (s->media)
                return clock_hold = s->t0 + (double)(dma_abs - s->abs) / (RATE * FRAME_BYTES);
            return clock_hold;
        }
    }
    return clock_hold;
}

static void ring_write(const int16_t *src, uint32_t bytes)
{
    volatile uint8_t *ring = pl_board + ZZAV_AUDIO_OFF;
    uint32_t off = (uint32_t)(wr_abs % ZZAV_AUDIO_SIZE), first = ZZAV_AUDIO_SIZE - off;

    if (first > bytes)
        first = bytes;
    if (src) {
        memcpy((void *)(ring + off), src, first);
        memcpy((void *)ring, (const uint8_t *)src + first, bytes - first);
    } else {
        memset((void *)(ring + off), 0, first);
        memset((void *)ring, 0, bytes - first);
    }
    wr_abs += bytes;
}

/* keep the ring LEAD ahead of the DMA, with sound or with silence */
static uint64_t chk_t, chk_abs;
static int audio_bad;

/* the DMA position must move at 192000 bytes a second by the ARM's timer;
   if it does not, the sound is given up and the picture runs on the timer */
static void dma_check(void)
{
    uint64_t t = now_ticks();

    if (!chk_t) {
        chk_t = t;
        chk_abs = dma_abs;
        return;
    }
    if (t - chk_t >= pl_tick_hz) {
        double rate = (double)(dma_abs - chk_abs) / secs(t - chk_t);

        if (rate > 192000 * 1.2 || rate < 192000 * 0.8) {
            audio_bad = 1;
            put(ZZAV_W_ERR, 0x200 | (uint32_t)(rate / 1000));  /* KB/s it seemed to move at */
        }
        chk_t = t;
        chk_abs = dma_abs;
    }
}

static void audio_service(void)
{
    int64_t lead;
    int paused = get(ZZAV_W_PAUSE) != 0;

    dma_update();
    dma_check();
    lead = (int64_t)(wr_abs - dma_abs);
    if (lead < 0) {                     /* the DMA overtook us: start again just ahead of it */
        wr_abs = dma_abs + PERIOD;
        lead = PERIOD;
        if (media_started)
            put(ZZAV_W_UNDERRUNS, get(ZZAV_W_UNDERRUNS) + 1);
    }
    if (lead > LEAD - PERIOD / 2)       /* top up in pieces of 10 ms or more */
        goto report;
    while (lead < LEAD) {
        uint32_t want = (uint32_t)(LEAD - lead), n;

        if (!paused && pcm_n > 0) {
            long frames = want / FRAME_BYTES, first;

            if (frames > pcm_n)
                frames = pcm_n;
            first = PCM_FRAMES - pcm_head;
            if (first > frames)
                first = frames;
            seg_add(wr_abs, (uint32_t)(frames * FRAME_BYTES), pcm_t0, 1);
            media_started = 1;
            ring_write(pcm + pcm_head * 2, (uint32_t)(first * FRAME_BYTES));
            if (frames > first)
                ring_write(pcm, (uint32_t)((frames - first) * FRAME_BYTES));
            pcm_head = (pcm_head + frames) % PCM_FRAMES;
            pcm_n -= frames;
            pcm_t0 += (double)frames / RATE;
            n = (uint32_t)(frames * FRAME_BYTES);
        } else if (lead < LEAD_MIN) {   /* nothing decoded (or paused): silence */
            n = LEAD_MIN - (uint32_t)lead;
            seg_add(wr_abs, n, pcm_valid ? pcm_t0 : clock_hold, 0);
            ring_write(NULL, n);
            if (!paused && audio_on && !adone && media_started)
                put(ZZAV_W_UNDERRUNS, get(ZZAV_W_UNDERRUNS) + 1);
        } else
            break;
        lead += n;
    }
report:
    put(ZZAV_W_ALEAD_MS, (uint32_t)(lead * 1000 / (RATE * FRAME_BYTES)));
}

static int16_t s16(float v)
{
    v = v * 32767.0f * (float)volume / 256.0f;
    if (v > 32767.0f)
        return 32767;
    if (v < -32768.0f)
        return -32768;
    return (int16_t)v;
}

/* resample one decoded frame into pcm (linear: good enough against AAC's own filtering) */
/* one sample of channel ch, as a float, from any of the decoders' formats */
static float smp(const AVFrame *f, int ch, int i)
{
    switch (f->format) {
    case AV_SAMPLE_FMT_FLTP: return ((const float *)f->extended_data[ch])[i];
    case AV_SAMPLE_FMT_FLT:  return ((const float *)f->extended_data[0])[i * f->channels + ch];
    case AV_SAMPLE_FMT_S16P: return ((const int16_t *)f->extended_data[ch])[i] * (1.0f / 32768);
    case AV_SAMPLE_FMT_S16:  return ((const int16_t *)f->extended_data[0])[i * f->channels + ch] * (1.0f / 32768);
    case AV_SAMPLE_FMT_S32P: return ((const int32_t *)f->extended_data[ch])[i] * (1.0f / 2147483648.0f);
    case AV_SAMPLE_FMT_S32:  return ((const int32_t *)f->extended_data[0])[i * f->channels + ch] * (1.0f / 2147483648.0f);
    default: return 0;
    }
}

/* the frame down to stereo: front left/right, the centre and the surrounds at
   -3 dB to their side, no LFE, scaled so the sum cannot clip (a film's 5.1:
   the dialogue is in the centre) */
static float *mixl, *mixr;
static int mix_n;

static int downmix(const AVFrame *f, const float **l, const float **r)
{
    float wl[16], wr[16], sl = 0, sr = 0, norm;
    const int ch = f->channels < 16 ? f->channels : 16, n = f->nb_samples;
    uint64_t layout = f->channel_layout ? f->channel_layout : (uint64_t)av_get_default_channel_layout(ch);
    int c, i;

    if (f->format == AV_SAMPLE_FMT_FLTP && ch <= 2) {   /* the common case: as it is */
        *l = (const float *)f->extended_data[0];
        *r = ch > 1 ? (const float *)f->extended_data[1] : *l;
        return 0;
    }
    if (n > mix_n) {
        free(mixl);
        free(mixr);
        mixl = malloc(n * sizeof *mixl);
        mixr = malloc(n * sizeof *mixr);
        mix_n = mixl && mixr ? n : 0;
        if (!mix_n)
            return -1;
    }
    for (c = 0; c < ch; c++) {
        uint64_t b = av_channel_layout_extract_channel(layout, c);

        wl[c] = wr[c] = 0;
        if (ch == 1)
            wl[c] = wr[c] = 1;
        else if (b == AV_CH_FRONT_LEFT)
            wl[c] = 1;
        else if (b == AV_CH_FRONT_RIGHT)
            wr[c] = 1;
        else if (b == AV_CH_LOW_FREQUENCY)
            ;
        else if (b == AV_CH_SIDE_LEFT || b == AV_CH_BACK_LEFT || b == AV_CH_FRONT_LEFT_OF_CENTER)
            wl[c] = 0.7071f;
        else if (b == AV_CH_SIDE_RIGHT || b == AV_CH_BACK_RIGHT || b == AV_CH_FRONT_RIGHT_OF_CENTER)
            wr[c] = 0.7071f;
        else
            wl[c] = wr[c] = 0.7071f;    /* the centres */
        sl += wl[c];
        sr += wr[c];
    }
    norm = sl > sr ? sl : sr;
    norm = norm > 1 ? 1 / norm : 1;
    for (i = 0; i < n; i++) {
        float a = 0, b = 0;

        for (c = 0; c < ch; c++) {
            const float v = smp(f, c, i);

            a += wl[c] * v;
            b += wr[c] * v;
        }
        mixl[i] = a * norm;
        mixr[i] = b * norm;
    }
    *l = mixl;
    *r = mixr;
    return 0;
}

static void audio_add(AVFrame *f)
{
    const float *l, *r;
    float lv[2];
    int n = f->nb_samples, i0;

    if (n <= 0 || f->channels <= 0 || downmix(f, &l, &r) < 0)
        return;
    if (!pcm_valid) {
        pcm_valid = 1;
        pcm_t0 = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp * atb : 0;
        clock_hold = pcm_t0;
    }
    while (rs_pos < n - 1 && pcm_n < PCM_FRAMES) {
        long t = (pcm_head + pcm_n) % PCM_FRAMES;
        float fr;

        i0 = (int)(rs_pos + 1.0) - 1;   /* floor for rs_pos >= -1 */
        fr = (float)(rs_pos - i0);
        if (i0 < 0) {
            lv[0] = rs_prev[0] + (l[0] - rs_prev[0]) * fr;
            lv[1] = rs_prev[1] + (r[0] - rs_prev[1]) * fr;
        } else {
            lv[0] = l[i0] + (l[i0 + 1] - l[i0]) * fr;
            lv[1] = r[i0] + (r[i0 + 1] - r[i0]) * fr;
        }
        pcm[t * 2] = s16(lv[0]);
        asamples++;
        pcm[t * 2 + 1] = s16(lv[1]);
        pcm_n++;
        rs_pos += rs_step;
    }
    rs_pos -= n;
    rs_prev[0] = l[n - 1];
    rs_prev[1] = r[n - 1];
}

/* decode sound until 1 s is held (or nothing is there to decode) */
static void audio_decode(void)
{
    while (actx && !adone && pcm_n < RATE) {
        int r = avcodec_receive_frame(actx, aframe);

        if (r == 0) {
            put(ZZAV_W_AFRAMES, get(ZZAV_W_AFRAMES) + 1);
            audio_add(aframe);
            av_frame_unref(aframe);
            continue;
        }
        if (r == AVERROR_EOF) {
            adone = 1;
            break;
        }
        if (r != AVERROR(EAGAIN)) {
            put(ZZAV_W_AERR, (uint32_t)r);
            break;
        }
        {
            AVPacket *p = pq_get(&aq);

            if (p) {
                int e = avcodec_send_packet(actx, p);

                if (e < 0)
                    put(ZZAV_W_AERR, (uint32_t)e);
                av_packet_free(&p);
            } else if (eof_in && !aflushed) {
                avcodec_send_packet(actx, NULL);
                aflushed = 1;
            } else
                break;
        }
    }
}

/* called by the H.264 decoder as rows of a picture are done */
static uint64_t last_service;

static volatile uint32_t *c1ld;         /* below, with the overlay */
static int dbuf;
static void flip(void);

/* ---- when a picture reaches the screen ----
 * Frames are decoded ahead into a queue (NQ), the next one is drawn into the
 * overlay's back buffer as soon as that is free (right after the last flip),
 * and the flip is asked for half a refresh before the frame's time: core 0
 * makes it at the next vblank, the one nearest the time stamp.  The check runs
 * in the main loop and in the decoder's band callback, so a long decode does
 * not push a frame off its refresh: 25 fps at 60 Hz then shows 2,3,2,3
 * refreshes, as a PC's player does, not 2,3,3,2,4.
 * A flip asked for the buffer already shown changes nothing but is still taken
 * (cleared) at the vblank: that measures the refresh period. */
#define NQ 4
static AVFrame *fq[NQ];                 /* decoded, not drawn yet; [0] next */
static double fq_pts[NQ];
static int nq;
static int ready, ready_key;            /* the back buffer holds the next frame */
static double ready_pts;
static int flip_req, paused_now, new_lag;
static uint64_t t_req, t_take;
static double refresh = 1.0 / 60, lag_last;
static uint32_t cad[5];                 /* frames by refreshes on the screen: 1, 2, 3, 4+ */
static int clock_started;
static double t_start_media;
static uint64_t t_start, t_pause, paused_ticks;

static double clock_now(uint64_t t)
{
    if (audio_on)
        return pcm_valid || seg_n ? audio_clock() : -1;
    return clock_started ? t_start_media + secs((t_pause ? t_pause : t) - t_start - paused_ticks) : -1;
}

/* the flip asked for has been made (core 0 cleared the word at a vblank) */
static void flip_poll(uint64_t t)
{
    int n;

    if (!flip_req || (dbuf && c1ld[C1LD_W_FLIP] != 0 && t - t_req < pl_tick_hz / 10))
        return;
    if (t_take && !paused_now) {            /* how long the last frame stayed */
        n = (int)(secs(t - t_take) / refresh + 0.5);
        cad[n < 1 ? 1 : n > 4 ? 4 : n]++;
    }
    t_take = paused_now ? 0 : t;
    flip_req = 0;
}

/* the frame in the back buffer is due: ask for the flip */
static void flip_due(uint64_t t)
{
    double clk;

    if (!ready || flip_req || paused_now)
        return;
    clk = clock_now(t);
    if (clk < 0 || clk < ready_pts - refresh / 2)
        return;
    flip();
    flip_req = 1;
    t_req = t;
    ready = 0;
    lag_last = clk - ready_pts;
    new_lag = 1;
    put(ZZAV_W_SHOWN, get(ZZAV_W_SHOWN) + 1);
}

/* the refresh period, from three vblanks (a flip to the buffer shown) */
static void measure_refresh(void)
{
    uint64_t tv[4], t0;
    double best = 0;
    int i;

    if (!dbuf)
        return;
    for (i = 0; i < 4; i++) {
        c1ld[C1LD_W_FLIP] = c1ld[C1LD_W_FRONT];
        for (t0 = now_ticks(); c1ld[C1LD_W_FLIP] != 0 && now_ticks() - t0 < pl_tick_hz / 10; )
            ;
        if (c1ld[C1LD_W_FLIP] != 0) {           /* no vblank: the overlay is off */
            c1ld[C1LD_W_FLIP] = 0;
            return;
        }
        tv[i] = now_ticks();
    }
    for (i = 1; i < 4; i++) {
        double d = secs(tv[i] - tv[i - 1]);

        if (d > 0.012 && d < 0.05 && (best == 0 || d < best))
            best = d;
    }
    if (best > 0)
        refresh = best;
}

static void hband(struct AVCodecContext *s, const AVFrame *src, int offset[AV_NUM_DATA_POINTERS],
                  int y, int type, int height)
{
    uint64_t t = now_ticks();

    (void)s; (void)src; (void)offset; (void)y; (void)type; (void)height;
    flip_poll(t);
    flip_due(t);
    if (audio_on && t - last_service > pl_tick_hz / 100) {
        last_service = t;
        audio_service();
    }
}

/* ---- the picture ---- */

static int ty[256], trv[256], tgu[256], tgv[256], tbu[256], full_range;
static uint32_t *line;

static void yuv_tables(int full)
{
    int i;

    full_range = full;
    for (i = 0; i < 256; i++) {
        int c = full ? i : i - 16, d = i - 128;

        ty[i] = (full ? 256 : 298) * c + 128;
        trv[i] = (full ? 359 : 409) * d;
        tgu[i] = (full ? -88 : -100) * d;
        tgv[i] = (full ? -183 : -208) * d;
        tbu[i] = (full ? 454 : 516) * d;
    }
}

static uint32_t clamp8(int v)
{
    v >>= 8;
    return v < 0 ? 0 : v > 255 ? 255 : (uint32_t)v;
}

#include "yuvrow.h"

/* double buffering (core 0's C1LD_FEAT_OVL_FLIP): draw into the buffer not shown,
   then ask core 0 to show it from the next vblank */
static volatile uint32_t *c1ld;
static int dbuf;

static uint32_t ovl_back(void)
{
    if (!dbuf)
        return ZZAV_OVL_OFF;
    return __builtin_bswap32(c1ld[C1LD_W_FRONT]) == 2 ? ZZAV_OVL_OFF : ZZAV_OVL_OFF1;
}

static void flip(void)
{
    if (dbuf)
        c1ld[C1LD_W_FLIP] = __builtin_bswap32(ovl_back() == ZZAV_OVL_OFF ? 1 : 2);
}

static uint32_t wide[2048];

/* a row into the overlay, 64 bytes a burst: the buffers are uncached */
static void row_out(volatile uint8_t *dst, const uint32_t *src, int n)
{
#ifdef __ARM_NEON
    uint32_t *d = (uint32_t *)dst;

    for (; n >= 16; n -= 16, src += 16, d += 16) {
        uint32x4x4_t q;

        q.val[0] = vld1q_u32(src);
        q.val[1] = vld1q_u32(src + 4);
        q.val[2] = vld1q_u32(src + 8);
        q.val[3] = vld1q_u32(src + 12);
        vst1q_u32(d, q.val[0]);
        vst1q_u32(d + 4, q.val[1]);
        vst1q_u32(d + 8, q.val[2]);
        vst1q_u32(d + 12, q.val[3]);
    }
    dst = (volatile uint8_t *)d;
#endif
    if (n > 0)
        memcpy((void *)dst, src, (size_t)n * 4);
}

/* each of n pixels s times into wide[] */
static int widen(const uint32_t *in, int n, int s)
{
    int x = 0, k, o = 0;

#ifdef __ARM_NEON
    if (s == 2) {
        for (; x + 4 <= n; x += 4, o += 8) {
            const uint32x4_t q = vld1q_u32(in + x);
            const uint32x4x2_t z = vzipq_u32(q, q);

            vst1q_u32(wide + o, z.val[0]);
            vst1q_u32(wide + o + 4, z.val[1]);
        }
    }
#endif
    for (; x < n; x++)
        for (k = 0; k < s; k++)
            wide[o++] = in[x];
    return o;
}

/* n pixels stretched to o into wide[] (o > n: a picture with non-square
   pixels, such as a 16:9 DVD's 720 x 576), sampled at each pixel's centre */
static int stretch(const uint32_t *in, int n, int o)
{
    const uint32_t step = ((uint32_t)n << 16) / (uint32_t)o;
    uint32_t pos = step >> 1;
    int i;

    for (i = 0; i < o; i++, pos += step)
        wide[i] = in[pos >> 16];
    return o;
}

/* a new size or place: what the last picture left in the two buffers goes */
static void clear_overlay(void)
{
    const uint32_t pitch = get(ZZAV_W_OVL_PITCH);
    const int rows = 1080;
    int y;

    memset(wide, 0, sizeof wide);
    if (pitch > sizeof wide)
        return;
    for (y = 0; y < rows; y++) {
        row_out(pl_board + ZZAV_OVL_OFF + (uint32_t)y * pitch, wide, (int)(pitch / 4));
        row_out(pl_board + ZZAV_OVL_OFF1 + (uint32_t)y * pitch, wide, (int)(pitch / 4));
    }
}

static void show(AVFrame *f)
{
    static uint32_t last_row0;

    int shift = (int)get(ZZAV_W_OVL_SHIFT) ? 1 : 0;
    int scale = shift ? 1 : (int)get(ZZAV_W_OVL_SCALE);
    int w = f->width >> shift, h = f->height >> shift, x, y;
    int maxw = (int)get(ZZAV_W_OVL_MAXW), maxh = (int)get(ZZAV_W_OVL_MAXH);
    int dw = (int)get(ZZAV_W_DISPW), ow;
    uint32_t pitch = get(ZZAV_W_OVL_PITCH);
    volatile uint8_t *dst = pl_board + ovl_back() + (uint32_t)get(ZZAV_W_OVL_ROW0) * pitch;

    if (scale < 1 || scale > 4)
        scale = 1;
    /* the shown width: the 68k's DISPW (coded width x sample aspect ratio) */
    dw = dw > 0 ? (dw >> shift) & ~1 : w;
    if (dw == w) {
        if (maxw > 0 && w * scale > maxw)
            w = maxw / scale;
        dw = w;
        ow = w * scale;
    } else {
        ow = dw * scale;                /* the whole picture, stretched */
        if (maxw > 0 && ow > maxw)
            ow = maxw;
    }
    if (maxh > 0 && h * scale > maxh)
        h = maxh / scale;
    if (w <= 0 || h <= 0 || ow <= 0 || !pitch || (uint32_t)ow * 4 > pitch ||
        (uint32_t)ow > sizeof wide / sizeof wide[0])
        return;
    if ((int)get(ZZAV_W_OUTW) != ow || (int)get(ZZAV_W_OUTH) != h * scale ||
        get(ZZAV_W_OVL_ROW0) != last_row0) {
        put(ZZAV_W_OUTW, (uint32_t)ow);
        put(ZZAV_W_OUTH, (uint32_t)(h * scale));
        last_row0 = get(ZZAV_W_OVL_ROW0);
        clear_overlay();
    }
    for (y = 0; y < h; y++) {
        int sy = y << shift;
        const uint8_t *py = f->data[0] + sy * f->linesize[0];
        const uint8_t *pu = f->data[1] + (sy >> 1) * f->linesize[1];
        const uint8_t *pv = f->data[2] + (sy >> 1) * f->linesize[2];

        const uint8_t *py2 = py + (sy + 1 < f->height ? f->linesize[0] : 0);

        x = 0;
#ifdef __ARM_NEON
        if (!full_range) {
            row_neon(line, py, py2, pu, pv, w & ~7, shift);
            x = w & ~7;
        }
#endif
        if (shift) {
            for (; x < w; x++) {
                int yy = ty[(py[x * 2] + py[x * 2 + 1] + py2[x * 2] + py2[x * 2 + 1] + 2) >> 2], u = pu[x], v = pv[x];

                line[x] = clamp8(yy + trv[v]) << 16 | clamp8(yy + tgu[u] + tgv[v]) << 8 | clamp8(yy + tbu[u]);
            }
        } else {
            for (; x < w; x++) {
                int yy = ty[py[x]], u = pu[x >> 1], v = pv[x >> 1];

                line[x] = clamp8(yy + trv[v]) << 16 | clamp8(yy + tgu[u] + tgv[v]) << 8 | clamp8(yy + tbu[u]);
            }
        }
        if (dw != w) {
            const int o = stretch(line, w, ow);
            int k;

            for (k = 0; k < scale; k++)
                row_out(dst + (uint32_t)(y * scale + k) * pitch, wide, o);
        } else if (scale == 1) {
            row_out(dst + (uint32_t)y * pitch, line, w);
        } else {
            const int o = widen(line, w, scale);
            int k;

            for (k = 0; k < scale; k++)
                row_out(dst + (uint32_t)(y * scale + k) * pitch, wide, o);
        }
        if ((y & 31) == 31)
            hband(NULL, NULL, NULL, 0, 0, 0);
    }
    /* the flip: flip_due(), at the frame's time */
}

/* ---- the whole ---- */

static AVCodecContext *open_codec(enum AVCodecID id, uint32_t extra_off, uint32_t extra_len)
{
    const AVCodec *c = avcodec_find_decoder(id);
    AVCodecContext *x = c ? avcodec_alloc_context3(c) : NULL;

    if (!x)
        return NULL;
    if (extra_len && extra_len <= ZZAV_EXTRA_MAX) {
        x->extradata = av_mallocz(extra_len + AV_INPUT_BUFFER_PADDING_SIZE);
        if (x->extradata) {
            memcpy(x->extradata, (const void *)(pl_board + extra_off), extra_len);
            x->extradata_size = (int)extra_len;
        }
    }
    x->thread_count = 1;
    if (id == AV_CODEC_ID_H264) {
        x->draw_horiz_band = hband;
        x->slice_flags = SLICE_FLAG_CODED_ORDER | SLICE_FLAG_ALLOW_FIELD;
    } else {
        x->sample_rate = (int)get(ZZAV_W_ARATE);
        x->channels = (int)get(ZZAV_W_ACH);
    }
    if (avcodec_open2(x, c, NULL) < 0)
        avcodec_free_context(&x);
    return x;
}

static void set_skip(int level)
{
    /* the picture's quality goes last: first frames nothing refers to, then the
       loop filter (blocks show), keyframes only as the last resort */
    if (level >= 2)
        vctx->flags2 |= AV_CODEC_FLAG2_FAST;
    else
        vctx->flags2 &= ~AV_CODEC_FLAG2_FAST;
    vctx->skip_loop_filter = level >= 2 ? AVDISCARD_ALL : AVDISCARD_DEFAULT;
    vctx->skip_frame = level >= 3 ? AVDISCARD_NONKEY : level >= 1 ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
    put(ZZAV_W_SKIP, (uint32_t)level);
}

static void cleanup(void)
{
    pq_flush(&vq);
    pq_flush(&aq);
    avcodec_free_context(&vctx);
    avcodec_free_context(&actx);
    av_frame_free(&vframe);
    for (int i = 0; i < NQ; i++)
        av_frame_free(&fq[i]);
    nq = ready = flip_req = 0;
    av_frame_free(&aframe);
    free(pcm);
    free(line);
    pcm = NULL;
    line = NULL;
    /* the board plays the ring on: leave silence in it */
    memset((void *)(pl_board + ZZAV_AUDIO_OFF), 0, ZZAV_AUDIO_SIZE);
}

int player_run(void)
{
    int skip = 0, shown_any = 0, i;
    double vpts = 0, vdur = 0.04, lag = 0;
    uint64_t t_report = 0, t_skip = 0;
    uint64_t dec_ticks = 0;
    uint32_t dec_n = 0;
    uint64_t t_first = 0, t_shown = 0, show_ticks = 0;
    uint32_t show_n = 0;

    blk = (volatile uint32_t *)(pl_board + ZZAV_OFF);
    c1ld = (volatile uint32_t *)(pl_board + C1LD_OFF);
    /* OPEN after OPEN_URL: core 1 fetched the stream, netsrc.c gives the packets */
    src_net = get(ZZAV_W_STATE) == ZZAV_READY && pl_net_pull;
    dbuf = get(ZZAV_W_OVL_DBUF) != 0;
    if (dbuf)
        c1ld[C1LD_W_FLIP] = 0;
    memset(&vq, 0, sizeof vq);
    memset(&aq, 0, sizeof aq);
    rd = 0;
    ring_size = ZZAV_RING_SIZE;
    eof_in = vflushed = vdone = aflushed = adone = 0;
    pcm_head = pcm_n = 0;
    pcm_valid = 0;
    rs_pos = 0;
    rs_prev[0] = rs_prev[1] = 0;
    seg_n = seg_first = 0;
    clock_hold = 0;
    chk_t = 0;
    audio_bad = 0;
    media_started = 0;
    asamples = 0;
    volume = 256;
    /* core 1's words; DISPW after them is the 68k's, set before INIT */
    for (int w = ZZAV_W_CLOCK_MS; w < ZZAV_W_DISPW; w++)
        put(w, 0);
    put(ZZAV_W_ERR, 0);

    vtb = get(ZZAV_W_VTB_DEN) ? (double)get(ZZAV_W_VTB_NUM) / get(ZZAV_W_VTB_DEN) : 0;
    atb = get(ZZAV_W_ATB_DEN) ? (double)get(ZZAV_W_ATB_NUM) / get(ZZAV_W_ATB_DEN) : 0;
    vctx = get(ZZAV_W_VCODEC) == ZZAV_H264 ? open_codec(AV_CODEC_ID_H264, ZZAV_VEXTRA_OFF, get(ZZAV_W_VEXTRA_LEN)) : NULL;
    {
        static const enum AVCodecID aid[] = { AV_CODEC_ID_NONE, AV_CODEC_ID_NONE, AV_CODEC_ID_AAC,
            AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3, AV_CODEC_ID_DTS, AV_CODEC_ID_MP3, AV_CODEC_ID_MP2, AV_CODEC_ID_VORBIS };
        const uint32_t ac = get(ZZAV_W_ACODEC);

        actx = ac >= ZZAV_AAC && ac <= ZZAV_VORBIS ?
               open_codec(aid[ac], ZZAV_AEXTRA_OFF, get(ZZAV_W_AEXTRA_LEN)) : NULL;
    }
    vframe = av_frame_alloc();
    for (i = 0; i < NQ; i++)
        fq[i] = av_frame_alloc();
    nq = ready = flip_req = new_lag = paused_now = 0;
    t_take = t_pause = paused_ticks = 0;
    clock_started = 0;
    memset(cad, 0, sizeof cad);
    refresh = 1.0 / 60;
    measure_refresh();
    put(ZZAV_W_REFRESH_US, (uint32_t)(refresh * 1e6));
    for (i = ZZAV_W_CAD1; i <= ZZAV_W_CAD4; i++)
        put(i, 0);
    aframe = av_frame_alloc();
    pcm = malloc(PCM_FRAMES * FRAME_BYTES);
    line = malloc(4096 * 4);
    if ((get(ZZAV_W_VCODEC) && !vctx) || (get(ZZAV_W_ACODEC) && !actx) || (!vctx && !actx) ||
        !vframe || !aframe || !pcm || !line || !fq[NQ - 1]) {
        put(ZZAV_W_ERR, !vctx && get(ZZAV_W_VCODEC) ? 1 : !actx && get(ZZAV_W_ACODEC) ? 2 : 3);
        put(ZZAV_W_STATE, ZZAV_ERROR);
        put(ZZAV_W_CMD, ZZAV_CMD_NONE);
        while (get(ZZAV_W_CMD) != ZZAV_CMD_CLOSE)
            ;
        cleanup();
        put(ZZAV_W_STATE, ZZAV_IDLE);
        put(ZZAV_W_CMD, ZZAV_CMD_NONE);
        return -1;
    }
    yuv_tables(0);
    audio_on = actx != NULL;
    if (actx)
        rs_step = (double)actx->sample_rate / RATE;
    /* start writing just ahead of the DMA */
    dma_last = pl_audio_pos() & ~3UL;
    if (dma_last >= ZZAV_AUDIO_SIZE)
        dma_last = 0;
    dma_abs = dma_last;
    wr_abs = dma_abs + PERIOD;
    put(ZZAV_W_STATE, ZZAV_PLAYING);
    put(ZZAV_W_CMD, ZZAV_CMD_NONE);
    running = 1;

    t_pass = now_ticks();
    idle_ticks = 0;
    for (;;) {
        uint64_t t = now_ticks();
        double clk;
        int paused;

        /* the loop only polls: a short pass found nothing to do */
        if (t - t_pass < pl_tick_hz / 10000)
            idle_ticks += t - t_pass;
        t_pass = t;
        if (get(ZZAV_W_CMD) == ZZAV_CMD_CLOSE)
            break;
        hb_service();
        paused = get(ZZAV_W_PAUSE) != 0;
        volume = (int)get(ZZAV_W_VOLUME);
        if (volume > 256 || volume < 0)
            volume = 256;
        if (get(ZZAV_W_STATE) == ZZAV_ERROR) {
            if (audio_on)
                audio_service();
            continue;
        }
        if (src_net)
            pl_net_pull();
        else
            pull_ring();
        audio_decode();
        if (audio_on)
            audio_service();

        /* the clock */
        if (!audio_on) {
            if (paused && !t_pause)
                t_pause = t;
            else if (!paused && t_pause) {
                paused_ticks += t - t_pause;
                t_pause = 0;
            }
        }
        paused_now = paused;
        flip_poll(t);
        clk = clock_now(t);

        /* the picture: decoded ahead into the queue */
        if (vctx && !vdone && nq < NQ) {
            int r = avcodec_receive_frame(vctx, fq[nq]);

            if (r == 0) {
                const AVFrame *f = fq[nq];

                if (f->best_effort_timestamp != AV_NOPTS_VALUE)
                    vpts = f->best_effort_timestamp * vtb;
                else
                    vpts += vdur;
                if (f->pkt_duration > 0)
                    vdur = f->pkt_duration * vtb;
                fq_pts[nq++] = vpts;
                put(ZZAV_W_DECODED, get(ZZAV_W_DECODED) + 1);
            } else if (r == AVERROR_EOF)
                vdone = 1;
            else if (r == AVERROR(EAGAIN)) {
                AVPacket *p = pq_get(&vq);

                if (p) {
                    uint64_t t0 = now_ticks();

                    avcodec_send_packet(vctx, p);
                    av_packet_free(&p);
                    dec_ticks += now_ticks() - t0;
                    dec_n++;
                } else if (eof_in && !vflushed) {
                    avcodec_send_packet(vctx, NULL);
                    vflushed = 1;
                }
            } else
                vdone = 1;
        }

        /* the next frame into the back buffer as soon as it is free (with one
           buffer only, the one on the screen: at the frame's time) */
        if (nq && !ready && !flip_req) {
            AVFrame *f = fq[0];
            const double fpts = fq_pts[0];

            if (!audio_on && !clock_started) {
                clock_started = 1;
                t_start = now_ticks();
                /* on from where the sound left off (it ended first), else from this frame */
                t_start_media = clock_hold > 0 && clock_hold < fpts ? clock_hold : fpts;
                clk = t_start_media;
            }
            if (clk < 0 && audio_on && !pcm_valid && (adone || (eof_in && !aq.n)))
                audio_on = 0;           /* the file's sound ended before it began: run on the timer */
            else if (!dbuf && (clk < 0 || clk < fpts - 0.005))
                ;
            else {
                /* late frames are dropped, but one is drawn at least every 80 ms:
                   a decoder slower than the file then shows ~12 fps, not nothing */
                if (clk >= 0 && shown_any && clk - fpts > 0.25 && !f->key_frame && secs(t - t_shown) < 0.08)
                    put(ZZAV_W_DROPPED, get(ZZAV_W_DROPPED) + 1);
                else {
                    uint64_t t0 = now_ticks();

                    show(f);
                    t_shown = now_ticks();
                    show_ticks += t_shown - t0;
                    show_n++;
                    if (!shown_any)
                        t_first = t_shown;
                    shown_any = 1;
                    ready = 1;
                    ready_pts = fpts;
                    ready_key = f->key_frame;
                }
                av_frame_unref(f);
                for (i = 1; i < nq; i++) {      /* the pool keeps its frames: rotate */
                    fq[i - 1] = fq[i];
                    fq_pts[i - 1] = fq_pts[i];
                }
                fq[nq - 1] = f;
                nq--;
            }
        }
        flip_due(now_ticks());

        /* fall behind: first the loop filter goes, then frames that are no
           reference, then all but keyframes (a last resort: files with few
           of them would show nothing) */
        if (new_lag) {
            new_lag = 0;
            lag = lag_last;
            put(ZZAV_W_LAG_MS, (uint32_t)(int32_t)(lag * 1000));
            if (t - t_skip > pl_tick_hz / 2) {
                int want = lag > 3.0 ? 3 : lag > 0.6 ? 2 : lag > 0.06 ? 1 : 0;

                if (want > skip || (want < skip && lag < 0.03)) {
                    skip = want > skip ? want : skip - 1;
                    set_skip(skip);
                    t_skip = t;
                }
            }
        }

        /* no sound to clock with: the timer (the DMA position does not move right,
           or the file's sound gave nothing a second after the first picture) */
        /* ... or the file's sound has ended before its picture: without this the clock
           stops at the last sample and the frames after it wait for ever */
        if (audio_on && actx && adone && !pcm_n && !aq.n &&
            get(ZZAV_W_ALEAD_MS) <= (LEAD_MIN + PERIOD) * 1000 / (RATE * FRAME_BYTES))
            audio_bad = 1;
        if (audio_on && (audio_bad || (shown_any && !pcm_valid && secs(t - t_first) > 1.0))) {
            audio_on = 0;
            clock_started = 0;
            memset((void *)(pl_board + ZZAV_AUDIO_OFF), 0, ZZAV_AUDIO_SIZE);
        }
        if (t - t_report > pl_tick_hz / 10) {
            t_report = t;
            pl_load(idle_ticks, get(ZZAV_W_SHOWN));
            idle_ticks = 0;
            put(ZZAV_W_ASAMPLES, asamples);
            put(ZZAV_W_CLOCKSRC, audio_on ? 1 : 2);
            put(ZZAV_W_CLOCK_MS, clk >= 0 ? (uint32_t)(clk * 1000) : 0);
            for (i = 1; i <= 4; i++)
                put(ZZAV_W_CAD1 + i - 1, cad[i]);
            if (show_n) {
                put(ZZAV_W_SHOW_US, (uint32_t)(secs(show_ticks) * 1e6 / show_n));
                if (show_n > 25) {
                    show_ticks = 0;
                    show_n = 0;
                }
            }
            if (dec_n) {
                put(ZZAV_W_VDEC_US, (uint32_t)(secs(dec_ticks) * 1e6 / dec_n));
                if (dec_n > 25) {
                    dec_ticks = 0;
                    dec_n = 0;
                }
            }
        }
        /* all shown and heard: say so, then play silence until CLOSE */
        if (eof_in && !vq.n && !aq.n && (!vctx || vdone) && !nq && !ready && !flip_req && (!actx || (adone && !pcm_n)) &&
            get(ZZAV_W_STATE) == ZZAV_PLAYING &&
            (!audio_on || get(ZZAV_W_ALEAD_MS) <= (LEAD_MIN + PERIOD) * 1000 / (RATE * FRAME_BYTES)))   /* only silence ahead */
            put(ZZAV_W_STATE, ZZAV_FINISHED);
    }
    running = 0;
    cleanup();
    put(ZZAV_W_STATE, ZZAV_IDLE);
    put(ZZAV_W_CMD, ZZAV_CMD_NONE);
    return 0;
}
