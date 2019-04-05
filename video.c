#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <inttypes.h>

#include <libavcodec/avcodec.h>

#include "v4l2.h"

#define DRM_ALIGN(val, align)	((val + (align - 1)) & ~(align - 1))


struct drm_buf {
	uint32_t width, height;
	uint32_t size;
	uint32_t fb_id;
	uint32_t pix_fmt;
	uint32_t handle;
	uint32_t pitch[4];
	uint32_t offset[4];
	uint8_t *plane[4];
};

struct data_priv {
	int fd_drm;
	int loops;
	int front_buf;
	uint32_t encoder_id;
	uint32_t connector_id;
	uint32_t crtc_id;
	uint32_t video_plane;
	uint32_t osd_plane;
	int use_zpos;
	uint64_t zpos_overlay;
	uint64_t zpos_primary;
	struct drm_buf bufs[2];
	struct drm_buf buf_black;
	drmModeCrtc *saved_crtc;
	drmModeModeInfo mode_hd;
	drmModeModeInfo mode_hdr;
	drmEventContext ev;
};

static struct data_priv *d_priv = NULL;


// helper functions

static uint64_t GetPropertyValue(int fd_drm, uint32_t objectID,
						uint32_t objectType, const char *propName)
{
	uint32_t i;
	int found = 0;
	uint64_t value = 0;
	drmModePropertyPtr Prop;
	drmModeObjectPropertiesPtr objectProps =
		drmModeObjectGetProperties(fd_drm, objectID, objectType);

	for (i = 0; i < objectProps->count_props; i++) {
		if ((Prop = drmModeGetProperty(fd_drm, objectProps->props[i])) == NULL)
			fprintf(stderr, "Unable to query property.\n");

		if (strcmp(propName, Prop->name) == 0) {
			value = objectProps->prop_values[i];
			found = 1;
		}

		drmModeFreeProperty(Prop);

		if (found)
			break;
	}

	drmModeFreeObjectProperties(objectProps);

	if (!found)
		fprintf(stderr, "Unable to find value for property \'%s\'.\n", propName);

	return value;
}


static int DrmSetPropertyRequest(drmModeAtomicReqPtr ModeReq, int fd_drm,
					uint32_t objectID, uint32_t objectType,
					const char *propName, uint32_t value)
{
	uint32_t i;
	int found = 0;
	uint64_t id = 0;
	drmModePropertyPtr Prop;
	drmModeObjectPropertiesPtr objectProps =
		drmModeObjectGetProperties(fd_drm, objectID, objectType);

	for (i = 0; i < objectProps->count_props; i++) {
		if ((Prop = drmModeGetProperty(fd_drm, objectProps->props[i])) == NULL)
			fprintf(stderr, "Unable to query property.\n");

		if (strcmp(propName, Prop->name) == 0) {
			id = Prop->prop_id;
			found = 1;
		}

		drmModeFreeProperty(Prop);

		if (found)
			break;
	}

	drmModeFreeObjectProperties(objectProps);

	if (id == 0)
		fprintf(stderr, "Unable to find value for property \'%s\'.\n", propName);

	return drmModeAtomicAddProperty(ModeReq, objectID, id, value);
}


void Drm_page_flip_event( __attribute__ ((unused)) int fd,
					__attribute__ ((unused)) unsigned int frame,
					__attribute__ ((unused)) unsigned int sec,
					__attribute__ ((unused)) unsigned int usec,
					__attribute__ ((unused)) void *data)
{
	struct data_priv *priv = d_priv;
	struct drm_buf *buf = 0;

	buf = &priv->bufs[priv->front_buf];

	if (priv->loops < 100) {

		DequeueBufferCapture(buf->plane[0], buf->plane[1]);

		drmModeAtomicReqPtr ModeReq;
		const uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT;
		if (!(ModeReq = drmModeAtomicAlloc()))
			fprintf(stderr, "cannot allocate atomic request (%d): %m\n", errno);

		DrmSetPropertyRequest(ModeReq, priv->fd_drm, priv->video_plane,
						DRM_MODE_OBJECT_PLANE, "FB_ID", buf->fb_id);
		if (drmModeAtomicCommit(priv->fd_drm, ModeReq, flags, NULL) != 0)
			fprintf(stderr, "cannot page flip to FB %i (%d): %m\n",
				buf->fb_id, errno);

		drmModeAtomicFree(ModeReq);
		priv->front_buf ^= 1;
		priv->loops++;

/*		if (drmModePageFlip(priv->fd_drm, priv->crtc_id, buf->fb_id,
				DRM_MODE_PAGE_FLIP_EVENT, buf)) {
			fprintf(stderr, "Drm_page_flip_event: cannot page flip fb_id %i %i x %i (%d): %m\n",
				buf->fb_id, buf->width, buf->height, errno);
		} else {
			priv->front_buf ^= 1;
			priv->loops++;
		}*/
	}
}


static int Drm_find_dev(void)
{
	int fd_drm;
	drmModeRes *resources;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo *mode;
	drmModePlane *plane;
	drmModePlaneRes *plane_res;
	int i;
	uint64_t has_cap;
	uint32_t j, k;
	struct data_priv *priv;
	
//	fd_drm = drmOpen("imx-drm", NULL);
	fd_drm = open("/dev/dri/card0", O_RDWR);
	if (fd_drm < 0) {
		fprintf(stderr, "Drm_find_dev: drmOpen failed: (%d): %m\n", errno);
		goto out;
	}

	// check capability
	if (drmGetCap(fd_drm, DRM_CAP_DUMB_BUFFER, &has_cap) < 0 || has_cap == 0)
		fprintf(stderr, "drmGetCap DRM_CAP_DUMB_BUFFER failed or doesn't have dumb buffer\n");

	if (drmSetClientCap(fd_drm, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
		fprintf(stderr, "DRM_CLIENT_CAP_UNIVERSAL_PLANES not available.\n");
		goto close_fd;
	}

	if (drmSetClientCap(fd_drm, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
		fprintf(stderr, "DRM_CLIENT_CAP_ATOMIC not available.\n");
		goto close_fd;
	}

	resources = drmModeGetResources(fd_drm);
	if (resources == NULL) {
		fprintf(stderr, "drmModeGetResources failed: (%d): %m\n", errno);
		goto close_fd;
	}

	if (drmGetCap(fd_drm, DRM_CAP_PRIME, &has_cap) < 0)
		fprintf(stderr, "Drm_find_dev: DRM_CAP_PRIME not available.\n");

	if (drmGetCap(fd_drm, DRM_PRIME_CAP_EXPORT, &has_cap) < 0)
		fprintf(stderr, "Drm_find_dev: DRM_PRIME_CAP_EXPORT not available.\n");

	if (drmGetCap(fd_drm, DRM_PRIME_CAP_IMPORT, &has_cap) < 0)
		fprintf(stderr, "Drm_find_dev: DRM_PRIME_CAP_IMPORT not available.\n");

	if (drmGetCap(fd_drm, DRM_CAP_ADDFB2_MODIFIERS, &has_cap) < 0)
		fprintf(stderr, "Drm_find_dev: DRM_CAP_ADDFB2_MODIFIERS not available.\n");

	fprintf(stderr, "drmModeGetResources count_fbs: %i count_crtcs: %i crtcs[0] %i crtcs[1] %i crtcs[2] %i count_connectors: %i count_encoders: %i\n",
		resources->count_fbs, resources->count_crtcs, resources->crtcs[0], resources->crtcs[1], resources->crtcs[2],
		 resources->count_connectors, resources->count_encoders);

	// create a private structure
	priv = malloc(sizeof(*priv));
	memset(priv, 0, sizeof(*priv));
	priv->fd_drm = fd_drm;
	priv->video_plane = 0;
	priv->osd_plane = 0;
	priv->use_zpos = 0;

	// find the first available connector with modes
	for (i=0; i < resources->count_connectors; ++i) {
		connector = drmModeGetConnector(fd_drm, resources->connectors[i]);
		if(connector != NULL && connector->connection == DRM_MODE_CONNECTED
			&& connector->count_modes > 0) {
			priv->connector_id = connector->connector_id;
			break;
		}
		else
			fprintf(stderr, "Drm_find_dev: get a null connector pointer\n");
	}
	if (i == resources->count_connectors) {
		fprintf(stderr, "Drm_find_dev: No active connector found.\n");
		goto free_drm_res;
	}

    // search Modes for HD and HDready
	for (i = 0; i < connector->count_modes; i++) {
		mode = &connector->modes[i];
		// Mode HD
		if (mode->hdisplay == 1920 && mode->vdisplay == 1080 && mode->vrefresh == 50
				&& !(mode->flags & DRM_MODE_FLAG_INTERLACE)) {
			memcpy(&priv->mode_hd, mode, sizeof(priv->mode_hd));
			fprintf(stderr, "Drm_find_dev: Find Mode %ix%i@%i\n", mode->hdisplay, mode->vdisplay, mode->vrefresh);
		}
		// Mode HDready
		if (mode->hdisplay == 1280 && mode->vdisplay == 720 && mode->vrefresh == 50
				&& !(mode->flags & DRM_MODE_FLAG_INTERLACE)) {
			memcpy(&priv->mode_hdr, mode, sizeof(priv->mode_hdr));
			fprintf(stderr, "Drm_find_dev: Find Mode %ix%i@%i\n", mode->hdisplay, mode->vdisplay, mode->vrefresh);
		}
	}

	// find the encoder matching the first available connector
	for (i=0; i < resources->count_encoders; ++i) {
		encoder = drmModeGetEncoder(fd_drm, resources->encoders[i]);

		// If there more then one encoder this must rewrite
		if (encoder != NULL && encoder->encoder_id == connector->encoder_id) {
			priv->encoder_id = encoder->encoder_id;
			priv->crtc_id = encoder->crtc_id;
			break;
		} else
			fprintf(stderr, "Drm_find_dev: get a null encoder pointer\n");
	}
	if (i == resources->count_encoders) {
		fprintf(stderr, "No matching encoder with connector!\n");
		goto free_drm_res;
	}

	// find planes
	if ((plane_res = drmModeGetPlaneResources(fd_drm)) == NULL)
		fprintf(stderr, "cannot retrieve PlaneResources (%d): %m\n", errno);

	for (j = 0; j < plane_res->count_planes; j++) {
		plane = drmModeGetPlane(fd_drm, plane_res->planes[j]);

		if (plane == NULL)
			fprintf(stderr, "cannot query DRM-KMS plane %d\n", j);

		for (i = 0; i < resources->count_crtcs; i++) {
			if (plane->possible_crtcs & (1 << i))
				break;
		}

		uint64_t type = GetPropertyValue(fd_drm, plane_res->planes[j],
							DRM_MODE_OBJECT_PLANE, "type");
		uint64_t zpos = GetPropertyValue(fd_drm, plane_res->planes[j],
							DRM_MODE_OBJECT_PLANE, "zpos");

/*		fprintf(stderr, "Plane id %i crtc_id %i possible_crtcs %i possible CRTC %i type %s zpos %"PRIu64"\n",
			plane->plane_id, plane->crtc_id, plane->possible_crtcs, resources->crtcs[i],
			(type == DRM_PLANE_TYPE_PRIMARY) ? "primary plane" :
			(type == DRM_PLANE_TYPE_OVERLAY) ? "overlay plane" :
			(type == DRM_PLANE_TYPE_CURSOR) ? "cursor plane" : "No plane type", zpos);*/

		// test pixel format and plane caps
		for (k = 0; k < plane->count_formats; k++) {
			if (encoder->possible_crtcs & plane->possible_crtcs) {
				switch (plane->formats[k]) {
					case DRM_FORMAT_NV12:
						if (!priv->video_plane) {
							if (type != DRM_PLANE_TYPE_PRIMARY) {
								priv->use_zpos = 1;
								priv->zpos_overlay = zpos;
							}
							priv->video_plane = plane->plane_id;
							if (plane->plane_id == priv->osd_plane)
								priv->osd_plane = 0;
						}
						fprintf(stderr, "Drm_find_dev: Pixel format 4cc = %.4s plane_id %i zpos %"PRIu64"\n",
							(char*)&plane->formats[k], priv->video_plane, zpos);
						break;
					case DRM_FORMAT_ARGB8888:
						if (!priv->osd_plane) {
							if (type != DRM_PLANE_TYPE_OVERLAY)
								priv->zpos_primary = zpos;
							priv->osd_plane = plane->plane_id;
						}
						fprintf(stderr, "Drm_find_dev: Pixel format 4cc = %.4s osd_plane %i zpos %"PRIu64"\n",
							(char*)&plane->formats[k], priv->osd_plane, zpos);
						break;
					default:
						break;
				}
			}
		}

		drmModeFreePlane(plane);
	}

	drmModeFreeEncoder(encoder);
	drmModeFreeConnector(connector);
	drmModeFreeResources(resources);

	drmModeFreePlaneResources(plane_res);

	if (!priv->video_plane || !priv->osd_plane) {
		fprintf(stderr, "No plane found! Video plane %i OSD Plane %i\n",
			priv->video_plane, priv->osd_plane);
		goto close_fd;
	}

	d_priv = priv;
	return 0;

free_drm_res:
	drmModeFreeEncoder(encoder);
	drmModeFreeResources(resources);

close_fd:
	drmClose(fd_drm);

out:
	return 1;
}


static void DrmDestroyFb(int fd_drm, struct drm_buf *buf)
{
	struct drm_mode_destroy_dumb dreq;

	if (munmap(buf->plane[0], buf->size) < 0)
		fprintf(stderr, "cannot unmap dumb buffer (%d): %m\n", errno);

	if (drmModeRmFB(fd_drm, buf->fb_id) < 0)
		fprintf(stderr, "cannot remove framebuffer (%d): %m\n", errno);

	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = buf->handle;

	if (drmIoctl(fd_drm, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq) < 0)
		fprintf(stderr, "cannot destroy dumb buffer (%d): %m\n", errno);
}


static int DrmSetupFb(struct drm_buf *buf, uint32_t pix_fmt)
{
	struct data_priv *priv = d_priv;
	struct drm_mode_create_dumb cdumb;
	struct drm_mode_map_dumb mdumb;
	uint64_t modifiers[4] = { 0, 0, 0, 0 };
	uint32_t handle[4] = { 0, 0, 0, 0 };

	uint32_t width = DRM_ALIGN(buf->width, 128);
	uint32_t height = DRM_ALIGN(buf->height, 64);
//	uint32_t width = buf->width;
//	uint32_t height = buf->height;

	memset(&cdumb, 0, sizeof(struct drm_mode_create_dumb));
	cdumb.width = width;
	cdumb.height = height;
//	cdumb.flags = DRM_MODE_FB_MODIFIERS;

	// 32 bpp for ARGB, 8 bpp for YUV420 and NV12
	if (pix_fmt == DRM_FORMAT_ARGB8888)
		cdumb.bpp = 32;
	else
		cdumb.bpp = 12;

	if (drmIoctl(priv->fd_drm, DRM_IOCTL_MODE_CREATE_DUMB, &cdumb) < 0) {
		fprintf(stderr, "cannot create dumb buffer (%d): %m\n", errno);
		return -errno;
	}

	buf->size = cdumb.size;

	if (pix_fmt == DRM_FORMAT_ARGB8888) {
		buf->handle = handle[0] = cdumb.handle;
		buf->pitch[0] = cdumb.pitch;
		buf->offset[0] = 0;
	}

	if (pix_fmt == DRM_FORMAT_YUV420) {
		buf->handle = handle[2] = handle[1] = handle[0] = cdumb.handle;
		buf->pitch[0] = width;
		buf->pitch[2] = buf->pitch[1] = buf->pitch[0] / 2;

		buf->offset[0] = 0;
		buf->offset[1] = buf->pitch[0] * height;
		buf->offset[2] = buf->offset[1] + buf->pitch[1] * height / 2;
	}

	if (pix_fmt == DRM_FORMAT_NV12) {
		fprintf(stderr, "dumb buffer size: %llu pitch: %d\n",
		cdumb.size, cdumb.pitch);
		buf->handle = handle[1] = handle[0] = cdumb.handle;
		buf->pitch[1] = buf->pitch[0] = width;

		buf->offset[0] = 0;
		buf->offset[1] = buf->pitch[0] * height;
	}

	modifiers[0] = DRM_FORMAT_MOD_SAMSUNG_64_32_TILE;
	modifiers[1] = DRM_FORMAT_MOD_SAMSUNG_64_32_TILE;
	fprintf(stderr, "DRM_ALIGN width %d height %d\n", width, height);

	if (drmModeAddFB2WithModifiers(priv->fd_drm, width, height, pix_fmt,
			handle, buf->pitch, buf->offset, modifiers, &buf->fb_id, DRM_MODE_FB_MODIFIERS)) {
		fprintf(stderr, "cannot create modifiers framebuffer (%d): %m\n", errno);
		goto clean_dumb;
	}

/*	if (drmModeAddFB2(priv->fd_drm, width, height,
		pix_fmt, handle, buf->pitch, buf->offset, &buf->fb_id, 0)) {
		fprintf(stderr, "cannot create framebuffer (%d): %m\n", errno);
		goto clean_dumb;
	}*/

	memset(&mdumb, 0, sizeof(struct drm_mode_map_dumb));
	mdumb.handle = cdumb.handle;

	if (drmIoctl(priv->fd_drm, DRM_IOCTL_MODE_MAP_DUMB, &mdumb)) {
		fprintf(stderr, "cannot map dumb buffer (%d): %m\n", errno);
		goto clean_fb;
	}

	buf->plane[0] = mmap(0, cdumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, priv->fd_drm, mdumb.offset);
	if (buf->plane[0] == MAP_FAILED) {
		fprintf(stderr, "cannot mmap dumb buffer (%d): %m\n", errno);
		goto clean_fb;
	}
	buf->plane[1] = buf->plane[0] + buf->offset[1];
	buf->plane[2] = buf->plane[0] + buf->offset[2];

	return 0;

clean_fb:
	if (drmModeRmFB(priv->fd_drm, buf->fb_id) < 0)
		fprintf(stderr, "cannot remove framebuffer (%d): %m\n", errno);

clean_dumb:
	fprintf(stderr, "destroy dumb buffer\n");
	struct drm_mode_destroy_dumb dreq;
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = cdumb.handle;
	if (drmIoctl(priv->fd_drm, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq) < 0)
		fprintf(stderr, "cannot destroy dumb buffer (%d): %m\n", errno);

	return -errno;
}


///
/// If primary plane support only rgb and overlay plane nv12
/// must the zpos change. At the end it must change back.
/// @param backward		if set change to origin.
///
void DrmChangePlanes(int back)
{
	struct data_priv *priv = d_priv;
	drmModeAtomicReqPtr ModeReq;
	const uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
	uint64_t zpos_video;
	uint64_t zpos_osd;

	if (!(ModeReq = drmModeAtomicAlloc()))
		fprintf(stderr, "cannot allocate atomic request (%d): %m\n", errno);

	if (back) {
		zpos_video = priv->zpos_overlay;
		zpos_osd = priv->zpos_primary;
	} else {
		zpos_video = priv->zpos_primary;
		zpos_osd = priv->zpos_overlay;
	}
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, priv->video_plane,
			DRM_MODE_OBJECT_PLANE, "zpos", zpos_video);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, priv->osd_plane,
			DRM_MODE_OBJECT_PLANE, "zpos", zpos_osd);

	if (drmModeAtomicCommit(priv->fd_drm, ModeReq, flags, NULL) != 0)
		fprintf(stderr, "cannot change planes (%d): %m\n", errno);

	drmModeAtomicFree(ModeReq);
}


void DrmSetCrtc(struct data_priv *priv, drmModeAtomicReqPtr ModeReq,
				uint32_t plane_id)
{
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "CRTC_X", 0);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "CRTC_W", priv->mode_hd.hdisplay);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "CRTC_H", priv->mode_hd.vdisplay);
}


void DrmSetSrc(struct data_priv *priv, drmModeAtomicReqPtr ModeReq,
				uint32_t plane_id, struct drm_buf *buf)
{
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "SRC_X", 0);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "SRC_Y", 0);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "SRC_W", buf->width << 16);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "SRC_H", buf->height << 16);
}


void DrmSetPlane(uint32_t plane_id, struct drm_buf *buf)
{
	struct data_priv *priv = d_priv;
	drmModeAtomicReqPtr ModeReq;
	const uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;

	if (!(ModeReq = drmModeAtomicAlloc()))
		fprintf(stderr, "cannot allocate atomic request (%d): %m\n", errno);

	DrmSetCrtc(priv, ModeReq, plane_id);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "CRTC_ID", priv->crtc_id);

	DrmSetSrc(priv, ModeReq, plane_id, buf);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "FB_ID", buf->fb_id);

	if (drmModeAtomicCommit(priv->fd_drm, ModeReq, flags, NULL) != 0)
		fprintf(stderr, "cannot set plane (%d): %m\n", errno);

	drmModeAtomicFree(ModeReq);
}


void DrmSetBuf(uint32_t plane_id, struct drm_buf *buf)
{
	struct data_priv *priv = d_priv;
	drmModeAtomicReqPtr ModeReq;
	const uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;

	if (!(ModeReq = drmModeAtomicAlloc()))
		fprintf(stderr, "cannot allocate atomic request (%d): %m\n", errno);

	DrmSetSrc(priv, ModeReq, plane_id, buf);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, plane_id,
						DRM_MODE_OBJECT_PLANE, "FB_ID", buf->fb_id);

	if (drmModeAtomicCommit(priv->fd_drm, ModeReq, flags, NULL) != 0)
		fprintf(stderr, "cannot set atomic FB (%d): %m\n", errno);

	drmModeAtomicFree(ModeReq);
}


void VideoInit(void)
{
	struct data_priv *priv;

	if (Drm_find_dev()){
		fprintf(stderr, "VideoInit: drm_find_dev() failed\n");
	}

	priv = d_priv;

	// set essentials
	priv->bufs[0].width = priv->bufs[1].width = priv->mode_hdr.hdisplay; // mode_hdr for scaling
	priv->bufs[0].height = priv->bufs[1].height = priv->mode_hdr.vdisplay;
	priv->bufs[0].pix_fmt = priv->bufs[1].pix_fmt = DRM_FORMAT_NV12;
//	priv->buf_osd.pix_fmt = DRM_FORMAT_ARGB8888;
//	priv->buf_osd.width = priv->mode_hd.hdisplay;
//	priv->buf_osd.height = priv->mode_hd.vdisplay;

	// save actual modesetting for connector + CRTC
	priv->saved_crtc = drmModeGetCrtc(priv->fd_drm, priv->crtc_id);

	drmModeAtomicReqPtr ModeReq;
	const uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
	uint32_t modeID = 0;
	uint32_t prime_plane;
	uint32_t overlay_plane;

	if (priv->use_zpos) {
		prime_plane = priv->osd_plane;
		overlay_plane = priv->video_plane;
	} else {
		prime_plane = priv->video_plane;
		overlay_plane = priv->osd_plane;
	}
	// OSD FB
//	if (DrmSetupFb(&priv->buf_osd, DRM_FORMAT_ARGB8888)){
//		fprintf(stderr, "DrmSetupFb OSD FB failed\n");
//	}
	// black FB
	priv->buf_black.pix_fmt = DRM_FORMAT_NV12;
	priv->buf_black.width = 1280;
	priv->buf_black.height = 720;
	if (DrmSetupFb(&priv->buf_black, DRM_FORMAT_NV12))
		fprintf(stderr, "VideoInit: DrmSetupFB black FB %i x %i failed\n",
			priv->buf_black.width, priv->buf_black.height);
	unsigned int i;
	for (i = 0; i < priv->buf_black.width * priv->buf_black.height; ++i) {
		priv->buf_black.plane[0][i] = 0x10;
		if (i < priv->buf_black.width * priv->buf_black.height / 2)
		priv->buf_black.plane[1][i] = 0x80;
	}

	fprintf(stderr, "Setting mode  %ix%i@%i crtc_id %i prime_plane %i connector_id %i use_zpos %i\n",
		priv->mode_hd.hdisplay, priv->mode_hd.vdisplay, priv->mode_hd.vrefresh, priv->crtc_id,
		prime_plane, priv->connector_id, priv->use_zpos);

	if (drmModeCreatePropertyBlob(priv->fd_drm, &priv->mode_hd, sizeof(priv->mode_hd), &modeID) != 0)
		fprintf(stderr, "Failed to create mode property.\n");
	if (!(ModeReq = drmModeAtomicAlloc()))
		fprintf(stderr, "cannot allocate atomic request (%d): %m\n", errno);

	DrmSetPropertyRequest(ModeReq, priv->fd_drm, priv->crtc_id,
						DRM_MODE_OBJECT_CRTC, "MODE_ID", modeID);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, priv->connector_id,
						DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", priv->crtc_id);
	DrmSetPropertyRequest(ModeReq, priv->fd_drm, priv->crtc_id,
						DRM_MODE_OBJECT_CRTC, "ACTIVE", 1);
	DrmSetCrtc(priv, ModeReq, prime_plane);

	if (priv->use_zpos) {
		// Primary plane
//		DrmSetSrc(priv, ModeReq, prime_plane, &priv->buf_osd);
//		DrmSetPropertyRequest(ModeReq, priv->fd_drm, prime_plane,
//						DRM_MODE_OBJECT_PLANE, "FB_ID", priv->buf_osd.fb_id);
		// Black Buffer
		DrmSetCrtc(priv, ModeReq, overlay_plane);
		DrmSetPropertyRequest(ModeReq, priv->fd_drm, overlay_plane,
						DRM_MODE_OBJECT_PLANE, "CRTC_ID", priv->crtc_id);
		DrmSetSrc(priv, ModeReq, overlay_plane, &priv->buf_black);
		DrmSetPropertyRequest(ModeReq, priv->fd_drm, overlay_plane,
						DRM_MODE_OBJECT_PLANE, "FB_ID", priv->buf_black.fb_id);
	} else {
		// Black Buffer
		DrmSetSrc(priv, ModeReq, prime_plane, &priv->buf_black);
		DrmSetPropertyRequest(ModeReq, priv->fd_drm, prime_plane,
						DRM_MODE_OBJECT_PLANE, "FB_ID", priv->buf_black.fb_id);
	}
	if (drmModeAtomicCommit(priv->fd_drm, ModeReq, flags, NULL) != 0)
		fprintf(stderr, "cannot set atomic mode (%d): %m\n", errno);

	drmModeAtomicFree(ModeReq);

	if (DrmSetupFb(&priv->bufs[0], DRM_FORMAT_NV12)) {
		fprintf(stderr, "DrmSetupFb FB0 failed!\n");
	}
	if (DrmSetupFb(&priv->bufs[1], DRM_FORMAT_NV12)) {
		fprintf(stderr, "DrmSetupFb FB1 failed!\n");
	}

	// init variables page flip
	memset(&priv->ev, 0, sizeof(priv->ev));
//	priv->ev.version = DRM_EVENT_CONTEXT_VERSION;
	priv->ev.version = 2;
	priv->ev.page_flip_handler = Drm_page_flip_event;
}


void DebugMode(void)
{
	struct data_priv *priv = d_priv;
	uint32_t plane_id;

	if (priv->use_zpos) plane_id = priv->osd_plane;
	else plane_id = priv->video_plane;

//	uint64_t src_x = GetPropertyValue(priv->fd_drm, priv->plane_id,
//							DRM_MODE_OBJECT_PLANE, "SRC_X") >> 16;
//	uint64_t src_y = GetPropertyValue(priv->fd_drm, priv->plane_id,
//							DRM_MODE_OBJECT_PLANE, "SRC_Y") >> 16;
	uint64_t src_w = GetPropertyValue(priv->fd_drm, plane_id,
							DRM_MODE_OBJECT_PLANE, "SRC_W") >> 16;
	uint64_t src_h = GetPropertyValue(priv->fd_drm, plane_id,
							DRM_MODE_OBJECT_PLANE, "SRC_H") >> 16;
//	uint64_t crtc_x = GetPropertyValue(priv->fd_drm, priv->plane_id,
//							DRM_MODE_OBJECT_PLANE, "CRTC_X");
//	uint64_t crtc_y = GetPropertyValue(priv->fd_drm, priv->plane_id,
//							DRM_MODE_OBJECT_PLANE, "CRTC_Y");
	uint64_t crtc_w = GetPropertyValue(priv->fd_drm, plane_id,
							DRM_MODE_OBJECT_PLANE, "CRTC_W");
	uint64_t crtc_h = GetPropertyValue(priv->fd_drm, plane_id,
							DRM_MODE_OBJECT_PLANE, "CRTC_H");
	uint64_t fb_id = GetPropertyValue(priv->fd_drm, plane_id,
							DRM_MODE_OBJECT_PLANE, "FB_ID");
	uint64_t crtc_id = GetPropertyValue(priv->fd_drm, plane_id,
							DRM_MODE_OBJECT_PLANE, "CRTC_ID");
//	uint64_t fence_fd = GetPropertyValue(priv->fd_drm, priv->plane_id,
//							DRM_MODE_OBJECT_PLANE, "FENCE_FD");
	uint64_t zpos = GetPropertyValue(priv->fd_drm, plane_id,
							DRM_MODE_OBJECT_PLANE, "zpos");

//	fprintf(stderr, "SRC_X: %"PRIu64" SRC_Y: %"PRIu64" SRC_W: %"PRIu64" "
//		"SRC_H: %"PRIu64" CRTC_X: %"PRIu64" CRTC_Y: %"PRIu64" CRTC_W: %"PRIu64" "
//		"CRTC_H: %"PRIu64" FB_ID: %"PRIu64" CRTC_ID: %"PRIu64" Plane_ID: %i\n",
//		src_x, src_y, src_w, src_h, crtc_x, crtc_y, crtc_w, crtc_h, fb_id, crtc_id, priv->plane_id);
	fprintf(stderr, "SRC_W: %"PRIu64" SRC_H: %"PRIu64" CRTC_W: %"PRIu64" "
		"CRTC_H: %"PRIu64" FB_ID: %"PRIu64" CRTC_ID: %"PRIu64" zpos: %"PRIu64" Plane_ID: %i\n",
		src_w, src_h, crtc_w, crtc_h, fb_id, crtc_id, zpos, plane_id);
}


void DebugProps(int fd_drm, uint32_t objectID, uint32_t objectType)
{
	uint32_t i;

	drmModeObjectPropertiesPtr pModeObjectProperties =
		drmModeObjectGetProperties(fd_drm, objectID, objectType);
	fprintf(stderr, "Find %i properties.\n", pModeObjectProperties->count_props);

	for (i = 0; i < pModeObjectProperties->count_props; i++) {

		drmModePropertyPtr pProperty =
			drmModeGetProperty(fd_drm, pModeObjectProperties->props[i]);

		if (pProperty == NULL)
			fprintf(stderr, "Unable to query property.\n");

		if (pProperty->name && pModeObjectProperties->prop_values[i])
			fprintf(stderr, "Find property %s value %"PRIu64"\n",
				pProperty->name, pModeObjectProperties->prop_values[i]);
		else fprintf(stderr, "Find property %s\n", pProperty->name);


		drmModeFreeProperty(pProperty);
	}
	drmModeFreeObjectProperties(pModeObjectProperties);
}


void StartPlay(void)
{
	struct data_priv *priv = d_priv;
	struct drm_buf *buf = 0;
	priv->front_buf = 0;

	buf = &priv->bufs[priv->front_buf];

	DequeueBufferCapture(buf->plane[0], buf->plane[1]);

	DrmSetBuf(priv->video_plane, buf);

	priv->front_buf ^= 1;
}


void VideoDeInit(void)
{
	struct data_priv *priv = d_priv;

	// restore modesettings
	fprintf(stderr, "main: restore modesettings\n");
	if (priv->saved_crtc){
		drmModeSetCrtc(priv->fd_drm, priv->saved_crtc->crtc_id, priv->saved_crtc->buffer_id,
			priv->saved_crtc->x, priv->saved_crtc->y, &priv->connector_id, 1, &priv->saved_crtc->mode);
		drmModeFreeCrtc(priv->saved_crtc);
	}
	if (priv->use_zpos)
		DrmChangePlanes(1);

	// destroy framebuffer
//	DrmDestroyFb(priv->fd_drm, &priv->buf_osd);
	DrmDestroyFb(priv->fd_drm, &priv->buf_black);
	DrmDestroyFb(priv->fd_drm, &priv->bufs[0]);
	DrmDestroyFb(priv->fd_drm, &priv->bufs[1]);

	DebugMode();

//close_fd:
	drmClose(priv->fd_drm);
	free(priv);
}
