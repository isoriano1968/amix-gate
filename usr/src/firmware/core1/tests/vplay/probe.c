/* probe.c -- (PC) a file's streams by numbers: codec, size, rate, profile, bitrate */
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>

int main(int argc, char **argv)
{
    AVFormatContext *fmt = NULL;
    unsigned i;

    if (argc < 2 || avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0)
        return 1;
    printf("%s: %.1f s, %.0f kbit/s\n", fmt->iformat->name, fmt->duration / 1e6, fmt->bit_rate / 1000.0);
    for (i = 0; i < fmt->nb_streams; i++) {
        AVStream *s = fmt->streams[i];
        AVCodecParameters *c = s->codecpar;
        AVRational fr = av_guess_frame_rate(fmt, s, NULL);

        if (c->codec_type == AVMEDIA_TYPE_VIDEO)
            printf("  video %s %s level %d, %dx%d, %.3f fps, %s, %.0f kbit/s\n", avcodec_get_name(c->codec_id),
                   avcodec_profile_name(c->codec_id, c->profile) ? avcodec_profile_name(c->codec_id, c->profile) : "?",
                   c->level, c->width, c->height, av_q2d(fr), av_get_pix_fmt_name(c->format), c->bit_rate / 1000.0);
        else if (c->codec_type == AVMEDIA_TYPE_AUDIO)
            printf("  audio %s, %d Hz, %d ch, %.0f kbit/s\n", avcodec_get_name(c->codec_id), c->sample_rate,
                   c->channels, c->bit_rate / 1000.0);
    }
    return 0;
}
