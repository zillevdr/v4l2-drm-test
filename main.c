#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include <libavcodec/avcodec.h>

#include "main.h"
#include "stream.h"
#include "v4l2.h"
#include "video.h"

void PacketToOut(void)
{
	AVPacket pkt;
	av_init_packet(&pkt);
	ReadPacket(&pkt);
	QueuePacketOut(&pkt, 0);
}

int main(int c, char *v[])
{
	int i;

	if (c < 2) {
		printf ("Usage: ./v4l2_test <url>\n"
				"./v4l2_test /mnt/share/video-samples/00005.ts\n");
		return 1;
	}

	if (!fd_v4l2_dec)
//		fd_v4l2_dec = open("/dev/video0", O_RDWR); // Cubie und Odroid-C2
		fd_v4l2_dec = open("/dev/video6", O_RDWR); // Odroid
//		fd_v4l2_dec = open("/dev/video7", O_RDWR); // Matrix
	if (fd_v4l2_dec < 0)
		fprintf(stderr, "V4l2Open: Open fd_v4l2_dec failed: (%d): %m\n", errno);

	PrintCaps(fd_v4l2_dec);

	StreamOpen(v[1]);
	VideoInit();

	V4l2SetupOutput();
	decoder_start = 0;
	dec_buf_out_index = 0;
	while(!decoder_start)
		PacketToOut();

	V4l2SetupCapture();

	for (i = 0; i < BUF_CAP; i++) {
		PacketToOut();
	}

//	DequeueBufferCapture();
//	Drm_page_flip_event(0,0,0,0,0);
	StartPlay();
	PacketToOut();
	Drm_page_flip_event(0,0,0,0,0);
	PacketToOut();
	Drm_page_flip_event(0,0,0,0,0);
	PacketToOut();
	Drm_page_flip_event(0,0,0,0,0);
	PacketToOut();
	Drm_page_flip_event(0,0,0,0,0);

	sleep(10);

	StreamClose();
	StreamOff();
	MunmapBuffer();

	VideoDeInit();

	close(fd_v4l2_dec);

	return EXIT_SUCCESS;
}
