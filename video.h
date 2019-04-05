
void VideoInit(void);

void VideoDeInit(void);

void Drm_page_flip_event(int fd, unsigned int frame, unsigned int sec,
					unsigned int usec, void *data);

void StartPlay(void);
