#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include <libavcodec/avcodec.h>

#include "main.h"
#include "stream.h"
#include "v4l2.h"


void PrintCaps(int fd_v4l2)
{
	struct v4l2_capability caps;
	memset(&caps, 0, sizeof caps);

	if (ioctl(fd_v4l2, VIDIOC_QUERYCAP, &caps) != 0)
		fprintf(stderr, "VIDIOC_QUERYCAP failed: (%d): %m\n", errno);

	fprintf(stderr, "driver: %s card: %s bus_info: %s\n",
		caps.driver, caps.card, caps.bus_info);

	if (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)
		fprintf(stderr, "V4L2_CAP_VIDEO_CAPTURE is supported\n");
	if (caps.capabilities & V4L2_CAP_VIDEO_OUTPUT)
		fprintf(stderr, "V4L2_CAP_VIDEO_OUTPUT is supported\n");
	if (caps.capabilities & V4L2_CAP_VIDEO_OVERLAY)
		fprintf(stderr, "V4L2_CAP_VIDEO_OVERLAY is supported\n");
	if (caps.capabilities & V4L2_CAP_VIDEO_OVERLAY)
		fprintf(stderr, "V4L2_CAP_VIDEO_OVERLAY is supported\n");
	if (caps.capabilities & V4L2_CAP_VBI_CAPTURE)
		fprintf(stderr, "V4L2_CAP_VBI_CAPTURE is supported\n");
	if (caps.capabilities & V4L2_CAP_VBI_OUTPUT)
		fprintf(stderr, "V4L2_CAP_VBI_OUTPUT is supported\n");
	if (caps.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE)
		fprintf(stderr, "V4L2_CAP_SLICED_VBI_CAPTURE is supported\n");
	if (caps.capabilities & V4L2_CAP_SLICED_VBI_OUTPUT)
		fprintf(stderr, "V4L2_CAP_SLICED_VBI_OUTPUT is supported\n");
	if (caps.capabilities & V4L2_CAP_RDS_CAPTURE)
		fprintf(stderr, "V4L2_CAP_RDS_CAPTURE is supported\n");
	if (caps.capabilities & V4L2_CAP_VIDEO_OUTPUT_OVERLAY)
		fprintf(stderr, "V4L2_CAP_VIDEO_OUTPUT_OVERLAY is supported\n");
	if (caps.capabilities & V4L2_CAP_HW_FREQ_SEEK)
		fprintf(stderr, "V4L2_CAP_HW_FREQ_SEEK is supported\n");
	if (caps.capabilities & V4L2_CAP_RDS_OUTPUT)
		fprintf(stderr, "V4L2_CAP_RDS_OUTPUT is supported\n");

	if (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
		fprintf(stderr, "V4L2_CAP_VIDEO_CAPTURE_MPLANE is supported\n");
	if (caps.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE)
		fprintf(stderr, "V4L2_CAP_VIDEO_OUTPUT_MPLANE is supported\n");
	if (caps.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)
		fprintf(stderr, "V4L2_CAP_VIDEO_M2M_MPLANE is supported\n");
	if (caps.capabilities & V4L2_CAP_VIDEO_M2M)
		fprintf(stderr, "V4L2_CAP_VIDEO_M2M is supported\n");

	if (caps.capabilities & V4L2_CAP_EXT_PIX_FORMAT)
		fprintf(stderr, "V4L2_CAP_EXT_PIX_FORMAT is supported\n");

	if (caps.capabilities & V4L2_CAP_READWRITE)
		fprintf(stderr, "V4L2_CAP_READWRITE is supported\n");
	if (caps.capabilities & V4L2_CAP_ASYNCIO)
		fprintf(stderr, "V4L2_CAP_ASYNCIO is supported\n");
	if (caps.capabilities & V4L2_CAP_STREAMING)
		fprintf(stderr, "V4L2_CAP_STREAMING is supported\n");
	if (caps.capabilities & V4L2_CAP_META_CAPTURE)
		fprintf(stderr, "V4L2_CAP_META_CAPTURE is supported\n");

	if (caps.capabilities & V4L2_CAP_DEVICE_CAPS)
		fprintf(stderr, "V4L2_CAP_DEVICE_CAPS is supported\n");

	fprintf(stderr, "V4L2_CAP_DEVICE_CAPS %x\n", caps.device_caps);

}


void V4l2SetupOutput(void)
{
	// buffer out FORMAT OUT
	struct v4l2_buffer buf;
	struct v4l2_format fmt;
	unsigned int i;

	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
//	fmt.fmt.pix_mp.width = 1280;
//	fmt.fmt.pix_mp.height = 720;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 524288; // Das muss nachgebessert werden!!!

	if (ioctl(fd_v4l2_dec, VIDIOC_S_FMT, &fmt) < 0)
		fprintf(stderr, "V4l2SetupOutput: Output VIDIOC_S_FMT failed: (%d): %m\n", errno);

	printf("V4l2SetupOutput: FMT OUT: width %u height %u size %u 4cc = %.4s\n",
		fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.plane_fmt[0].sizeimage,
		(char*)&fmt.fmt.pix_mp.pixelformat);

	// REQBUFS OUT
	struct v4l2_requestbuffers reqbuf_out;
	struct v4l2_plane plane; //out have only one plane

	memset (&reqbuf_out, 0, sizeof (reqbuf_out));
	reqbuf_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	reqbuf_out.memory = V4L2_MEMORY_MMAP;
	reqbuf_out.count = BUF_OUT;

	if (ioctl (fd_v4l2_dec, VIDIOC_REQBUFS, &reqbuf_out) < 0)
		fprintf(stderr, "V4l2SetupOutput: Output VIDIOC_REQBUFS OUT failed: (%d): %m\n", errno);

	// QUERYBUF & MAP OUT
	for (i = 0; i < reqbuf_out.count; i++) {
		memset(&buf, 0, sizeof(buf));
		memset(&plane, 0, sizeof(plane));

		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		buf.m.planes = &plane;
		buf.length = 1;

		if (-1 == ioctl(fd_v4l2_dec, VIDIOC_QUERYBUF, &buf))
			fprintf(stderr, "V4l2SetupOutput: Output VIDIOC_QUERYBUF OUT failed: count %i (%d): %m\n", i, errno);

		buffers_out[i].length = buf.m.planes[0].length;
		buffers_out[i].offset = buf.m.planes[0].m.mem_offset;
		buffers_out[i].start = mmap(NULL, buf.m.planes[0].length,
			PROT_READ | PROT_WRITE, MAP_SHARED, fd_v4l2_dec,
			buf.m.planes[0].m.mem_offset);

		if (buffers_out[i].start == MAP_FAILED)
			fprintf(stderr, "V4l2SetupOutput: Output MAP_FAILED OUT failed: (%d): %m\n", errno);
	}
}


void V4l2SetupCapture(void)
{
	// buffer in FORMAT Capture
	struct v4l2_format fmt;
	struct v4l2_fmtdesc fdesc;
	unsigned int i;

	fdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fdesc.index = 0;
	if (ioctl(fd_v4l2_dec, VIDIOC_ENUM_FMT, &fdesc))
		fprintf(stderr, "VIDIOC_ENUM_FMT Capture failed: (%d): %m\n", errno);

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.pixelformat = fdesc.pixelformat;

	if (ioctl(fd_v4l2_dec, VIDIOC_G_FMT, &fmt))
		fprintf(stderr, "VIDIOC_G_FMT Capture failed: (%d): %m\n", errno);

//	fmt.fmt.pix_mp.width = 1280;
//	fmt.fmt.pix_mp.height = 720;
//	if (ioctl(fd_v4l2_dec, VIDIOC_S_FMT, &fmt))
//		fprintf(stderr, "VIDIOC_S_FMT Capture failed: (%d): %m\n", errno);

	// read video stream properties
/*	struct v4l2_control control = { 0, };
	control.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
	if (ioctl(fd_v4l2_dec, VIDIOC_G_CTRL, &control)) {
		fprintf(stderr, "Get a minimum buffers failed: (%d): %m\n", errno);
	} else {
		fprintf(stderr, "Get a minimum of %d buffers\n", control.value);
	}*/

	fprintf(stderr, "FMT CAPTURE: width %u height %u 4cc %.4s num_planes %d\n"
		"v4l2 plane 0 sizeimage %d bytesperline %d\n"
		"v4l2 plane 1 sizeimage %d bytesperline %d\n",
		fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
		(char*)&fmt.fmt.pix_mp.pixelformat, fmt.fmt.pix_mp.num_planes,
		fmt.fmt.pix_mp.plane_fmt[0].sizeimage, fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
		fmt.fmt.pix_mp.plane_fmt[1].sizeimage, fmt.fmt.pix_mp.plane_fmt[1].bytesperline);

// VIDIOC_G_SELECTION

	// REQBUFS Capture
	struct v4l2_requestbuffers reqbuf_cap;
	memset (&reqbuf_cap, 0, sizeof(reqbuf_cap));
	reqbuf_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	reqbuf_cap.memory = V4L2_MEMORY_MMAP;
	reqbuf_cap.count = BUF_CAP;

	if (ioctl (fd_v4l2_dec, VIDIOC_REQBUFS, &reqbuf_cap))
		fprintf(stderr, "VIDIOC_REQBUFS Capture failed: (%d): %m\n", errno);

	// QUERYBUF & MAP Capture
	for (i = 0; i < reqbuf_cap.count; i++) {
		struct v4l2_buffer buf;
		struct v4l2_plane planes[2] = { {0} };
		memset(&buf, 0, sizeof(buf));
		memset(planes, 0, sizeof(planes));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		buf.m.planes = planes;
		buf.length = fmt.fmt.pix_mp.num_planes;

		if ((ioctl(fd_v4l2_dec, VIDIOC_QUERYBUF, &buf)) < 0) {
			fprintf(stderr, "VIDIOC_QUERYBUF Capture failed: (%d): %m\n", errno);
			fprintf(stderr, "num_planes %d index %i\n",
				fmt.fmt.pix_mp.num_planes, buf.index);
			return;
		}

		buffers_cap[i].length = buf.m.planes[0].length;
		buffers_cap[i].offset = buf.m.planes[0].m.mem_offset;
		buffers_cap[i].start = mmap(NULL, buf.m.planes[0].length,
			PROT_READ | PROT_WRITE, MAP_SHARED, fd_v4l2_dec,
			buf.m.planes[0].m.mem_offset);

		if (buffers_cap[i].start == MAP_FAILED)
			fprintf(stderr, "MAP_FAILED Capture failed: (%d): %m\n", errno);

		// Queue buffer CAPTURE
		if (ioctl(fd_v4l2_dec, VIDIOC_QBUF, &buf) < 0)
			fprintf(stderr, "VIDIOC_QBUF Capture failed: (%d): %m\n", errno);
	}
	// STREAMON Capture hier ???
	enum v4l2_buf_type type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (ioctl(fd_v4l2_dec, VIDIOC_STREAMON, &type_cap)< 0)
		fprintf(stderr, "VIDIOC_STREAMON Capture failed: (%d): %m\n", errno);
	else fprintf(stderr, "VIDIOC_STREAMON Capture\n");
}


void StreamOff(void)
{
	QueuePacketOut(NULL, V4L2_BUF_FLAG_LAST);

	enum v4l2_buf_type type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (ioctl(fd_v4l2_dec, VIDIOC_STREAMOFF, &type_out)< 0)
		fprintf(stderr, "VIDIOC_STREAMOFF Output failed: (%d): %m\n", errno);

	enum v4l2_buf_type type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (ioctl(fd_v4l2_dec, VIDIOC_STREAMOFF, &type_cap)< 0)
		fprintf(stderr, "VIDIOC_STREAMOFF Capture failed: (%d): %m\n", errno);
}


int DequeuePacketOut(void)
{
	// Queue buffer OUT
	struct v4l2_buffer buf;
	struct v4l2_plane planes[1];

	// set buffer
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.length = 1;
	buf.m.planes = planes;

	if (ioctl(fd_v4l2_dec, VIDIOC_DQBUF, &buf) < 0) {
		fprintf(stderr, "VIDIOC_DQBUF OUTPUT failed: (%d): %m\n", errno);
		return 1;
	} else {
		return 0;
	}
}


void QueuePacketOut(AVPacket *pkt, uint32_t flags)
{
	// Queue buffer OUT
	struct v4l2_buffer buf;
	struct v4l2_plane planes[1];

	if (decoder_start > (BUF_OUT - 1)) {
		if (DequeuePacketOut()) {
			if (pkt)
				av_packet_unref(pkt);
			return;
		}
	}

	// set buffer
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.length = 1;
	buf.m.planes = planes;
	buf.index = dec_buf_out_index;

	// fill buffer
	if (pkt) {
		buf.m.planes[0].bytesused = pkt->size;
		memcpy(buffers_out[dec_buf_out_index].start, pkt->data, pkt->size);
	} else {
		buf.m.planes[0].bytesused = 0;
	}
	buf.m.planes[0].data_offset = 0;
	buf.flags = flags;

	if (ioctl(fd_v4l2_dec, VIDIOC_QBUF, &buf) < 0) {
		fprintf(stderr, "VIDIOC_QBUF OUT failed: (%d): %m\n", errno);
		if (pkt)
			av_packet_unref(pkt);
	} else {
		if (dec_buf_out_index == BUF_OUT - 1) {
			dec_buf_out_index = 0;
		} else {
			dec_buf_out_index++;
		}

		if(decoder_start == 0) {
			// STREAMON OUT hier ???
			enum v4l2_buf_type type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			if (ioctl(fd_v4l2_dec, VIDIOC_STREAMON, &type_out)< 0)
				fprintf(stderr, "VIDIOC_STREAMON OUT failed: (%d): %m\n", errno);
			else fprintf(stderr, "VIDIOC_STREAMON OUT\n");

			sleep(1);
		}
		decoder_start++;
		if (pkt)
			av_packet_unref(pkt);
	}
}


	// Dequeue buffer Capture
void DequeueBufferCapture(uint8_t *plane0, uint8_t *plane1)
{
	struct v4l2_plane planes[2];	// Das muss noch automatisiert werden!!!
	struct v4l2_buffer buf;
	int index;

	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.length = 2;
	buf.m.planes = planes;

	if (ioctl(fd_v4l2_dec, VIDIOC_DQBUF, &buf) < 0) {
		fprintf(stderr, "VIDIOC_DQBUF Capture failed: (%d): %m\n", errno);
	} else {

		memcpy(plane0, buffers_cap[buf.index].start, buf.m.planes[0].length);
		memcpy(plane1, buffers_cap[buf.index].start + buf.m.planes[0].bytesused,
			buf.m.planes[1].length);

		index = buf.index;
		memset(&buf, 0, sizeof(buf));
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.length = 2;
		buf.m.planes = planes;
		buf.index = index;

		if (ioctl(fd_v4l2_dec, VIDIOC_QBUF, &buf)) {
			fprintf(stderr, "VIDIOC_QBUF Capture failed: (%d): %m\n", errno);
		}
	}
}


void MunmapBuffer(void)
{
	int i;

	for (i = 0; i < BUF_OUT; i++) {
		if (munmap(buffers_out[i].start, buffers_out[i].length))
			fprintf(stderr, "munmap_buffer: munmap_buffer output failed: (%d): %m\n", errno);
	}
	for (i = 0; i < BUF_CAP; i++) {
		if (munmap(buffers_cap[i].start, buffers_cap[i].length))
			fprintf(stderr, "munmap_buffer: munmap_buffer capture failed: (%d): %m\n", errno);
	}
}
