#include "preview_shm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct preview_shm {
    struct preview_shm_header header;
    uint8_t pixels[PREVIEW_SHM_BYTES];
};

static int preview_fd = -1;
static struct preview_shm *preview_memory;

int preview_shm_init(void) {
    if (preview_memory) return 0;

    preview_fd = shm_open(PREVIEW_SHM_NAME, O_CREAT | O_RDWR, 0660);
    if (preview_fd == -1) {
        fprintf(stderr, "preview: shm_open: %s\n", strerror(errno));
        return -1;
    }

    if (ftruncate(preview_fd, sizeof(*preview_memory)) == -1) {
        fprintf(stderr, "preview: ftruncate: %s\n", strerror(errno));
        close(preview_fd);
        preview_fd = -1;
        return -1;
    }

    preview_memory = mmap(NULL, sizeof(*preview_memory), PROT_READ | PROT_WRITE,
                          MAP_SHARED, preview_fd, 0);
    if (preview_memory == MAP_FAILED) {
        fprintf(stderr, "preview: mmap: %s\n", strerror(errno));
        preview_memory = NULL;
        close(preview_fd);
        preview_fd = -1;
        return -1;
    }

    __atomic_store_n(&preview_memory->header.sequence, 1, __ATOMIC_RELEASE);
    memset(preview_memory->pixels, 0, sizeof(preview_memory->pixels));
    preview_memory->header.magic = PREVIEW_SHM_MAGIC;
    preview_memory->header.width = PREVIEW_SHM_WIDTH;
    preview_memory->header.height = PREVIEW_SHM_HEIGHT;
    preview_memory->header.stride = PREVIEW_SHM_WIDTH;
    preview_memory->header.position = 0;
    preview_memory->header.duration = -1;
    preview_memory->header.filename[0] = 0;

    __atomic_store_n(&preview_memory->header.sequence, 2, __ATOMIC_RELEASE);
    return 0;
}

void preview_shm_publish(const uint8_t *pixels, int width) {
    if (!preview_memory || !pixels) return;

    __atomic_fetch_add(&preview_memory->header.sequence, 1, __ATOMIC_RELAXED);
    if (width > 0) preview_memory->header.width = width;
    memcpy(preview_memory->pixels, pixels, PREVIEW_SHM_BYTES);
    __atomic_fetch_add(&preview_memory->header.sequence, 1, __ATOMIC_RELEASE);
}
void preview_shm_publish_name(const char *name) {
    if (!preview_memory || !name) return;

    __atomic_fetch_add(&preview_memory->header.sequence, 1, __ATOMIC_RELAXED);
    strncpy(preview_memory->header.filename, name, sizeof(preview_memory->header.filename) - 1);
    preview_memory->header.filename[sizeof(preview_memory->header.filename) - 1] = '\0';
    __atomic_fetch_add(&preview_memory->header.sequence, 1, __ATOMIC_RELEASE);
}
void preview_shm_publish_position(int position) {
    if (!preview_memory) return;

    __atomic_fetch_add(&preview_memory->header.sequence, 1, __ATOMIC_RELAXED);
    preview_memory->header.position = position;
    __atomic_fetch_add(&preview_memory->header.sequence, 1, __ATOMIC_RELEASE);
}
void preview_shm_publish_duration(int duratation) {
    if (!preview_memory) return;

    __atomic_fetch_add(&preview_memory->header.sequence, 1, __ATOMIC_RELAXED);
    preview_memory->header.duration = duratation;
    __atomic_fetch_add(&preview_memory->header.sequence, 1, __ATOMIC_RELEASE);
}

void preview_shm_close(void) {
    if (preview_memory) {
        munmap(preview_memory, sizeof(*preview_memory));
        preview_memory = NULL;
    }
    if (preview_fd != -1) {
        close(preview_fd);
        preview_fd = -1;
    }
}