
#define BUF_CAP	13	///< maximal of frames dec capture
#define BUF_OUT	3	///< maximal of frames dec out

struct buffers {
	void *start;
	size_t length;
	size_t offset;
//	AVPacket *pkt;
};

	int fd_v4l2_dec;
	int decoder_start;
	int dec_buf_out_index;
//	int use_v4l2;
//	int buf_in;
//	struct v4l2_format dec_fmt_in;
//	struct v4l2_buffer buffer_in;
//	struct v4l2_buffer buffer_out;
	struct buffers buffers_cap[BUF_CAP];
	struct buffers buffers_out[BUF_OUT];
