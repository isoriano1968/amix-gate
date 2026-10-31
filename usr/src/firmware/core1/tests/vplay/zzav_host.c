/*
 * zzav_host.c -- arm/h264bench/player.c on the PC: one thread is the 68k
 * (demuxes a file with libavformat, fills the ring as src/zzav.c does), one
 * is core 1 (player_run).  The audio DMA is simulated from the wall clock;
 * what it "plays" is written to a WAV, the overlay is saved as a PPM at the
 * end, and the player's report is printed every second.
 *
 *   zzav_host file.mp4 out.wav out.ppm [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <libavformat/avformat.h>
#include "../arm/h264bench/zzav.h"
#include "../arm/h264bench/player.h"

#define BOARD_SIZE      0x08000000UL
#define SCREEN_W        1920
#define SCREEN_H        1080

volatile uint8_t *pl_board;
uint64_t pl_tick_hz = 1000000000ULL;
static uint64_t t0;

uint64_t pl_ticks(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* 48 kHz, 4 bytes a frame, round the ring */
uint32_t pl_audio_pos(void)
{
    uint64_t bytes = (pl_ticks() - t0) * 192000 / 1000000000ULL;

    return (uint32_t)(bytes % ZZAV_AUDIO_SIZE) & ~3U;
}

static volatile uint32_t *blk;
static uint32_t get(int w) { return __builtin_bswap32(blk[w]); }
static void put(int w, uint32_t v) { blk[w] = __builtin_bswap32(v); }

static void *core1(void *arg)
{
    (void)arg;
    while (get(ZZAV_W_CMD) != ZZAV_CMD_OPEN)
        usleep(1000);
    player_run();
    return NULL;
}

/* the DMA's "playback": copy what it passed into the WAV */
static FILE *wav;
static uint32_t dma_seen;
static long wav_bytes;

static void dma_tap(void)
{
    uint32_t pos = pl_audio_pos();
    const volatile uint8_t *ring = pl_board + ZZAV_AUDIO_OFF;

    while (dma_seen != pos) {
        fwrite((const void *)(ring + dma_seen), 1, 4, wav);
        wav_bytes += 4;
        dma_seen = (dma_seen + 4) % ZZAV_AUDIO_SIZE;
    }
}

static void wav_header(long data)
{
    uint8_t h[44];
    uint32_t v;

    memcpy(h, "RIFF", 4); v = (uint32_t)(36 + data); memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8); v = 16; memcpy(h + 16, &v, 4);
    h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
    v = 48000; memcpy(h + 24, &v, 4); v = 192000; memcpy(h + 28, &v, 4);
    h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); v = (uint32_t)data; memcpy(h + 40, &v, 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
}

/* one packet into the ring, as src/zzav.c does; 0 if it does not fit now */
static uint32_t wr;

static int ring_put(int type, const AVPacket *p)
{
    volatile uint8_t *ring = pl_board + ZZAV_RING_OFF;
    uint32_t need = (ZZAV_PKT_HDR + (uint32_t)p->size + 3) & ~3U, rd = get(ZZAV_W_RD), free_;
    uint64_t pts = p->pts == AV_NOPTS_VALUE ? ZZAV_NOPTS : (uint64_t)p->pts;
    uint32_t h[4];

    if (wr + need > ZZAV_RING_SIZE) {   /* no room before the end: wrap */
        if (rd > wr || rd == 0)
            return 0;
        h[0] = __builtin_bswap32((uint32_t)ZZAV_PKT_WRAP << 24);
        memcpy((void *)(ring + wr), h, 4);
        wr = 0;
        put(ZZAV_W_WR, wr);
    }
    free_ = rd > wr ? rd - wr - 1 : ZZAV_RING_SIZE - wr + rd - 1;
    if (need > free_)
        return 0;
    memcpy((void *)(ring + wr + ZZAV_PKT_HDR), p->data, (size_t)p->size);
    h[0] = __builtin_bswap32((uint32_t)type << 24 | (p->flags & AV_PKT_FLAG_KEY ? ZZAV_PKT_KEY : 0));
    h[1] = __builtin_bswap32((uint32_t)p->size);
    h[2] = __builtin_bswap32((uint32_t)(pts >> 32));
    h[3] = __builtin_bswap32((uint32_t)pts);
    memcpy((void *)(ring + wr), h, ZZAV_PKT_HDR);
    wr += need;
    if (wr >= ZZAV_RING_SIZE)
        wr = 0;
    put(ZZAV_W_WR, wr);
    return 1;
}

int main(int argc, char **argv)
{
    AVFormatContext *fmt = NULL;
    AVPacket *p;
    pthread_t th;
    int vidx, aidx, have = 0, done = 0;
    double limit = argc > 4 ? atof(argv[4]) : 1e9;
    uint64_t t_last = 0, t_begin;

    if (argc < 4) {
        fprintf(stderr, "usage: zzav_host file.mp4 out.wav out.ppm [seconds]\n");
        return 2;
    }
    pl_board = calloc(1, BOARD_SIZE);
    blk = (volatile uint32_t *)(pl_board + ZZAV_OFF);
    wav = fopen(argv[2], "wb");
    if (!pl_board || !wav || avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 ||
        avformat_find_stream_info(fmt, NULL) < 0) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    wav_header(0);
    vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (vidx >= 0 && fmt->streams[vidx]->codecpar->codec_id == AV_CODEC_ID_H264) {
        AVCodecParameters *c = fmt->streams[vidx]->codecpar;

        put(ZZAV_W_VCODEC, ZZAV_H264);
        memcpy((void *)(pl_board + ZZAV_VEXTRA_OFF), c->extradata, (size_t)c->extradata_size);
        put(ZZAV_W_VEXTRA_LEN, (uint32_t)c->extradata_size);
        put(ZZAV_W_VTB_NUM, (uint32_t)fmt->streams[vidx]->time_base.num);
        put(ZZAV_W_VTB_DEN, (uint32_t)fmt->streams[vidx]->time_base.den);
        put(ZZAV_W_VW, (uint32_t)c->width);
        put(ZZAV_W_VH, (uint32_t)c->height);
    } else
        vidx = -1;
    if (aidx >= 0 && fmt->streams[aidx]->codecpar->codec_id == AV_CODEC_ID_AAC) {
        AVCodecParameters *c = fmt->streams[aidx]->codecpar;

        put(ZZAV_W_ACODEC, ZZAV_AAC);
        memcpy((void *)(pl_board + ZZAV_AEXTRA_OFF), c->extradata, (size_t)c->extradata_size);
        put(ZZAV_W_AEXTRA_LEN, (uint32_t)c->extradata_size);
        put(ZZAV_W_ATB_NUM, (uint32_t)fmt->streams[aidx]->time_base.num);
        put(ZZAV_W_ATB_DEN, (uint32_t)fmt->streams[aidx]->time_base.den);
        put(ZZAV_W_ARATE, (uint32_t)c->sample_rate);
        put(ZZAV_W_ACH, (uint32_t)c->channels);
    } else
        aidx = -1;
    printf("video %s, sound %s\n", vidx >= 0 ? "H.264" : "-", aidx >= 0 ? "AAC" : "-");
    put(ZZAV_W_OVL_PITCH, SCREEN_W * 4);
    put(ZZAV_W_OVL_ROW0, 100);
    put(ZZAV_W_OVL_SHIFT, getenv("SHIFT") ? (uint32_t)atoi(getenv("SHIFT")) : 0);
    put(ZZAV_W_OVL_MAXW, SCREEN_W);
    put(ZZAV_W_OVL_MAXH, SCREEN_H - 100);
    put(ZZAV_W_VOLUME, 256);
    t0 = pl_ticks();
    pthread_create(&th, NULL, core1, NULL);
    put(ZZAV_W_CMD, ZZAV_CMD_OPEN);
    t_begin = pl_ticks();

    p = av_packet_alloc();
    while (!done) {
        double t = (double)(pl_ticks() - t_begin) / 1e9;

        dma_tap();
        if (t > limit)
            break;
        if (!have && !get(ZZAV_W_EOF)) {
            if (av_read_frame(fmt, p) < 0)
                put(ZZAV_W_EOF, 1);
            else if (p->stream_index == vidx || p->stream_index == aidx)
                have = 1;
            else
                av_packet_unref(p);
        }
        if (have && ring_put(p->stream_index == vidx ? ZZAV_PKT_VIDEO : ZZAV_PKT_AUDIO, p)) {
            av_packet_unref(p);
            have = 0;
        } else
            usleep(2000);
        if (pl_ticks() - t_last > 1000000000ULL) {
            t_last = pl_ticks();
            printf("%5.1f s: state %u clock %u ms shown %u dropped %u decoded %u underruns %u dec %u us lead %u ms lag %d ms skip %u out %ux%u err %u\n",
                   t, get(ZZAV_W_STATE), get(ZZAV_W_CLOCK_MS), get(ZZAV_W_SHOWN), get(ZZAV_W_DROPPED),
                   get(ZZAV_W_DECODED), get(ZZAV_W_UNDERRUNS), get(ZZAV_W_VDEC_US), get(ZZAV_W_ALEAD_MS),
                   (int)get(ZZAV_W_LAG_MS), get(ZZAV_W_SKIP), get(ZZAV_W_OUTW), get(ZZAV_W_OUTH), get(ZZAV_W_ERR));
            fflush(stdout);
        }
        if (get(ZZAV_W_STATE) == ZZAV_FINISHED || get(ZZAV_W_STATE) == ZZAV_ERROR)
            done = 1;
    }
    printf("end: state %u clock %u ms shown %u dropped %u decoded %u underruns %u err %u\n",
           get(ZZAV_W_STATE), get(ZZAV_W_CLOCK_MS), get(ZZAV_W_SHOWN), get(ZZAV_W_DROPPED),
           get(ZZAV_W_DECODED), get(ZZAV_W_UNDERRUNS), get(ZZAV_W_ERR));
    {
        FILE *o = fopen(argv[3], "wb");
        uint32_t w = get(ZZAV_W_OUTW), h = get(ZZAV_W_OUTH), x, y;

        fprintf(o, "P6 %u %u 255\n", w, h);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                uint32_t v = *(volatile uint32_t *)(pl_board + ZZAV_OVL_OFF + (100 + y) * SCREEN_W * 4 + x * 4);
                uint8_t rgb[3] = { (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };

                fwrite(rgb, 1, 3, o);
            }
        fclose(o);
    }
    put(ZZAV_W_CMD, ZZAV_CMD_CLOSE);
    pthread_join(th, NULL);
    wav_header(wav_bytes);
    fclose(wav);
    printf("close: state %u, %ld bytes of sound tapped\n", get(ZZAV_W_STATE), wav_bytes);
    return 0;
}
