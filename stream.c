#include <fcntl.h>
#include <libintl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "stream.h"


AVFormatContext *avfmtctx;
int stream_index;


void StreamClose(void)
{
	if (avfmtctx)
		avformat_close_input(&avfmtctx);
}


int StreamOpen(char *url)
{
	int ret;

//	av_log_set_level(get_av_log_level());

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58,18,100)
	av_register_all();
#endif
	avformat_network_init();

	ret = avformat_open_input(&avfmtctx, url, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to open %s\n", url);
		goto fail;
	}

	ret = avformat_find_stream_info(avfmtctx, NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to get streams info\n");
		goto fail;
	}

	av_dump_format(avfmtctx, -1, url, 0);

	ret = av_find_best_stream(avfmtctx, AVMEDIA_TYPE_VIDEO, -1, -1,
				  NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "stream does not seem to contain video\n");
		goto fail;
	}
	stream_index = ret;
	return 0;

fail:
	StreamClose();
	return -1;
}


int ReadPacket(AVPacket * pkt)
{
read:
	if (av_read_frame(avfmtctx, pkt) < 0) {
		return -1;
	}
	if (stream_index != pkt->stream_index)
		goto read;
	return 0;
}
