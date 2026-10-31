/* adec -- decode a file's sound and convert it to 16-bit mono, as vplay does (PC profiling harness) */
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>

int main(int argc, char **argv)
{
	AVFormatContext *fmt = NULL;
	AVCodecContext *d;
	const AVCodec *c;
	AVPacket *p = av_packet_alloc();
	AVFrame *f = av_frame_alloc();
	SwrContext *swr;
	uint8_t buf[65536], *out = buf;
	int a;
	long n = 0;

	if (argc < 2 || avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0)
		return 1;
	a = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	c = avcodec_find_decoder(fmt->streams[a]->codecpar->codec_id);
	d = avcodec_alloc_context3(c);
	avcodec_parameters_to_context(d, fmt->streams[a]->codecpar);
	avcodec_open2(d, c, NULL);
	swr = swr_alloc_set_opts(NULL, AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, d->sample_rate,
	    av_get_default_channel_layout(d->channels), d->sample_fmt, d->sample_rate, 0, NULL);
	swr_init(swr);
	while (av_read_frame(fmt, p) >= 0) {
		if (p->stream_index == a && avcodec_send_packet(d, p) >= 0)
			while (avcodec_receive_frame(d, f) >= 0)
				n += swr_convert(swr, &out, sizeof buf / 2, (const uint8_t **)f->extended_data, f->nb_samples);
		av_packet_unref(p);
	}
	printf("%ld samples\n", n);
	return 0;
}
