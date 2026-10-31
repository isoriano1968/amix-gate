/*
 * aacdec_arm.c -- the ARM (core 1) build of FFmpeg's AAC decoder under
 * qemu-arm with semihosting: decode a pktdump file, report what comes out.
 *   qemu-arm -cpu cortex-a9 -semihosting aacdec_arm.elf file.pkts
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>

static uint32_t r32(FILE *f) { uint32_t v = 0; fread(&v, 4, 1, f); return v; }

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_AAC);
    AVCodecContext *x;
    AVPacket *p = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    char magic[4];
    long pkts = 0, frames = 0, samples = 0, errs = 0;
    double sum = 0;
    int r, len;

    if (!f || fread(magic, 4, 1, f) != 1 || memcmp(magic, "PKTS", 4) || !c) {
        printf("no input or no AAC decoder (%p)\n", (void *)c);
        return 1;
    }
    x = avcodec_alloc_context3(c);
    r32(f);
    x->sample_rate = (int)r32(f);
    x->channels = (int)r32(f);
    len = (int)r32(f);
    x->extradata = av_mallocz(len + AV_INPUT_BUFFER_PADDING_SIZE);
    fread(x->extradata, 1, (size_t)len, f);
    x->extradata_size = len;
    r = avcodec_open2(x, c, NULL);
    printf("open: %d, rate %d ch %d extradata %d\n", r, x->sample_rate, x->channels, len);
    if (r < 0)
        return 1;
    while ((len = (int)r32(f)) > 0 && len < (1 << 20)) {
        if (av_new_packet(p, len) < 0 || fread(p->data, 1, (size_t)len, f) != (size_t)len)
            break;
        pkts++;
        r = avcodec_send_packet(x, p);
        av_packet_unref(p);
        if (r < 0 && errs++ < 5)
            printf("send packet %ld: %d\n", pkts, r);
        while ((r = avcodec_receive_frame(x, fr)) == 0) {
            frames++;
            samples += fr->nb_samples;
            if (frames == 1)
                printf("first frame: format %d (fltp %d), %d samples, %d ch, rate %d\n", fr->format,
                       AV_SAMPLE_FMT_FLTP, fr->nb_samples, fr->channels, fr->sample_rate);
            if (fr->format == AV_SAMPLE_FMT_FLTP)
                for (int i = 0; i < fr->nb_samples; i++)
                    sum += ((float *)fr->extended_data[0])[i] * ((float *)fr->extended_data[0])[i];
            av_frame_unref(fr);
        }
        if (r != AVERROR(EAGAIN) && errs++ < 5)
            printf("receive after packet %ld: %d\n", pkts, r);
    }
    printf("%ld packets, %ld frames, %ld samples, rms %.4f, %ld errors\n", pkts, frames, samples,
           samples ? __builtin_sqrt(sum / samples) : 0.0, errs);
    return 0;
}
