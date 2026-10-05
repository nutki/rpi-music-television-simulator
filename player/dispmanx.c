#if defined(USE_LIBVLC)

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <time.h>
#include <poll.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include "image.h"
#include "vcrfont.h"
#include "teletext.h"
#include "preview_shm.h"
#include "dispmanx.h"
#include <libyuv.h>

extern bool loadPNG(const char *f_name, Image *image);

#define COMPOSITE_FRAME_W 822
#define COMPOSITE_FRAME_H 576
#define OSD_WIDTH 320
#define OSD_HEIGHT VCR_FONT_H
#define OSD_TARGET_WIDTH 700
#define OSD_OFFSET_X ((COMPOSITE_FRAME_W - OSD_TARGET_WIDTH)/2)
#define OSD_TARGET_HEIGHT 48
#define OSD_OFFSET_Y 50
#define TELETEXT_OFFSET_Y 32
#define COMPOSITE_FRAME_BYTES (COMPOSITE_FRAME_W * COMPOSITE_FRAME_H * 4U)
static uint8_t strap_premultiplied[COMPOSITE_FRAME_W * COMPOSITE_FRAME_H * 4];

struct dumb_buffer {
    void *mmap, *mmap_u, *mmap_v;
    uint32_t w, h;
    uint32_t handle;
    uint32_t fb_id;
    uint32_t size;
    uint32_t pitch, pitchuv;
    uint32_t format;
};

struct drm_vec_plane {
    int fd;
    uint32_t crtc_id;
    struct dumb_buffer fbs[2];
    int active_fb_index;
    uint8_t *strap_pixels;
    int strap_alpha;
    uint8_t *osd_source;
    uint8_t *osd_pixels;
    int osd_active;
};

struct drm_hdmi_planes {
    int fd;
    int active;
    uint32_t crtc_id;
    struct dumb_buffer fb_buffer;
    uint32_t video_plane_id;
    struct dumb_buffer video_buffers[3];
    int active_video_buffer_index;
    uint32_t strap_plane_id;
    struct dumb_buffer strap_buffer;
    uint32_t osd_plane_id;
    struct dumb_buffer osd_buffer;
    struct dumb_buffer bg_buffer;
    struct dumb_buffer noise_buffer;
    struct {
        uint32_t crtc_id;
        uint32_t fb_id;
        uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
        uint32_t src_x, src_y, src_w, src_h;
        uint32_t alpha, blend;
    } prop;
};

static struct drm_vec_plane drm_vec_plane = {
    .fd = -1,
    .crtc_id = 0,
    .fbs = {{0}, {0}},
    .active_fb_index = 0,
    .strap_pixels = NULL,
    .strap_alpha = -1,
    .osd_source = NULL,
    .osd_pixels = NULL,
    .osd_active = 0,
};

static struct drm_hdmi_planes drm_hdmi_planes = {
    .fd = -1,
};

struct display_frame {
    uint8_t *pixels;
    unsigned width;
    unsigned height;
    int busy;
    int from_vlc;
};

static struct display_frame display_frame;
static pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t display_buffer_free = PTHREAD_COND_INITIALIZER;
static pthread_t display_thread, display_hdmi_thread;
static int display_thread_running;
static int display_thread_stop;
static uint64_t vlc_submitted;
static uint64_t display_flips;

static void destroy_dumb_fb(int fd, struct dumb_buffer *b) {
    if (b->mmap) {
        munmap(b->mmap, b->size);
        b->mmap = b->mmap_u = b->mmap_v = 0;
    }
    if (b->fb_id != 0) {
        drmModeRmFB(fd, b->fb_id);
        b->fb_id = 0;
    }
    if (b->handle != 0) {
        struct drm_mode_destroy_dumb destroy = { .handle = b->handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        b->handle = 0;
    }
}
static int create_dumb_fb(int fd, uint32_t w, uint32_t h, uint32_t format, struct dumb_buffer *out) {
    if (w == out->w && h == out->h && format == out->format && out->fb_id) return 0;
    destroy_dumb_fb(fd, out);
    out->w = w;
    out->h = h;
    out->format = format;
    int is_i420 = format == DRM_FORMAT_YUV420 || format == DRM_FORMAT_YVU420 || format == DRM_FORMAT_NV12;
    int is_nv12 = format == DRM_FORMAT_NV12;
    if (is_i420 && ((w & 1U) || (h & 1U) || h > UINT32_MAX / 2U)) {
        fprintf(stderr, "I420 framebuffer dimensions must be even and fit the dumb-buffer height\n");
        return -1;
    }

    uint32_t bpp = is_i420 ? 8 : (format == DRM_FORMAT_RGB565 ? 16 : 32);

    struct drm_mode_create_dumb create = {
        .width = w,
        .height = is_i420 ? h + h / 2U : h,
        .bpp = bpp,
        .flags = 0,
    };

    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        fprintf(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed: %s\n", strerror(errno));
        return -1;
    }
    out->handle = create.handle;
    out->size = create.size;
    out->pitch = out->pitchuv = create.pitch;

    uint32_t fb_id = 0;
    uint32_t handles[4] = { create.handle, 0, 0, 0 };
    uint32_t pitches[4] = { create.pitch, 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    if (is_nv12) {
        pitches[1] = out->pitchuv = create.pitch;
        handles[1] = create.handle;
        offsets[1] = create.pitch * h;
    } else if (is_i420) {
        pitches[1] = pitches[2] = out->pitchuv = create.pitch / 2U;
        handles[1] = handles[2] = create.handle;
        offsets[1] = offsets[2] = create.pitch * h;
        offsets[2] += pitches[1] * (h / 2U);
    }

    struct drm_mode_map_dumb map = { .handle = create.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        fprintf(stderr, "DRM_IOCTL_MODE_MAP_DUMB failed: %s\n", strerror(errno));
        destroy_dumb_fb(fd, out);
        return -1;
    }

    void *ptr = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        destroy_dumb_fb(fd, out);
        return -1;
    }
    out->mmap = ptr;
    out->mmap_u = (int8_t*)out->mmap + offsets[1];
    out->mmap_v = (int8_t*)out->mmap + offsets[2];

    int ret = drmModeAddFB2(fd, w, h, format, handles, pitches, offsets, &fb_id, 0);
    if (ret != 0) {
        fprintf(stderr, "drmModeAddFB2 failed: %d\n", ret);
        destroy_dumb_fb(fd, out);
        return -1;
    }
    out->fb_id = fb_id;

    return 0;
}

static int drm_vec_plane_prepare_buffer(int index);
static int drm_vec_plane_acquire(void) {
    if (drm_vec_plane.fd >= 0) {
        return 0;
    }

    const char *device = "/dev/dri/by-path/platform-1f00144000.vec-card";
    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "drm-rp1-vec: failed to open %s: %s\n", device, strerror(errno));
        return -1;
    }
    drmModeResPtr resources = drmModeGetResources(fd);
    if (!resources) {
        fprintf(stderr, "drm-rp1-vec: drmModeGetResources failed on %s\n", device);
        close(fd);
        return -1;
    }
    uint32_t connector_id = resources->connectors[0];
    uint32_t crtc_id = resources->crtcs[0];
    drmModeFreeResources(resources);

    int mode_index = -1;
    drmModeConnectorPtr connector = drmModeGetConnector(fd, connector_id);
    if (!connector) {
        fprintf(stderr, "drm-rp1-vec: no connector found\n");
        close(fd);
        return -1;
    }
    for (int i = 0; i < connector->count_modes; ++i) if (!strcmp(connector->modes[i].name, "720x576i")) {
        mode_index = i;
    }
    drmModeFreeConnector(connector);
    if (mode_index < 0) {
        fprintf(stderr, "drm-rp1-vec: connected output has no 720x576i mode\n");
        close(fd);
        return -1;
    }
    drmModeModeInfo custom_mode = connector->modes[mode_index];
    custom_mode.vdisplay += TELETEXT_OFFSET_Y;
    custom_mode.vsync_start += TELETEXT_OFFSET_Y;
    custom_mode.vsync_end += TELETEXT_OFFSET_Y;
    custom_mode.clock = 15429;
    custom_mode.htotal = 987;
    custom_mode.hdisplay = COMPOSITE_FRAME_W;
    custom_mode.hsync_start = 836;
    custom_mode.hsync_end = 909;


    drm_vec_plane.fd = fd;
    drm_vec_plane.crtc_id = crtc_id;


    if (drm_vec_plane_prepare_buffer(0) != 0) {
        fprintf(stderr, "drm-rp1-vec: failed to create initial fb\n");
        close(fd);
        return -1;
    };

    memset(drm_vec_plane.fbs[0].mmap, 0x55, drm_vec_plane.fbs[0].size);
    if (drmModeSetCrtc(fd, crtc_id, drm_vec_plane.fbs[0].fb_id, 0, 0, &connector_id, 1,
                       &custom_mode) != 0) {
        fprintf(stderr, "drm-rp1-vec: failed to set 720x576i CRTC mode\n");
        destroy_dumb_fb(fd, &drm_vec_plane.fbs[0]);
        close(fd);
        return -1;
    }

    printf("drm-rp1-vec: acquired rtc=%u connector=%u mode=%s\n",
           drm_vec_plane.crtc_id,
           connector_id,
           custom_mode.name);

    return 0;
}

static int pick_1080p_59_94_mode(drmModeConnector *connector, drmModeModeInfo *out_mode) {
    for (int i = 0; i < connector->count_modes; ++i) {
        drmModeModeInfo *mode = &connector->modes[i];
        if (mode->hdisplay == HDMI_WIDTH && mode->vdisplay == HDMI_HEIGHT) {
            *out_mode = *mode;
            return i;
        }
    }

    return -1;
}
uint64_t get_plane_type(int fd, uint32_t plane_id) {
    uint64_t plane_type = -1;

    // 1. Get all properties attached to this plane object
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) return -1;

    // 2. Loop through the properties to find the one named "type"
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[i]);
        if (!prop) continue;

        if (strcmp(prop->name, "type") == 0) {
            // The value is stored in the corresponding values array
            plane_type = props->prop_values[i];
            drmModeFreeProperty(prop);
            break;
        }
        drmModeFreeProperty(prop);
    }

    drmModeFreeObjectProperties(props);
    return plane_type;
}
static uint32_t find_plane(int fd, drmModePlaneRes *plane_res) {
    static uint32_t skip_plane_id = 0;
    for (uint32_t i = 0; i < plane_res->count_planes; ++i) {
        uint32_t plane_id = plane_res->planes[i];
        if (get_plane_type(fd, plane_id) == DRM_PLANE_TYPE_OVERLAY && plane_id > skip_plane_id) {
            skip_plane_id = plane_id;
            return plane_id;
        }
    }
    return 0;
}
static uint32_t get_plane_prop_id(int fd, uint32_t object_id, const char *name) {
    drmModeObjectProperties *props = drmModeObjectGetProperties(fd, object_id, DRM_MODE_OBJECT_PLANE);
    uint32_t id;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
        int match = !strcmp(prop->name, name);
        drmModeFreeProperty(prop);
        if (match) {
            id = props->props[i];
            break;
        }
    }
    drmModeFreeObjectProperties(props);
    if (!id) printf("failed prop %s\n", name);
    return id;
}

static int drm_hdmi_planes_acquire(void) {
    if (drm_hdmi_planes.fd >= 0) {
        return 0;
    }

    int fd = open("/dev/dri/by-path/platform-axi:gpu-card", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Unable to open HDMI: %s\n", strerror(errno));
        return 1;
    }
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

    drmModeConnector *connector = NULL;
    uint32_t connector_id = 0;
    uint32_t crtc_id = 0;
    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        fprintf(stderr, "drmModeGetResources failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    for (int i = 0; i < res->count_connectors; ++i) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (!c) {
            continue;
        }
        if (c->connection == DRM_MODE_CONNECTED && c->connector_type == DRM_MODE_CONNECTOR_HDMIA) {
            connector = c;
            connector_id = c->connector_id;
            break;
        }

        drmModeFreeConnector(c);
    }

    if (!connector) {
        fprintf(stderr, "No HDMI-A connector found\n");
        drmModeFreeResources(res);
        close(fd);
        return 1;
    }

    if (connector->encoder_id != 0) {
        drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoder_id);
        if (encoder) {
            crtc_id = encoder->crtc_id;
            drmModeFreeEncoder(encoder);
        }
    }

    if (crtc_id == 0 && res->count_crtcs > 0) {
        crtc_id = res->crtcs[0];
    }
    drmModeFreeResources(res);

    drmModeModeInfo mode = {0};
    for (int i = 0; i < connector->count_modes; ++i) {
        drmModeModeInfo *cmode = &connector->modes[i];
        if (cmode->hdisplay == HDMI_WIDTH && cmode->vdisplay == HDMI_HEIGHT) {
            mode = *cmode;
            break;
        }
    }
    drmModeFreeConnector(connector);
    if (!mode.hdisplay || !mode.vdisplay) {
        fprintf(stderr, "Could not find a 1920x1080 mode on the HDMI connector\n");
        close(fd);
        return 1;
    }
    drmModePlaneRes *plane_res = drmModeGetPlaneResources(fd);
    if (!plane_res) {
        fprintf(stderr, "drmModeGetPlaneResources failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    uint32_t video_plane = find_plane(fd, plane_res);
    uint32_t strap_plane = find_plane(fd, plane_res);
    uint32_t osd_plane = find_plane(fd, plane_res);
    drmModeFreePlaneResources(plane_res);
    if (!video_plane || !strap_plane || !osd_plane) {
        fprintf(stderr, "Could not find suitable planes for this CRTC\n");
        close(fd);
        return 1;
    }
    drm_hdmi_planes.video_plane_id = video_plane;
    drm_hdmi_planes.strap_plane_id = strap_plane;
    drm_hdmi_planes.osd_plane_id = osd_plane;
    drm_hdmi_planes.prop.crtc_id = get_plane_prop_id(fd, video_plane, "CRTC_ID");
    drm_hdmi_planes.prop.fb_id = get_plane_prop_id(fd, video_plane, "FB_ID");
    drm_hdmi_planes.prop.crtc_x = get_plane_prop_id(fd, video_plane, "CRTC_X");
    drm_hdmi_planes.prop.crtc_y = get_plane_prop_id(fd, video_plane, "CRTC_Y");
    drm_hdmi_planes.prop.crtc_w = get_plane_prop_id(fd, video_plane, "CRTC_W");
    drm_hdmi_planes.prop.crtc_h = get_plane_prop_id(fd, video_plane, "CRTC_H");
    drm_hdmi_planes.prop.src_x = get_plane_prop_id(fd, video_plane, "SRC_X");
    drm_hdmi_planes.prop.src_y = get_plane_prop_id(fd, video_plane, "SRC_Y");
    drm_hdmi_planes.prop.src_w = get_plane_prop_id(fd, video_plane, "SRC_W");
    drm_hdmi_planes.prop.src_h = get_plane_prop_id(fd, video_plane, "SRC_H");
    drm_hdmi_planes.prop.alpha = get_plane_prop_id(fd, strap_plane, "alpha");
    drm_hdmi_planes.prop.blend = get_plane_prop_id(fd, strap_plane, "pixel blend mode");

    if (create_dumb_fb(fd, HDMI_WIDTH, HDMI_HEIGHT, DRM_FORMAT_XRGB8888, &drm_hdmi_planes.fb_buffer) < 0) {
        fprintf(stderr, "Failed to allocate background framebuffers\n");
        close(fd);
        return 1;
    }

    if (drmModeSetCrtc(fd, crtc_id, drm_hdmi_planes.fb_buffer.fb_id, 0, 0, &connector_id, 1, &mode) != 0) {
        fprintf(stderr, "drmModeSetCrtc failed: %s\n", strerror(errno));
        destroy_dumb_fb(fd, &drm_hdmi_planes.fb_buffer);
        close(fd);
        return 1;
    }

    if (create_dumb_fb(fd, HDMI_WIDTH, HDMI_HEIGHT, DRM_FORMAT_ARGB8888, &drm_hdmi_planes.strap_buffer) < 0) {
        fprintf(stderr, "Failed to allocate background framebuffers\n");
        destroy_dumb_fb(fd, &drm_hdmi_planes.fb_buffer);
        close(fd);
        return 1;
    }
    if (drmModeSetPlane(fd, strap_plane, crtc_id, drm_hdmi_planes.strap_buffer.fb_id, 0,
        0, 0, HDMI_WIDTH, HDMI_HEIGHT, 0, 0, HDMI_WIDTH << 16, HDMI_HEIGHT << 16) != 0) {
        fprintf(stderr, "drmModeSetPlane failed: %s\n", strerror(errno));
        destroy_dumb_fb(fd, &drm_hdmi_planes.fb_buffer);
        destroy_dumb_fb(fd, &drm_hdmi_planes.strap_buffer);
        close(fd);
        return 1;
    }
    if (create_dumb_fb(fd, OSD_WIDTH, OSD_HEIGHT, DRM_FORMAT_ARGB8888, &drm_hdmi_planes.osd_buffer) < 0) {
        fprintf(stderr, "Failed to allocate background framebuffers\n");
        destroy_dumb_fb(fd, &drm_hdmi_planes.fb_buffer);
        destroy_dumb_fb(fd, &drm_hdmi_planes.strap_buffer);
        close(fd);
        return 1;
    }
    if (drmModeSetPlane(fd, osd_plane, crtc_id, drm_hdmi_planes.osd_buffer.fb_id, 0,
        32, 32, HDMI_WIDTH - 64, HDMI_HEIGHT/10, 0, 0, OSD_WIDTH << 16, OSD_HEIGHT << 16) != 0) {
        fprintf(stderr, "drmModeSetPlane failed: %s\n", strerror(errno));
        destroy_dumb_fb(fd, &drm_hdmi_planes.fb_buffer);
        destroy_dumb_fb(fd, &drm_hdmi_planes.strap_buffer);
        destroy_dumb_fb(fd, &drm_hdmi_planes.osd_buffer);
        close(fd);
        return 1;
    }
    if (create_dumb_fb(fd, HDMI_WIDTH/2, HDMI_HEIGHT/2, DRM_FORMAT_YUV420, &drm_hdmi_planes.noise_buffer) < 0) {
        fprintf(stderr, "Failed to allocate background framebuffers\n");
        destroy_dumb_fb(fd, &drm_hdmi_planes.fb_buffer);
        destroy_dumb_fb(fd, &drm_hdmi_planes.strap_buffer);
        destroy_dumb_fb(fd, &drm_hdmi_planes.osd_buffer);
        close(fd);
        return 1;
    }
    memset(drm_hdmi_planes.noise_buffer.mmap, 128, drm_hdmi_planes.noise_buffer.size);
    for (int i = 0; i < drm_hdmi_planes.noise_buffer.w * drm_hdmi_planes.noise_buffer.h; i++) ((uint8_t*)drm_hdmi_planes.noise_buffer.mmap)[i] = rand() & 255;
    if (create_dumb_fb(fd, 1, 1, DRM_FORMAT_XRGB8888, &drm_hdmi_planes.bg_buffer) < 0) {
        fprintf(stderr, "Failed to allocate background framebuffers\n");
        destroy_dumb_fb(fd, &drm_hdmi_planes.fb_buffer);
        destroy_dumb_fb(fd, &drm_hdmi_planes.strap_buffer);
        destroy_dumb_fb(fd, &drm_hdmi_planes.osd_buffer);
        destroy_dumb_fb(fd, &drm_hdmi_planes.noise_buffer);
        close(fd);
        return 1;
    }
    drm_hdmi_planes.fd = fd;
    drm_hdmi_planes.crtc_id = crtc_id;
    drm_hdmi_planes.active = 1;

    fprintf(stdout, "Using HDMI-A connector %u, CRTC %u, mode %s %ux%u @ %d (%5.3lf) Hz\n",
            connector_id, crtc_id, mode.name, mode.hdisplay, mode.vdisplay, mode.vrefresh, mode.clock*1000./mode.htotal/mode.vtotal);
}
static int drm_vec_plane_prepare_buffer(int index) {
    if (drm_vec_plane.fbs[index].fb_id != 0) {
        return 0;
    }
    return create_dumb_fb(drm_vec_plane.fd, COMPOSITE_FRAME_W, COMPOSITE_FRAME_H + TELETEXT_OFFSET_Y, DRM_FORMAT_XRGB8888, &drm_vec_plane.fbs[index]);
}

static int teletext_bit(uint8_t *packet, int bit) {
    if (bit >= 0 && bit < 32) {
        return 1 & (0x27555500 >> bit);
    } else if (bit < 8 * 46) {
        return 1 & (packet[(bit-32) >> 3] >> (bit & 7));
    }
    return 0;
}
static void overlay_teletext(uint8_t *argb, int field) {
    int pitch = COMPOSITE_FRAME_W * 4;
    static uint8_t packets[32][42];
    int cnt = 0;
    for (int y = 0; y < TELETEXT_OFFSET_Y; y++) {
        uint32_t *row = (uint32_t *)(argb + y * pitch);
        if (field == y % 2) {
            teletext_get_packet(packets[y]);
            cnt++;
        }
        for (int x = 0; x < COMPOSITE_FRAME_W; x++) {
            // ratio = pixel clock = 108Mhz/7 / teletext data clock  = 6.9375Mhz = 2.2239...
            // offset (real data (clock runin) starts at 8)
            int source_x = x/2.223938223938224 + 6;
            row[x] = teletext_bit(packets[y], source_x) ? 0xffffffff : 0xff000000;
        }
    }
    teletext_request_packets(cnt);
}

static int complete = 0;
static void page_flip_handler(int fd, unsigned int frame, unsigned int seconds, unsigned int useconds, void *data) {
    (void)fd;
    (void)frame;
    (void)seconds;
    (void)useconds;
    complete = 1;
}
static void wait_for_flip(int fd) {
    drmEventContext event = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .page_flip_handler = page_flip_handler,
    };
    struct pollfd pollfd = {fd, POLLIN, 0};
    complete = 0;
    while (!complete) {
        if (poll(&pollfd, 1, -1) < 0) perror("poll DRM page flip");
        if (drmHandleEvent(fd, &event) < 0) perror("drmHandleEvent");
    }
}
static int drm_vec_plane_update_fb(const uint8_t *argb, unsigned width, unsigned height, int from_vlc) {
    if (!argb || width == 0 || height == 0) {
        return -1;
    }
    if (width != COMPOSITE_FRAME_W || height != COMPOSITE_FRAME_H) {
        printf("Bad size %dx%d\n", width, height);
        return -1;
    }

    uint32_t pitch = COMPOSITE_FRAME_W * 4U;

    int target_index = drm_vec_plane.active_fb_index ^ 1;
    if (drm_vec_plane_prepare_buffer(target_index) < 0) return -1;

    uint8_t *pixels = drm_vec_plane.fbs[target_index].mmap;
    uint8_t *frame_pixels = pixels + TELETEXT_OFFSET_Y * pitch;

    if (drm_vec_plane.strap_alpha > 0 && from_vlc) {
        int blend_ret = ARGBBlend(strap_premultiplied,
                                  COMPOSITE_FRAME_W * 4,
                                  argb, pitch,
                                  frame_pixels, pitch,
                                  width, height);
        if (blend_ret != 0) {
            fprintf(stderr, "drm-rp1-vec: strap blend failed: %d\n", blend_ret);
            return -1;
        }
    } else {
        memcpy(frame_pixels, argb, height * pitch);
    }

    if (drm_vec_plane.osd_active && drm_vec_plane.osd_pixels) {
        int blend_ret = ARGBBlend(drm_vec_plane.osd_pixels,
                                  OSD_TARGET_WIDTH * 4,
                                  frame_pixels + pitch * OSD_OFFSET_Y + OSD_OFFSET_X * 4, pitch,
                                  frame_pixels + pitch * OSD_OFFSET_Y + OSD_OFFSET_X * 4, pitch,
                                  OSD_TARGET_WIDTH, OSD_TARGET_HEIGHT);
        if (blend_ret != 0) {
            fprintf(stderr, "drm-rp1-vec: OSD blend failed: %d\n", blend_ret);
            return -1;
        }
    }
    overlay_teletext(pixels, target_index);

    int ret = drmModePageFlip(drm_vec_plane.fd, drm_vec_plane.crtc_id,
                              drm_vec_plane.fbs[target_index].fb_id,
                        DRM_MODE_PAGE_FLIP_EVENT, 0);
    if (ret != 0) {
        fprintf(stderr, "drm-rp1-vec: drmModePageFlip failed: %d\n", ret);
        return -1;
    }

    drm_vec_plane.active_fb_index = target_index;

    // printf("drm-rp1-vec: uploaded %ux%u ARGB frame via back buffer %u\n",
    //        out_w, out_h, drm_vec_plane.fb_ids[target_index]);
    return 0;
}

static void *drm_vec_display_loop(void *unused) {
    (void)unused;
    struct timespec last_report;
    clock_gettime(CLOCK_MONOTONIC, &last_report);
    uint64_t report_flips = 0;
    uint64_t report_vlc = 0;

    for (;;) {
        int can_wait = 0;
        pthread_mutex_lock(&display_mutex);
        if (display_thread_stop) {
            pthread_mutex_unlock(&display_mutex);
            break;
        }

        display_frame.busy = 1;
        int ret = drm_vec_plane_update_fb(display_frame.pixels,
                                            display_frame.width,
                                            display_frame.height,
                                            display_frame.from_vlc);
        if (ret == 0) {
            display_flips++;
            report_flips++;
            can_wait = 1;
        }
        display_frame.busy = 0;
        pthread_cond_signal(&display_buffer_free);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - last_report.tv_sec) +
                         (double)(now.tv_nsec - last_report.tv_nsec) / 1000000000.0;
        if (elapsed >= 1.0) {
                        report_vlc = vlc_submitted;
                        printf("drm-rp1-vec: vlc fps=%.2f output fps=%.2f flips=%llu\n",
                                     (double)report_vlc / elapsed,
                   (double)report_flips / elapsed,
                 (unsigned long long)display_flips);
             vlc_submitted = 0;
            report_flips = 0;
            last_report = now;
        }
        pthread_mutex_unlock(&display_mutex);
       if (can_wait) wait_for_flip(drm_vec_plane.fd); else usleep(5000);
    }
    return NULL;
}
void hdmi_load_strap(uint8_t *ptr) {
   if (!drm_hdmi_planes.active) return;
   memcpy(drm_hdmi_planes.strap_buffer.mmap, ptr, HDMI_HEIGHT * HDMI_WIDTH * 4);
}
static struct { int sw, sh, sx, sy, dw, dh, dx, dy; } hdg;
void hdmi_set_geometry(int sw, int sh, int sx, int sy, int dw, int dh, int dx, int dy) {
    hdg.sw = sw; hdg.sh = sh; hdg.sx = sx; hdg.sy = sy;
    hdg.dw = dw; hdg.dh = dh; hdg.dx = dx; hdg.dy = dy;
}
int64_t get_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
int8_t *hdmi_get_frame(int w, int h, int planes) {
    if (!drm_hdmi_planes.active) {
        static int8_t *dummy_frame = 0;
        static uint32_t frame_size = 0;
        uint32_t size = w * h * 3 / 2;
        if (size > frame_size) {
            free(dummy_frame);
            frame_size = size;
            dummy_frame = malloc(size);
        }
        return dummy_frame;
    }
    drm_hdmi_planes.active_video_buffer_index = (drm_hdmi_planes.active_video_buffer_index + 1) % 3;
    if (create_dumb_fb(drm_hdmi_planes.fd, w, h, planes == 3 ? DRM_FORMAT_YUV420 : DRM_FORMAT_NV12, &drm_hdmi_planes.video_buffers[drm_hdmi_planes.active_video_buffer_index]) < 0) return 0;
    return drm_hdmi_planes.video_buffers[drm_hdmi_planes.active_video_buffer_index].mmap;
}
static void hdmi_atomic_commit(uint32_t fb_id,
    uint32_t crtc_x, uint32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h,
    uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h,
    uint32_t alpha, uint32_t flags) {
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req, drm_hdmi_planes.strap_plane_id, drm_hdmi_planes.prop.alpha, alpha);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.strap_plane_id, drm_hdmi_planes.prop.blend, 1);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.fb_id,   fb_id);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.crtc_id, drm_hdmi_planes.crtc_id);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.crtc_x,  crtc_x);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.crtc_y,  crtc_y);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.crtc_w,  crtc_w);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.crtc_h,  crtc_h);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.src_x,   src_x << 16);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.src_y,   src_y << 16);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.src_w,   src_w << 16);
    drmModeAtomicAddProperty(req, drm_hdmi_planes.video_plane_id, drm_hdmi_planes.prop.src_h,   src_h << 16);
    if (drmModeAtomicCommit(drm_hdmi_planes.fd, req, flags, NULL)) {
        perror("drmModeAtomicCommit");
    }
    drmModeAtomicFree(req);
}
void hdmi_commit_frame() {
    if (!drm_hdmi_planes.active) return;
    struct dumb_buffer *cb = &drm_hdmi_planes.video_buffers[drm_hdmi_planes.active_video_buffer_index];
    uint32_t alpha =  drm_vec_plane.strap_alpha > 0 ?  drm_vec_plane.strap_alpha << 8 : 0;
    hdmi_atomic_commit(cb->fb_id, hdg.dx, hdg.dy, hdg.dw, hdg.dh, hdg.sx, hdg.sy, hdg.sw, hdg.sh, alpha, DRM_MODE_ATOMIC_NONBLOCK);
}
void hdmi_bg_mode(int mode) {
    if (!drm_hdmi_planes.active) return;
    if (mode == 2) {
        int w = drm_hdmi_planes.noise_buffer.w / 2, h = drm_hdmi_planes.noise_buffer.h / 2;
        hdmi_atomic_commit(drm_hdmi_planes.noise_buffer.fb_id, 0, 0, HDMI_WIDTH, HDMI_HEIGHT, rand() % w, rand() % h, w, h, 0, 0);
    } else {
        *(uint32_t*)drm_hdmi_planes.bg_buffer.mmap = mode ? 0xff0000ff : 0;
        hdmi_atomic_commit(drm_hdmi_planes.bg_buffer.fb_id, 0, 0, HDMI_WIDTH, HDMI_HEIGHT, 0, 0, 1, 1, 0, 0);
    }
}

static int drm_vec_submit_frame(const uint8_t *argb, unsigned width,
                                unsigned height, int from_vlc) {
    if (!argb || width == 0 || height == 0 ||
        (size_t)width * height * 4U > COMPOSITE_FRAME_BYTES) {
        return -1;
    }

    pthread_mutex_lock(&display_mutex);
    while (!display_thread_stop && display_frame.busy) {
        pthread_cond_wait(&display_buffer_free, &display_mutex);
    }
    if (display_thread_stop || !display_frame.pixels) {
        pthread_mutex_unlock(&display_mutex);
        return -1;
    }
    memcpy(display_frame.pixels, argb, (size_t)width * height * 4U);
    display_frame.width = width;
    display_frame.height = height;
    display_frame.from_vlc = from_vlc;
    if (from_vlc) {
        vlc_submitted++;
    }
    pthread_mutex_unlock(&display_mutex);
    return 0;
}

void load_strap(char *path) {
    if (!path) {
        return;
    }

    char *dot = strrchr(path, '.');
    size_t base_length = dot ? (size_t)(dot - path) : strlen(path);
    const char suffix[] = ".strap.png";
    char *strap_path = malloc(base_length + sizeof(suffix));
    if (!strap_path) {
        fprintf(stderr, "drm-rp1-vec: strap path allocation failed\n");
        return;
    }
    memcpy(strap_path, path, base_length);
    memcpy(strap_path + base_length, suffix, sizeof(suffix));

    Image image = {0};
    if (!loadPNG(strap_path, &image) || !image.buffer ||
        image.width <= 0 || image.height <= 0 ||
        image.type != VC_IMAGE_RGBA32) {
        fprintf(stderr, "drm-rp1-vec: unable to load strap %s\n", strap_path);
        free(strap_path);
        free(image.buffer);
        return;
    }
    free(strap_path);

    hdmi_load_strap(image.buffer);
    uint8_t *scaled_argb = malloc((size_t)COMPOSITE_FRAME_W * COMPOSITE_FRAME_H * 4U);
    if (!scaled_argb) {
        fprintf(stderr, "drm-rp1-vec: strap pixel allocation failed\n");
        free(image.buffer);
        return;
    }
    int ret = ARGBScale(image.buffer + image.width / 16 * 8, image.pitch, image.width / 4 * 3, image.height,
                        scaled_argb, COMPOSITE_FRAME_W * 4, COMPOSITE_FRAME_W, COMPOSITE_FRAME_H,
                        kFilterBilinear);
    free(image.buffer);
    if (ret != 0) {
        fprintf(stderr, "drm-rp1-vec: strap scaling failed: %d\n", ret);
        free(scaled_argb);
        return;
    }
    ARGBAttenuate(scaled_argb, COMPOSITE_FRAME_W * 4,
                  scaled_argb, COMPOSITE_FRAME_W * 4,
                  COMPOSITE_FRAME_W, COMPOSITE_FRAME_H);

    pthread_mutex_lock(&display_mutex);
    free(drm_vec_plane.strap_pixels);
    drm_vec_plane.strap_pixels = scaled_argb;
    drm_vec_plane.strap_alpha = -1;
    pthread_mutex_unlock(&display_mutex);
    printf("drm-rp1-vec: loaded strap %dx%d scaled to %dx%d\n",
           image.width, image.height, COMPOSITE_FRAME_W, COMPOSITE_FRAME_H);
}

void dispmanx_init(void) {
    if (drm_vec_plane_acquire() < 0) {
        return;
    }
    if (drm_hdmi_planes_acquire() < 0) {
        printf("HDMI init failed\n");
        return;
    }
    display_frame.pixels = malloc(COMPOSITE_FRAME_BYTES);
    if (!display_frame.pixels) {
        fprintf(stderr, "drm-rp1-vec: display buffer allocation failed\n");
        return;
    }
    display_frame.width = COMPOSITE_FRAME_W;
    display_frame.height = COMPOSITE_FRAME_H;
    display_frame.busy = 0;
    display_frame.from_vlc = 0;
    display_thread_stop = 0;
    if (pthread_create(&display_thread, NULL, drm_vec_display_loop, NULL) != 0) {
        fprintf(stderr, "drm-rp1-vec: display thread creation failed\n");
        free(display_frame.pixels);
        display_frame.pixels = NULL;
        return;
    }
    display_thread_running = 1;
}

void dispmanx_display_argb(const uint8_t *argb, unsigned width, unsigned height) {
    drm_vec_submit_frame(argb, width, height, 1);
}

void dispmanx_alpha(int a) {
    pthread_mutex_lock(&display_mutex);
    if (a == drm_vec_plane.strap_alpha) {
        pthread_mutex_unlock(&display_mutex);
        return;
    }
    drm_vec_plane.strap_alpha = a;
    if (!drm_vec_plane.strap_pixels) {
        pthread_mutex_unlock(&display_mutex);
        return;
    }
    uint32_t alpha_mult = a;
    alpha_mult = alpha_mult | (alpha_mult << 16);
    alpha_mult = alpha_mult | (alpha_mult << 8);
    ARGBShade(drm_vec_plane.strap_pixels, COMPOSITE_FRAME_W * 4,
              strap_premultiplied, COMPOSITE_FRAME_W * 4,
              COMPOSITE_FRAME_W, COMPOSITE_FRAME_H, alpha_mult);
    pthread_mutex_unlock(&display_mutex);
}

uint32_t black_bg[COMPOSITE_FRAME_W*COMPOSITE_FRAME_H], blue_bg[COMPOSITE_FRAME_W*COMPOSITE_FRAME_H], random_bg[COMPOSITE_FRAME_W*COMPOSITE_FRAME_H + 0xfff];
uint8_t preview_black_bg[86*48], preview_blue_bg[86*48], preview_random_bg[86*48+0xff];

void blank_background(void) {
    for(int i = 0; i < COMPOSITE_FRAME_W*COMPOSITE_FRAME_H; i++) blue_bg[i] = 0xFF0000FF;
    for(int i = 0; i < COMPOSITE_FRAME_W*COMPOSITE_FRAME_H + 0xfff; i++) random_bg[i] = 0x01010101 * (rand() & 0xff);
    for(int i = 0; i < 86*48; i++) preview_blue_bg[i] = 0x80;
    for(int i = 0; i < 86*48 + 0xff; i++) preview_random_bg[i] = rand() & 0xff;
}

void dispmanx_close(void) {
    pthread_mutex_lock(&display_mutex);
    display_thread_stop = 1;
    pthread_cond_broadcast(&display_buffer_free);
    pthread_mutex_unlock(&display_mutex);
    if (display_thread_running) {
        pthread_join(display_thread, NULL);
        display_thread_running = 0;
    }
    free(display_frame.pixels);
    display_frame.pixels = NULL;
    display_frame.busy = 0;
    display_frame.from_vlc = 0;
    free(drm_vec_plane.strap_pixels);
    drm_vec_plane.strap_pixels = NULL;
    drm_vec_plane.strap_alpha = -1;
    free(drm_vec_plane.osd_source);
    drm_vec_plane.osd_source = NULL;
    free(drm_vec_plane.osd_pixels);
    drm_vec_plane.osd_pixels = NULL;
    drm_vec_plane.osd_active = 0;
    for (int i = 0; i < 2; ++i) {
        destroy_dumb_fb(drm_vec_plane.fd, &drm_vec_plane.fbs[i]);
    }
    if (drm_vec_plane.fd >= 0) {
        close(drm_vec_plane.fd);
        drm_vec_plane.fd = -1;
        drm_vec_plane.crtc_id = 0;
        drm_vec_plane.active_fb_index = 0;
    }
}

static uint32_t *osd_render_pixels;

static void drm_vec_plane_put_osd_pixel(int x, int y, int set) {
    if (!osd_render_pixels || x < 0 || x >= OSD_WIDTH ||
        y < 0 || y >= OSD_HEIGHT) {
        return;
    }
    osd_render_pixels[(size_t)y * OSD_WIDTH + (size_t)x] =
        set ? 0xff00ff00U : 0U;
}

void osd_text(const char *c, int align) {
    (void)align;
    if (!c) {
        return;
    }
    pthread_mutex_lock(&display_mutex);
    if (!drm_vec_plane.osd_source) {
        drm_vec_plane.osd_source = calloc((size_t)OSD_WIDTH * OSD_HEIGHT, 4U);
    }
    if (!drm_vec_plane.osd_pixels) {
        drm_vec_plane.osd_pixels = malloc((size_t)OSD_TARGET_WIDTH * OSD_TARGET_HEIGHT * 4U);
    }
    if (!drm_vec_plane.osd_source || !drm_vec_plane.osd_pixels) {
        fprintf(stderr, "drm-rp1-vec: OSD buffer allocation failed\n");
        drm_vec_plane.osd_active = 0;
        pthread_mutex_unlock(&display_mutex);
        return;
    }

    memset(drm_vec_plane.osd_source, 0,
           (size_t)OSD_WIDTH * OSD_HEIGHT * 4U);
    osd_render_pixels = (uint32_t *)drm_vec_plane.osd_source;
    render_text(c, drm_vec_plane_put_osd_pixel, OSD_WIDTH);
    osd_render_pixels = NULL;
    if (drm_hdmi_planes.active)
        memcpy(drm_hdmi_planes.osd_buffer.mmap, drm_vec_plane.osd_source, OSD_WIDTH * OSD_HEIGHT * 4);

    int ret = ARGBScale(drm_vec_plane.osd_source, OSD_WIDTH * 4,
                        OSD_WIDTH, OSD_HEIGHT,
                        drm_vec_plane.osd_pixels, OSD_TARGET_WIDTH * 4,
                        OSD_TARGET_WIDTH, OSD_TARGET_HEIGHT, kFilterBilinear);
    if (ret != 0) {
        fprintf(stderr, "drm-rp1-vec: OSD scaling failed: %d\n", ret);
        drm_vec_plane.osd_active = 0;
        pthread_mutex_unlock(&display_mutex);
        return;
    }
    drm_vec_plane.osd_active = 1;
    pthread_mutex_unlock(&display_mutex);
}

void osd_text_clear(void) {
    pthread_mutex_lock(&display_mutex);
    drm_vec_plane.osd_active = 0;
    memset(drm_hdmi_planes.osd_buffer.mmap, 0, drm_hdmi_planes.osd_buffer.size);
    pthread_mutex_unlock(&display_mutex);
}

void bg_mode(int mode) {
    static int last_mode = 0;
    if (mode < 0) mode = last_mode;
    hdmi_bg_mode(mode);
    int32_t *src = mode == 2 ? random_bg + (rand() & 0xFFF) : mode == 1 ? blue_bg : black_bg;
    int8_t *srcp = mode == 2 ? preview_random_bg + (rand() & 0xFF) : mode == 1 ? preview_blue_bg : preview_black_bg;
    drm_vec_submit_frame((uint8_t*)src, COMPOSITE_FRAME_W, COMPOSITE_FRAME_H, 0);
    last_mode = mode;
    preview_shm_publish(srcp, -1);
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
