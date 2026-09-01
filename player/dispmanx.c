#if defined(USE_LIBVLC)

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

struct drm_vec_plane {
    int fd;
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t connector_id;
    uint32_t possible_crtcs;
    int width;
    int height;
};

static struct drm_vec_plane drm_vec_plane = {
    .fd = -1,
    .plane_id = 0,
    .crtc_id = 0,
    .connector_id = 0,
    .possible_crtcs = 0,
    .width = 0,
    .height = 0,
};

static int drm_vec_plane_set_colorbar(void) {
    if (drm_vec_plane.fd < 0 || drm_vec_plane.plane_id == 0) {
        return -1;
    }

    uint32_t width = 720;
    uint32_t height = 576;
    uint32_t pitch = width * 4;
    uint32_t size = pitch * height;

    struct drm_mode_create_dumb create = {0};
    create.width = width;
    create.height = height;
    create.bpp = 32;
    create.flags = 0;
    if (drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        fprintf(stderr, "drm-rp1-vec: DRM_IOCTL_MODE_CREATE_DUMB failed\n");
        return -1;
    }

    struct drm_mode_map_dumb map = {0};
    map.handle = create.handle;
    if (drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        fprintf(stderr, "drm-rp1-vec: DRM_IOCTL_MODE_MAP_DUMB failed\n");
        struct drm_mode_destroy_dumb destroy = {
            .handle = create.handle,
        };

        drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        return -1;
    }

    uint8_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          drm_vec_plane.fd, map.offset);
    if (pixels == MAP_FAILED) {
        fprintf(stderr, "drm-rp1-vec: mmap failed: %s\n", strerror(errno));
        return -1;
    }

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t r, g, b;
            int bar = (x * 8) / width;
            switch (bar) {
                case 0: r = 255; g = 0;   b = 0;   break;
                case 1: r = 255; g = 255; b = 0;   break;
                case 2: r = 0;   g = 255; b = 0;   break;
                case 3: r = 0;   g = 255; b = 255; break;
                case 4: r = 0;   g = 0;   b = 255; break;
                case 5: r = 255; g = 0;   b = 255; break;
                case 6: r = 255; g = 255; b = 255; break;
                default: r = 64;  g = 64;  b = 64;  break;
            }
            uint32_t *pixel = (uint32_t *)(pixels + (y * pitch) + (x * 4));
            *pixel = (0xffu << 24) | (r << 16) | (g << 8) | b;
        }
    }

    uint32_t fb_id = 0;
    int ret = drmModeAddFB(drm_vec_plane.fd, width, height, 24, 32,
                           pitch, create.handle, &fb_id);
    if (ret != 0) {
        fprintf(stderr, "drm-rp1-vec: drmModeAddFB failed: %d\n", ret);
        munmap(pixels, size);
        return -1;
    }

    ret = drmModeSetPlane(drm_vec_plane.fd, drm_vec_plane.plane_id,
                          drm_vec_plane.crtc_id, fb_id, 0,
                          0, 0, width, height,
                          0, 0, width << 16, height << 16);
    if (ret != 0) {
        fprintf(stderr, "drm-rp1-vec: drmModeSetPlane failed: %d\n", ret);
        drmModeRmFB(drm_vec_plane.fd, fb_id);
        munmap(pixels, size);
        return -1;
    }

    printf("drm-rp1-vec: colorbar plane set fb=%u w=%u h=%u\n", fb_id, width, height);
    munmap(pixels, size);
    return 0;
}

static int drm_vec_plane_acquire(void) {
    if (drm_vec_plane.fd >= 0) {
        return 0;
    }

    const char *device = "/dev/dri/card0";
    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "drm-rp1-vec: failed to open %s: %s\n", device, strerror(errno));
        return -1;
    }
// Enable universal planes to expose primary and cursor planes
if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) < 0) {
    fprintf(stderr, "drm-rp1-vec: failed to set universal planes capability\n");
    close(fd);
    return -1;
}
    drmModeResPtr resources = drmModeGetResources(fd);
    if (!resources) {
        fprintf(stderr, "drm-rp1-vec: drmModeGetResources failed on %s\n", device);
        close(fd);
        return -1;
    }

    drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
    if (!planes || planes->count_planes == 0) {
        fprintf(stderr, "drm-rp1-vec: no planes reported for %s %p %d\n", device, planes, planes->count_planes);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    uint32_t plane_id = 0;
    drmModePlanePtr plane = NULL;
    for (int i = 0; i < planes->count_planes; ++i) {
        plane = drmModeGetPlane(fd, planes->planes[i]);
        if (!plane) {
            continue;
        }

        if (plane->possible_crtcs == 0 || plane->count_formats == 0) {
            drmModeFreePlane(plane);
            plane = NULL;
            continue;
        }

        plane_id = plane->plane_id;
        printf("drm-rp1-vec: plane candidate id=%u crtc=%u possible_crtcs=0x%x formats=%u\n",
               plane_id, plane->crtc_id, plane->possible_crtcs, plane->count_formats);
        break;
    }

    if (!plane) {
        fprintf(stderr, "drm-rp1-vec: no usable graphic plane found\n");
        drmModeFreePlaneResources(planes);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    drm_vec_plane.fd = fd;
    drm_vec_plane.plane_id = plane_id;
    drm_vec_plane.crtc_id = plane->crtc_id;
    drm_vec_plane.connector_id = resources->connectors[0];
    drm_vec_plane.possible_crtcs = plane->possible_crtcs;
    drm_vec_plane.width = 1920;
    drm_vec_plane.height = 1080;

    printf("drm-rp1-vec: acquired plane id=%u crtc=%u connector=%u possible_crtcs=0x%x\n",
           drm_vec_plane.plane_id,
           drm_vec_plane.crtc_id,
           drm_vec_plane.connector_id,
           drm_vec_plane.possible_crtcs);

    drmModeFreePlane(plane);
    drmModeFreePlaneResources(planes);
    drmModeFreeResources(resources);
    return 0;
}

void load_strap(char *path) {
    (void)path;
}

void dispmanx_init(void) {
    drm_vec_plane_acquire();
    drm_vec_plane_set_colorbar();
}

void dispmanx_alpha(int a) {
    (void)a;
}

void blank_background(void) {
}

void dispmanx_close(void) {
    if (drm_vec_plane.fd >= 0) {
        close(drm_vec_plane.fd);
        drm_vec_plane.fd = -1;
        drm_vec_plane.plane_id = 0;
        drm_vec_plane.crtc_id = 0;
        drm_vec_plane.connector_id = 0;
        drm_vec_plane.possible_crtcs = 0;
    }
}

void osd_text(const char *c, int align) {
    (void)c;
    (void)align;
}

void osd_text_clear(void) {
}

void bg_mode(int mode) {
    (void)mode;
}

char *dispmanx_shifted_window(void) {
    return NULL;
}

#else

#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <bcm_host.h>
#include "image.h"
#include "vcrfont.h"
#include "teletext.h"

#define ELEMENT_CHANGE_LAYER (1<<0)
#define ELEMENT_CHANGE_OPACITY (1<<1)
#define ELEMENT_CHANGE_DEST_RECT (1<<2)
#define ELEMENT_CHANGE_SRC_RECT (1<<3)
#define ELEMENT_CHANGE_MASK_RESOURCE (1<<4)
#define ELEMENT_CHANGE_TRANSFORM (1<<5)

extern bool loadPNG(const char* f_name, Image *image);

static DISPMANX_DISPLAY_HANDLE_T display;
static DISPMANX_RESOURCE_HANDLE_T resource;
static DISPMANX_ELEMENT_HANDLE_T element;
static uint64_t last_strap_alpha = 0;
#define SCREENX 1920
#define SCREENY 1080
static int screenX, screenY, screenXoffset, screen_y_shift;
#define STRAP_EXT ".strap.png"

static DISPMANX_RESOURCE_HANDLE_T osd_resource;
static DISPMANX_ELEMENT_HANDLE_T osd_element;
#define OSD_W 320
#define OSD_H 256

void load_strap(char *path) {
	// Load image file to structure Image
  char *dotptr = strrchr(path, '.');
  int dotpos = dotptr ? dotptr - path : strlen(path);
  char *strapname = alloca(dotpos + sizeof(STRAP_EXT));
  memcpy(strapname, path, dotpos);
  memcpy(strapname + dotpos, STRAP_EXT, sizeof(STRAP_EXT));
  DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
	VC_RECT_T bmpRect;
	VC_RECT_T zeroRect;
	vc_dispmanx_rect_set(&bmpRect, 0, 0, SCREENX, SCREENY);
	Image image = { 0 };
	if (loadPNG(strapname, &image) == false || image.buffer == NULL ||
		image.width != SCREENX || image.height != SCREENY || image.type != VC_IMAGE_RGBA32)
	{
		fprintf(stderr, "Unable to load %s\n", strapname);
                // TODO clear resource buffer
  	vc_dispmanx_rect_set(&zeroRect, screenX, 0, 1, 1);
    vc_dispmanx_element_change_attributes(update, element, ELEMENT_CHANGE_DEST_RECT, 0, 0, &zeroRect, 0, 0, 0);
	} else {
	// Copy bitmap data to vc
    vc_dispmanx_resource_write_data(
      resource, image.type, image.pitch, image.buffer, &bmpRect);
    // Free bitmap data
    vc_dispmanx_rect_set(&zeroRect, 0, 0, screenX, screenY);
	  vc_dispmanx_element_change_attributes(update, element, ELEMENT_CHANGE_DEST_RECT, 0, 0, &zeroRect, 0, 0, 0);
    if (image.buffer) free(image.buffer);
  }
	int result = vc_dispmanx_update_submit_sync(update); // This waits for vsync?
	assert(result == 0);
}
static uint16_t osdbuf[OSD_W * VCR_FONT_H];
static void put_pixel(int x, int y, int v) {
  osdbuf[x + y * OSD_W + 16] = v ? 0x0f0f : 0;
}
void osd_text(const char * s, int align) {
	VC_RECT_T osdRect2;
  int rendered_width = render_text(s, put_pixel, OSD_W);
  int offset = align ? (OSD_W - rendered_width) / align : 0;
	vc_dispmanx_rect_set(&osdRect2, 0, 0, rendered_width, VCR_FONT_H);
    vc_dispmanx_resource_write_data(
      osd_resource, VC_IMAGE_RGBA16, OSD_W * 2, &osdbuf, &osdRect2);  
}
void osd_text_clear() {
	VC_RECT_T osdRect2;
  memset(osdbuf, 0, sizeof(osdbuf));
 	vc_dispmanx_rect_set(&osdRect2, 0, 0, OSD_W, VCR_FONT_H);
    vc_dispmanx_resource_write_data(
      osd_resource, VC_IMAGE_RGBA16, OSD_W * 2, &osdbuf, &osdRect2);  
}
char *dispmanx_shifted_window(void) {
  static char buf[100];
  if (!screen_y_shift) return 0;
  sprintf(buf, "%d %d %d %d", 0, screen_y_shift, screenX, screenY + screen_y_shift);
  return buf;
}
void dispmanx_init(int shift) {
	int32_t layer = 10;
	u_int32_t displayNumber = 0;
	int result = 0;

	// Init BCM
	bcm_host_init();

	display
		= vc_dispmanx_display_open(displayNumber);
	assert(display != 0);
  TV_DISPLAY_STATE_T tvstate;
  vc_tv_get_display_state(&tvstate);

  DISPMANX_MODEINFO_T display_info;
  int ret = vc_dispmanx_display_get_info(display, &display_info);
  assert(ret == 0);
  screenX = display_info.width;
  screenY = display_info.height;
  if ((tvstate.display.sdtv.mode & SDTV_MODE_FORMAT_MASK) == SDTV_MODE_PAL) {
    printf("PAL detected - initializing teletext\n");
    screen_y_shift = teletext_init();
  }
  int aspectX = 16;
  int aspectY = 9;
  if(tvstate.state & (VC_HDMI_HDMI | VC_HDMI_DVI)) switch (tvstate.display.hdmi.aspect_ratio) {
    case HDMI_ASPECT_4_3:   aspectX = 4;  aspectY = 3;  break;
    case HDMI_ASPECT_14_9:  aspectX = 14; aspectY = 9;  break;
    default:
    case HDMI_ASPECT_16_9:  aspectX = 16; aspectY = 9;  break;
    case HDMI_ASPECT_5_4:   aspectX = 5;  aspectY = 4;  break;
    case HDMI_ASPECT_16_10: aspectX = 16; aspectY = 10; break;
    case HDMI_ASPECT_15_9:  aspectX = 15; aspectY = 9;  break;
    case HDMI_ASPECT_64_27: aspectX = 64; aspectY = 27; break;
  } else switch (tvstate.display.sdtv.display_options.aspect) {
    default:
    case SDTV_ASPECT_4_3:  aspectX = 4, aspectY = 3;  break;
    case SDTV_ASPECT_14_9: aspectX = 14, aspectY = 9; break;
    case SDTV_ASPECT_16_9: aspectX = 16, aspectY = 9; break;
  }
  screenXoffset = (screenX - screenX * aspectY * 16 / 9 / aspectX) / 2;
  int screenOsdXoffset = (screenX - screenX * aspectY * 4 / 3 / aspectX) / 2;

	// Create a resource and copy bitmap to resource
	uint32_t vc_image_ptr = 0;
	resource = vc_dispmanx_resource_create(
		VC_IMAGE_RGBA32, SCREENX, SCREENY, &vc_image_ptr);
	osd_resource = vc_dispmanx_resource_create(
		VC_IMAGE_RGBA16, OSD_W, OSD_H, &vc_image_ptr);

	assert(resource != 0);
	assert(osd_resource != 0);


	// Notify vc that an update is takng place
	DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
	assert(update != 0);

	// Calculate source and destination rect values
	VC_RECT_T srcRect, dstRect, osdSrcRect, osdDstRect;
  int screenOsdX = screenX - 2 * screenOsdXoffset;
	vc_dispmanx_rect_set(&srcRect, 0, 0, SCREENX << 16, SCREENY << 16);
	vc_dispmanx_rect_set(&osdSrcRect, 0, 0, OSD_W << 16, OSD_H << 16);
	vc_dispmanx_rect_set(&dstRect, screenXoffset, screen_y_shift, screenX - 2 * screenXoffset, screenY);
	vc_dispmanx_rect_set(&osdDstRect, screenOsdXoffset + screenOsdX/18, screenY / 18 + screen_y_shift, screenOsdX * 8 / 9, screenY * 8 / 9);

	// Add element to vc
        last_strap_alpha = 0;
	VC_DISPMANX_ALPHA_T alpha = { DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_MIX, 0, 0 };
	element = vc_dispmanx_element_add(
		update, display, layer, &dstRect, resource, &srcRect,
		DISPMANX_PROTECTION_NONE, &alpha, NULL, DISPMANX_NO_ROTATE);
	osd_element = vc_dispmanx_element_add(
		update, display, layer + 1, &osdDstRect, osd_resource, &osdSrcRect,
		DISPMANX_PROTECTION_NONE, NULL, NULL, DISPMANX_NO_ROTATE);

	assert(element != 0);
  

	// Notify vc that update is complete
	result = vc_dispmanx_update_submit_sync(update); // This waits for vsync?
	assert(result == 0);
	//---------------------------------------------------------------------
}
void dispmanx_alpha(int a) {
  int result;
  if (!element) return;
  if (a == last_strap_alpha) return;
  last_strap_alpha = a;
  DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
  vc_dispmanx_element_change_attributes(update, element, ELEMENT_CHANGE_OPACITY, 0, a, 0, 0, 0, 0);
  result = vc_dispmanx_update_submit_sync(update);
}
DISPMANX_RESOURCE_HANDLE_T  bg_resource;
DISPMANX_ELEMENT_HANDLE_T   bg_element;
#define BG_WIDTH 720
#define BG_HEIGHT 480
uint16_t bg_data[BG_WIDTH * BG_HEIGHT];
uint16_t rnd_data[BG_WIDTH * BG_HEIGHT + 0x1000];
void blank_background()
{
  uint16_t rgba = 0xf000;
  DISPMANX_UPDATE_HANDLE_T    update;
  int             ret;
  uint32_t vc_image_ptr;
  VC_IMAGE_TYPE_T type = VC_IMAGE_RGBA16;
  int             layer = - 1;

  VC_RECT_T dst_rect, src_rect;

  for (int i = 0; i < BG_WIDTH * BG_HEIGHT; i++) bg_data[i] = rgba;
  for (int i = 0; i < BG_WIDTH * BG_HEIGHT + 0x1000; i++) rnd_data[i] = 0xf + 0x1110 * (rand() & 0xf);
  bg_resource = vc_dispmanx_resource_create( type, BG_WIDTH /*width*/, BG_HEIGHT /*height*/, &vc_image_ptr );
  assert( bg_resource );

  vc_dispmanx_rect_set( &dst_rect, 0, 0, BG_WIDTH, BG_HEIGHT);

  ret = vc_dispmanx_resource_write_data( bg_resource, type, BG_WIDTH * sizeof(*bg_data), &bg_data, &dst_rect );
  assert(ret == 0);

  vc_dispmanx_rect_set( &src_rect, 0, 0, BG_WIDTH<<16, BG_HEIGHT<<16);
  vc_dispmanx_rect_set( &dst_rect, 0, 0, screenX, screenY);

  update = vc_dispmanx_update_start(0);
  assert(update);

  bg_element = vc_dispmanx_element_add(update, display, layer, &dst_rect, bg_resource, &src_rect,
                                    DISPMANX_PROTECTION_NONE, NULL, NULL, DISPMANX_STEREOSCOPIC_MONO );
  assert(bg_element);

  ret = vc_dispmanx_update_submit_sync( update );
  assert( ret == 0 );
}
void bg_mode(int mode) {
	VC_RECT_T osdRect2;
  uint16_t rgba = mode ? 0x00ff : 0x000f, *src_data;
  if (mode == 2) {
    src_data = rnd_data + (rand() & 0xfff);
  } else {
    for (int i = 0; i < BG_WIDTH * BG_HEIGHT; i++) bg_data[i] = rgba;
    src_data = bg_data;
  }
 	vc_dispmanx_rect_set(&osdRect2, 0, 0, BG_WIDTH, BG_HEIGHT);
    vc_dispmanx_resource_write_data(
      bg_resource, VC_IMAGE_ARGB8888, BG_WIDTH * sizeof(*src_data), src_data, &osdRect2);  
}

void dispmanx_close() {
        int result;
        teletext_close();
	DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
	if (element) result = vc_dispmanx_element_remove(update, element);
	if (bg_element) result = vc_dispmanx_element_remove(update, bg_element);
	if (osd_element) result = vc_dispmanx_element_remove(update, osd_element);
	result = vc_dispmanx_update_submit_sync(update);
        if (resource) {
	result = vc_dispmanx_resource_delete(resource);
	assert(result == 0);
        }
        if (bg_resource) {
	result = vc_dispmanx_resource_delete(bg_resource);
	assert(result == 0);
        }
        if (osd_resource) {
	result = vc_dispmanx_resource_delete(osd_resource);
	assert(result == 0);
        }
        if (display) {
	result = vc_dispmanx_display_close(display);
	assert(result == 0);
        }
}

#endif
