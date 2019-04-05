
void PrintCaps(int fd_v4l2);

void V4l2SetupOutput(void);

void V4l2SetupCapture(void);

void QueuePacketOut(AVPacket *pkt, uint32_t flags);

void DequeueBufferCapture(uint8_t *plane0, uint8_t *plane1);

void MunmapBuffer(void);

void StreamOff(void);
