/*
 * pktdump.c -- (PC) a stream's packets into a file for tests/aacdec_arm.c
 * under qemu-arm: "PKTS", codec id, rate, channels, extradata length +
 * extradata, then per packet its size and data (32-bit little-endian).
 *   pktdump file.mp4 audio|video out.pkts
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <libavformat/avformat.h>

static void w32(FILE *o, uint32_t v) { fwrite(&v, 4, 1, o); }

int main(int argc, char **argv)
{
    AVFormatContext *fmt = NULL;
    AVPacket *p = av_packet_alloc();
    FILE *o;
    int idx;
    long n = 0;

    if (argc < 4 || avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0)
        return 1;
    idx = av_find_best_stream(fmt, strcmp(argv[2], "video") ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (idx < 0 || !(o = fopen(argv[3], "wb")))
        return 1;
    {
        AVCodecParameters *c = fmt->streams[idx]->codecpar;

        fwrite("PKTS", 4, 1, o);
        w32(o, (uint32_t)c->codec_id);
        w32(o, (uint32_t)c->sample_rate);
        w32(o, (uint32_t)c->channels);
        w32(o, (uint32_t)c->extradata_size);
        fwrite(c->extradata, 1, (size_t)c->extradata_size, o);
        printf("stream %d: codec %d rate %d ch %d extradata %d\n", idx, c->codec_id, c->sample_rate, c->channels, c->extradata_size);
    }
    while (av_read_frame(fmt, p) >= 0) {
        if (p->stream_index == idx) {
            w32(o, (uint32_t)p->size);
            fwrite(p->data, 1, (size_t)p->size, o);
            n++;
        }
        av_packet_unref(p);
    }
    fclose(o);
    printf("%ld packets\n", n);
    return 0;
}
