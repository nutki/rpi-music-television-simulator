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

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include "image.h"
#include "vcrfont.h"
#include <libyuv.h>

extern bool loadPNG(const char *f_name, Image *image);

#define STRAP_WIDTH 822
#define STRAP_HEIGHT 576
#define OSD_WIDTH 320
#define OSD_HEIGHT VCR_FONT_H
#define OSD_TARGET_WIDTH 700
#define OSD_OFFSET_X ((822 - 700)/2)
#define OSD_TARGET_HEIGHT 48
#define OSD_OFFSET_Y 50
#define TELETEXT_OFFSET_Y 16
#define DISPLAY_FRAME_BYTES (STRAP_WIDTH * STRAP_HEIGHT * 4U)
static uint8_t strap_premultiplied[STRAP_WIDTH * STRAP_HEIGHT * 4];

struct drm_vec_plane {
    int fd;
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t connector_id;
    uint32_t possible_crtcs;
    int width;
    int height;
    uint32_t fb_id;
    uint32_t fb_handle;
    uint8_t *fb_pixels;
    size_t fb_size;
    uint32_t fb_pitch;
    uint32_t fb_ids[2];
    uint32_t fb_handles[2];
    uint8_t *fb_pixels_buf[2];
    size_t fb_sizes[2];
    uint32_t fb_pitches[2];
    int active_fb_index;
    uint32_t mode_fb_id;
    uint32_t mode_fb_handle;
    uint8_t *mode_fb_pixels;
    size_t mode_fb_size;
    uint8_t *strap_pixels;
    int strap_alpha;
    uint8_t *osd_source;
    uint8_t *osd_pixels;
    int osd_active;
};

static struct drm_vec_plane drm_vec_plane = {
    .fd = -1,
    .plane_id = 0,
    .crtc_id = 0,
    .connector_id = 0,
    .possible_crtcs = 0,
    .width = 0,
    .height = 0,
    .fb_id = 0,
    .fb_handle = 0,
    .fb_pixels = NULL,
    .fb_size = 0,
    .fb_pitch = 0,
    .fb_ids = {0, 0},
    .fb_handles = {0, 0},
    .fb_pixels_buf = {NULL, NULL},
    .fb_sizes = {0, 0},
    .fb_pitches = {0, 0},
    .active_fb_index = 0,
    .mode_fb_id = 0,
    .mode_fb_handle = 0,
    .mode_fb_pixels = NULL,
    .mode_fb_size = 0,
    .strap_pixels = NULL,
    .strap_alpha = -1,
    .osd_source = NULL,
    .osd_pixels = NULL,
    .osd_active = 0,
};

struct display_frame {
    uint8_t *pixels;
    unsigned width;
    unsigned height;
    int valid;
    int busy;
};

static struct display_frame display_frame;
static pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t display_buffer_free = PTHREAD_COND_INITIALIZER;
static pthread_t display_thread;
static int display_thread_running;
static int display_thread_stop;
static uint64_t vlc_submitted;
static uint64_t display_flips;

static void drm_vec_plane_wait_vblank(void) {
    drmVBlank vblank = {0};
    vblank.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_NEXTONMISS;
    vblank.request.sequence = 1;
    if (drmWaitVBlank(drm_vec_plane.fd, &vblank) != 0) {
        fprintf(stderr, "drm-rp1-vec: drmWaitVBlank failed: %s\n", strerror(errno));
    }
}

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

    uint32_t handles[4] = { create.handle, 0, 0, 0 };
    uint32_t pitches[4] = { pitch, 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    uint32_t fb_id = 0;
    int ret = drmModeAddFB2(drm_vec_plane.fd, width, height,
                            DRM_FORMAT_XRGB8888,
                            handles, pitches, offsets, &fb_id, 0);
    if (ret != 0) {
        fprintf(stderr, "drm-rp1-vec: drmModeAddFB failed: %d\n", ret);
        munmap(pixels, size);
        return -1;
    }

    drm_vec_plane_wait_vblank();
    ret = drmModeSetPlane(drm_vec_plane.fd, drm_vec_plane.plane_id,
                          drm_vec_plane.crtc_id, fb_id, DRM_MODE_PAGE_FLIP_EVENT,
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

    drmModeConnectorPtr connector = NULL;
    uint32_t connector_id = 0;
    for (int i = 0; i < resources->count_connectors; ++i) {
        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED) {
            connector_id = connector->connector_id;
            break;
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }
    if (!connector) {
        fprintf(stderr, "drm-rp1-vec: no connected connector found\n");
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    drmModeModeInfo *pmode = NULL, custom_mode;
    for (int i = 0; i < connector->count_modes; ++i) {
        if (connector->modes[i].hdisplay == 720 &&
            connector->modes[i].vdisplay == 576 &&
            (connector->modes[i].flags & DRM_MODE_FLAG_INTERLACE)) {
            pmode = &connector->modes[i];
            if (!strcmp(connector->modes[i].name, "720x576i")) {
                break;
            }
        }
    }
    if (!pmode) {
        fprintf(stderr, "drm-rp1-vec: connected output has no 720x576i mode\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }
    custom_mode = *pmode;
    custom_mode.clock = 15429;
    custom_mode.htotal = 987;
    custom_mode.hdisplay = 822;
    custom_mode.hsync_start = 836;
    custom_mode.hsync_end = 909;
    drmModeEncoderPtr encoder = drmModeGetEncoder(fd, connector->encoder_id);
    if (!encoder) {
        fprintf(stderr, "drm-rp1-vec: failed to get connector encoder\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }
    uint32_t crtc_id = encoder->crtc_id;
    int crtc_index = -1;
    for (int i = 0; i < resources->count_crtcs; ++i) {
        if (resources->crtcs[i] == crtc_id) {
            crtc_index = i;
            break;
        }
    }
    drmModeFreeEncoder(encoder);
    if (crtc_index < 0) {
        fprintf(stderr, "drm-rp1-vec: connector encoder has no usable CRTC\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
    if (!planes || planes->count_planes == 0) {
        fprintf(stderr, "drm-rp1-vec: no planes reported for %s %p\n", device, (void *)planes);
        if (planes) drmModeFreePlaneResources(planes);
        drmModeFreeResources(resources);
        drmModeFreeConnector(connector);
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

        if (!(plane->possible_crtcs & (1U << crtc_index)) ||
            plane->count_formats == 0) {
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
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    drm_vec_plane.fd = fd;
    drm_vec_plane.plane_id = plane_id;
    drm_vec_plane.crtc_id = crtc_id;
    drm_vec_plane.connector_id = connector_id;
    drm_vec_plane.possible_crtcs = plane->possible_crtcs;
    drm_vec_plane.width = custom_mode.hdisplay;
    drm_vec_plane.height = custom_mode.vdisplay;

    struct drm_mode_create_dumb create = {0};
    create.width = custom_mode.hdisplay;
    create.height = custom_mode.vdisplay;
    create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        fprintf(stderr, "drm-rp1-vec: mode framebuffer allocation failed\n");
        drmModeFreePlane(plane);
        drmModeFreePlaneResources(planes);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }
    struct drm_mode_map_dumb map = { .handle = create.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        fprintf(stderr, "drm-rp1-vec: mode framebuffer mapping failed\n");
        drmModeFreePlane(plane);
        drmModeFreePlaneResources(planes);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }
    uint8_t *mode_pixels = mmap(NULL, create.size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, map.offset);
    if (mode_pixels == MAP_FAILED) {
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        fprintf(stderr, "drm-rp1-vec: mode framebuffer mapping failed\n");
        drmModeFreePlane(plane);
        drmModeFreePlaneResources(planes);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }
    memset(mode_pixels, 0, create.size);
    uint32_t mode_fb_id = 0;
    if (drmModeAddFB(fd, create.width, create.height, 24, 32,
                     create.pitch, create.handle, &mode_fb_id) != 0 ||
        drmModeSetCrtc(fd, crtc_id, mode_fb_id, 0, 0, &connector_id, 1,
                       &custom_mode) != 0) {
        fprintf(stderr, "drm-rp1-vec: failed to set 720x576i CRTC mode\n");
        if (mode_fb_id) drmModeRmFB(fd, mode_fb_id);
        munmap(mode_pixels, create.size);
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        drmModeFreePlane(plane);
        drmModeFreePlaneResources(planes);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }
    drm_vec_plane.mode_fb_id = mode_fb_id;
    drm_vec_plane.mode_fb_handle = create.handle;
    drm_vec_plane.mode_fb_pixels = mode_pixels;
    drm_vec_plane.mode_fb_size = create.size;

    printf("drm-rp1-vec: acquired plane id=%u crtc=%u connector=%u mode=%s possible_crtcs=0x%x\n",
           drm_vec_plane.plane_id,
           drm_vec_plane.crtc_id,
           drm_vec_plane.connector_id,
           custom_mode.name,
           drm_vec_plane.possible_crtcs);

    drmModeFreePlane(plane);
    drmModeFreePlaneResources(planes);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    return 0;
}

static int drm_vec_plane_prepare_buffer(unsigned out_w, unsigned out_h, int index) {
    uint32_t pitch = out_w * 4U;
    uint32_t size = pitch * out_h;

    if (drm_vec_plane.fb_ids[index] != 0) {
        return 0;
    }

    struct drm_mode_create_dumb create = {0};
    create.width = out_w;
    create.height = out_h;
    create.bpp = 32;
    create.flags = 0;
    if (drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        fprintf(stderr, "drm-rp1-vec: DRM_IOCTL_MODE_CREATE_DUMB failed\n");
        return -1;
    }

    drm_vec_plane.fb_handles[index] = create.handle;
    drm_vec_plane.fb_pitches[index] = pitch;
    drm_vec_plane.fb_sizes[index] = size;

    struct drm_mode_map_dumb map = {0};
    map.handle = create.handle;
    if (drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        fprintf(stderr, "drm-rp1-vec: DRM_IOCTL_MODE_MAP_DUMB failed\n");
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        return -1;
    }

    drm_vec_plane.fb_pixels_buf[index] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                                             drm_vec_plane.fd, map.offset);
    if (drm_vec_plane.fb_pixels_buf[index] == MAP_FAILED) {
        fprintf(stderr, "drm-rp1-vec: mmap failed: %s\n", strerror(errno));
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        drm_vec_plane.fb_pixels_buf[index] = NULL;
        return -1;
    }

    int ret = drmModeAddFB(drm_vec_plane.fd, out_w, out_h, 24, 32,
                           pitch, create.handle, &drm_vec_plane.fb_ids[index]);
    if (ret != 0) {
        fprintf(stderr, "drm-rp1-vec: drmModeAddFB failed: %d\n", ret);
        munmap(drm_vec_plane.fb_pixels_buf[index], size);
        drm_vec_plane.fb_pixels_buf[index] = NULL;
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        drm_vec_plane.fb_ids[index] = 0;
        drm_vec_plane.fb_handles[index] = 0;
        return -1;
    }

    return 0;
}
static uint8_t tt_source[370] = { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1,1, 0, 0, 1 };
static void overlay_teletext(uint8_t *argb) {
    uint32_t *pixels = (uint32_t *)argb;
    uint32_t white = 0x00ffffff;
    int pitch = 822 * 4;
    static int cc = 0;
    for (int y = 2; y < 16; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * pitch);
        tt_source[30] = cc&1;
        tt_source[31] = (cc>>1)&1;
        tt_source[32] = (cc>>2)&1;
        tt_source[33] = (cc>>3)&1;
        tt_source[34] = (cc>>4)&1;
        tt_source[35] = (cc>>5)&1;
        tt_source[36] = (cc>>6)&1;
        tt_source[37] = (cc>>7)&1;
        cc++;
        for (int x = 0; x < 822; x++) {
            const uint8_t *source_row = tt_source;
            // ratio = pixel clock = 108Mhz/7 / teletext data clock  = 6.9375Mhz = 2.2239...
            // offset (real data (clock runin) starts at 8)
            int source_x = x/2.223938223938224 + 6;
            int v = source_x < 370 ? source_row[source_x] : 0;
            row[x] = v ? (0xff000000 | white) : 0xff000000;
        }
    }

}

static int drm_vec_plane_update_fb(const uint8_t *argb, unsigned width, unsigned height) {
    if (!argb || width == 0 || height == 0) {
        return -1;
    }

    unsigned out_w = drm_vec_plane.width > 0 ? drm_vec_plane.width : width;
    unsigned out_h = drm_vec_plane.height > 0 ? drm_vec_plane.height : height;
    uint32_t pitch = out_w * 4U;

    int target_index = drm_vec_plane.active_fb_index ^ 1;
    if (drm_vec_plane_prepare_buffer(out_w, out_h, target_index) < 0) {
        return -1;
    }

    uint8_t *pixels = drm_vec_plane.fb_pixels_buf[target_index];
//    printf("%d %d %d %d\n", width, out_w, height, out_h);

    int tt_offset = TELETEXT_OFFSET_Y * pitch;
    if ((width == out_w && height == out_h)) {
        memcpy(pixels + tt_offset, argb, drm_vec_plane.fb_sizes[target_index] - tt_offset);
    } else {
        for (unsigned y = 0; y < out_h; ++y) {
            unsigned sy = (y * height) / out_h;
            uint32_t *dst = (uint32_t *)(pixels + (size_t)y * pitch);
            const uint32_t *src = (const uint32_t *)(argb + (size_t)sy * width * 4U);
            memcpy(dst, src, width * 4);
        }
    }

    if (drm_vec_plane.strap_alpha > 0) {
        int blend_ret = ARGBBlend(strap_premultiplied,
                                  STRAP_WIDTH * 4,
                                  pixels + tt_offset, pitch,
                                  pixels + tt_offset, pitch,
                                  out_w, out_h - TELETEXT_OFFSET_Y);
        if (blend_ret != 0) {
            fprintf(stderr, "drm-rp1-vec: strap blend failed: %d\n", blend_ret);
            return -1;
        }
    }

    if (drm_vec_plane.osd_active && drm_vec_plane.osd_pixels) {
        int blend_ret = ARGBBlend(drm_vec_plane.osd_pixels,
                                  OSD_TARGET_WIDTH * 4,
                                  pixels + tt_offset + pitch * OSD_OFFSET_Y + OSD_OFFSET_X * 4, pitch,
                                  pixels + tt_offset + pitch * OSD_OFFSET_Y + OSD_OFFSET_X * 4, pitch,
                                  OSD_TARGET_WIDTH, OSD_TARGET_HEIGHT);
        if (blend_ret != 0) {
            fprintf(stderr, "drm-rp1-vec: OSD blend failed: %d\n", blend_ret);
            return -1;
        }
    }
//    overlay_teletext(pixels + tt_offset * 5);

//    drm_vec_plane_wait_vblank();
    int ret = drmModeSetPlane(drm_vec_plane.fd, drm_vec_plane.plane_id,
                              drm_vec_plane.crtc_id, drm_vec_plane.fb_ids[target_index], DRM_MODE_PAGE_FLIP_EVENT,
                              0, 0, out_w, out_h,
                              0, 0, out_w << 16, out_h << 16);
    if (ret != 0) {
        fprintf(stderr, "drm-rp1-vec: drmModeSetPlane failed: %d\n", ret);
        return -1;
    }

    drm_vec_plane.active_fb_index = target_index;
    drm_vec_plane.fb_id = drm_vec_plane.fb_ids[target_index];
    drm_vec_plane.fb_handle = drm_vec_plane.fb_handles[target_index];
    drm_vec_plane.fb_pixels = pixels;
    drm_vec_plane.fb_size = drm_vec_plane.fb_sizes[target_index];
    drm_vec_plane.fb_pitch = pitch;

    // printf("drm-rp1-vec: uploaded %ux%u ARGB frame to plane %u via back buffer %u\n",
    //        out_w, out_h, drm_vec_plane.plane_id, drm_vec_plane.fb_id);
    return 0;
}

static void *drm_vec_display_loop(void *unused) {
    (void)unused;
    struct timespec last_report;
    clock_gettime(CLOCK_MONOTONIC, &last_report);
    uint64_t report_flips = 0;
    uint64_t report_vlc = 0;

    for (;;) {
        drm_vec_plane_wait_vblank();
        pthread_mutex_lock(&display_mutex);
        if (display_thread_stop) {
            pthread_mutex_unlock(&display_mutex);
            break;
        }

        if (display_frame.valid) {
            display_frame.busy = 1;
            int ret = drm_vec_plane_update_fb(display_frame.pixels,
                                              display_frame.width,
                                              display_frame.height);
            if (ret == 0) {
                display_flips++;
                report_flips++;
            }
            display_frame.busy = 0;
            pthread_cond_signal(&display_buffer_free);
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - last_report.tv_sec) +
                         (double)(now.tv_nsec - last_report.tv_nsec) / 1000000000.0;
        if (elapsed >= 1.0) {
                        report_vlc = vlc_submitted;
                //         printf("drm-rp1-vec: vlc fps=%.2f output fps=%.2f flips=%llu\n",
                //                      (double)report_vlc / elapsed,
                //    (double)report_flips / elapsed,
                //  (unsigned long long)display_flips);
             vlc_submitted = 0;
            report_flips = 0;
            last_report = now;
        }
        pthread_mutex_unlock(&display_mutex);
    }
    return NULL;
}

static int drm_vec_submit_frame(const uint8_t *argb, unsigned width,
                                unsigned height, int from_vlc) {
    if (!argb || width == 0 || height == 0 ||
        (size_t)width * height * 4U > DISPLAY_FRAME_BYTES) {
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
    display_frame.valid = 1;
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

    uint8_t *scaled_argb = malloc((size_t)STRAP_WIDTH * STRAP_HEIGHT * 4U);
    if (!scaled_argb) {
        fprintf(stderr, "drm-rp1-vec: strap pixel allocation failed\n");
        free(image.buffer);
        return;
    }
    int ret = ARGBScale(image.buffer + image.width / 16 * 8, image.pitch, image.width / 4 * 3, image.height,
                        scaled_argb, STRAP_WIDTH * 4, STRAP_WIDTH, STRAP_HEIGHT,
                        kFilterBilinear);
    free(image.buffer);
    if (ret != 0) {
        fprintf(stderr, "drm-rp1-vec: strap scaling failed: %d\n", ret);
        free(scaled_argb);
        return;
    }
    ARGBAttenuate(scaled_argb, STRAP_WIDTH * 4,
                  scaled_argb, STRAP_WIDTH * 4,
                  STRAP_WIDTH, STRAP_HEIGHT);

    pthread_mutex_lock(&display_mutex);
    free(drm_vec_plane.strap_pixels);
    drm_vec_plane.strap_pixels = scaled_argb;
    drm_vec_plane.strap_alpha = -1;
    pthread_mutex_unlock(&display_mutex);
    printf("drm-rp1-vec: loaded strap %dx%d scaled to %dx%d\n",
           image.width, image.height, STRAP_WIDTH, STRAP_HEIGHT);
}

void dispmanx_init(void) {
    if (drm_vec_plane_acquire() < 0) {
        return;
    }
    display_frame.pixels = malloc(DISPLAY_FRAME_BYTES);
    if (!display_frame.pixels) {
        fprintf(stderr, "drm-rp1-vec: display buffer allocation failed\n");
        return;
    }
    display_frame.width = STRAP_WIDTH;
    display_frame.height = STRAP_HEIGHT;
    display_frame.valid = 0;
    display_frame.busy = 0;
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
    ARGBShade(drm_vec_plane.strap_pixels, STRAP_WIDTH * 4,
              strap_premultiplied, STRAP_WIDTH * 4,
              STRAP_WIDTH, STRAP_HEIGHT, alpha_mult);
    pthread_mutex_unlock(&display_mutex);
}

uint32_t black_bg[822*576], blue_bg[822*576], random_bg[822*576 + 0xfff];
uint8_t preview_black_bg[86*48], preview_blue_bg[86*48], preview_random_bg[86*48+0xff];

void blank_background(void) {
    for(int i = 0; i < 822*576; i++) blue_bg[i] = 0xFF0000FF;
    for(int i = 0; i < 822*576 + 0xfff; i++) random_bg[i] = 0x01010101 * (rand() & 0xff);
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
    display_frame.valid = 0;
    display_frame.busy = 0;
    free(drm_vec_plane.strap_pixels);
    drm_vec_plane.strap_pixels = NULL;
    drm_vec_plane.strap_alpha = -1;
    free(drm_vec_plane.osd_source);
    drm_vec_plane.osd_source = NULL;
    free(drm_vec_plane.osd_pixels);
    drm_vec_plane.osd_pixels = NULL;
    drm_vec_plane.osd_active = 0;
    if (drm_vec_plane.mode_fb_pixels != NULL) {
        munmap(drm_vec_plane.mode_fb_pixels, drm_vec_plane.mode_fb_size);
        drm_vec_plane.mode_fb_pixels = NULL;
    }
    if (drm_vec_plane.mode_fb_id != 0 && drm_vec_plane.fd >= 0) {
        drmModeRmFB(drm_vec_plane.fd, drm_vec_plane.mode_fb_id);
        drm_vec_plane.mode_fb_id = 0;
    }
    if (drm_vec_plane.mode_fb_handle != 0 && drm_vec_plane.fd >= 0) {
        struct drm_mode_destroy_dumb destroy = {
            .handle = drm_vec_plane.mode_fb_handle,
        };
        drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        drm_vec_plane.mode_fb_handle = 0;
    }
    for (int i = 0; i < 2; ++i) {
        if (drm_vec_plane.fb_pixels_buf[i] != NULL) {
            munmap(drm_vec_plane.fb_pixels_buf[i], drm_vec_plane.fb_sizes[i]);
            drm_vec_plane.fb_pixels_buf[i] = NULL;
        }
        if (drm_vec_plane.fb_ids[i] != 0 && drm_vec_plane.fd >= 0) {
            drmModeRmFB(drm_vec_plane.fd, drm_vec_plane.fb_ids[i]);
            drm_vec_plane.fb_ids[i] = 0;
        }
        if (drm_vec_plane.fb_handles[i] != 0 && drm_vec_plane.fd >= 0) {
            struct drm_mode_destroy_dumb destroy = { .handle = drm_vec_plane.fb_handles[i] };
            drmIoctl(drm_vec_plane.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
            drm_vec_plane.fb_handles[i] = 0;
        }
    }
    if (drm_vec_plane.fb_pixels != NULL) {
        drm_vec_plane.fb_pixels = NULL;
    }
    if (drm_vec_plane.fb_id != 0) {
        drm_vec_plane.fb_id = 0;
    }
    if (drm_vec_plane.fd >= 0) {
        close(drm_vec_plane.fd);
        drm_vec_plane.fd = -1;
        drm_vec_plane.plane_id = 0;
        drm_vec_plane.crtc_id = 0;
        drm_vec_plane.connector_id = 0;
        drm_vec_plane.possible_crtcs = 0;
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
    pthread_mutex_unlock(&display_mutex);
}

#include "preview_shm.h"
void bg_mode(int mode) {
    static int last_mode = 0;
    if (mode < 0) mode = last_mode;
    int32_t *src = mode == 2 ? random_bg + (rand() & 0xFFF) : mode == 1 ? blue_bg : black_bg;
    int8_t *srcp = mode == 2 ? preview_random_bg + (rand() & 0xFF) : mode == 1 ? preview_blue_bg : preview_black_bg;
    drm_vec_submit_frame((uint8_t*)src, 822, 576, 0);
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
