/*
 * decode.c -- the timed decode loop of h264bench, the same on the ARM and
 * on the PC: the H.264 parser cuts the Annex B stream into access units,
 * the decoder decodes them one thread, one frame at a time.  Only
 * avcodec_send_packet + avcodec_receive_frame are timed; the checksum
 * (Adler-32 over every frame's luma, row by row) is not.
 */
#include <libavcodec/avcodec.h>
#include <libavutil/adler32.h>
#include "bench.h"

static void sum_frame(const AVFrame *f, struct hb_result *r)
{
    int y;

    for (y = 0; y < f->height; y++)
        r->checksum = (uint32_t)av_adler32_update(r->checksum, f->data[0] + (ptrdiff_t)y * f->linesize[0],
                                                  f->width);
    r->width = f->width;
    r->height = f->height;
}

/* send one packet (NULL: flush) and take what comes out, timed */
static int feed(AVCodecContext *ctx, AVPacket *pkt, AVFrame *f, hb_clock clock,
                void (*progress)(uint32_t), struct hb_result *r)
{
    uint64_t t0 = clock(), t;
    int ret = avcodec_send_packet(ctx, pkt);

    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        r->errors++;
        r->errcode = ret;
    }
    for (;;) {
        ret = avcodec_receive_frame(ctx, f);
        t = clock();
        r->ticks += t - t0;
        if (t - t0 > r->max_ticks)
            r->max_ticks = t - t0;
        if (ret < 0)
            break;
        r->frames++;
        sum_frame(f, r);
        av_frame_unref(f);
        if (progress)
            progress(r->frames);
        t0 = clock();
    }
    return ret == AVERROR_EOF ? 1 : 0;
}

int hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
              void (*progress)(uint32_t), struct hb_result *r)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecParserContext *parser = av_parser_init(AV_CODEC_ID_H264);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *f = av_frame_alloc();
    int ret = -1;

    r->frames = r->width = r->height = r->errors = 0;
    r->checksum = 1;            /* Adler-32's start */
    r->ticks = r->max_ticks = 0;
    r->errcode = 0;
    if (!parser || !ctx || !pkt || !f)
        goto out;
    ctx->thread_count = 1;
    if (flags & HB_FLAG_NODEBLOCK)
        ctx->skip_loop_filter = AVDISCARD_ALL;
    if ((ret = avcodec_open2(ctx, codec, NULL)) < 0)
        goto out;
    for (;;) {
        int used = av_parser_parse2(parser, ctx, &pkt->data, &pkt->size, data, (int)size,
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

        if (used < 0) {
            ret = used;
            goto out;
        }
        data += used;
        size -= (size_t)used;
        if (pkt->size)
            feed(ctx, pkt, f, clock, progress, r);
        if (!size && !pkt->size)
            break;              /* the parser has given out its last unit */
    }
    feed(ctx, NULL, f, clock, progress, r);
    ret = 0;
out:
    if (ret < 0)
        r->errcode = ret;
    av_frame_free(&f);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    if (parser)
        av_parser_close(parser);
    return ret;
}
