#ifndef PREVIEW_SHM_H
#define PREVIEW_SHM_H

#include <stdint.h>
#include <limits.h>

#define PREVIEW_SHM_NAME "/jvc-preview"
#define PREVIEW_SHM_MAGIC 0x27A84319
#define PREVIEW_SHM_WIDTH 86u
#define PREVIEW_SHM_HEIGHT 48u
#define PREVIEW_SHM_BYTES (PREVIEW_SHM_WIDTH * PREVIEW_SHM_HEIGHT)

struct preview_shm_header {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int32_t position;
    int32_t duration;
    uint32_t sequence;
    char filename[NAME_MAX+1];
};

int preview_shm_init(void);
void preview_shm_publish(const uint8_t *pixels, int width);
void preview_shm_publish_name(const char *name);
void preview_shm_publish_position(int position);
void preview_shm_publish_duration(int duratation);
void preview_shm_close(void);

#endif