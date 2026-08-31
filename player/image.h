#ifdef USE_LIBVLC

typedef enum {
    VC_IMAGE_RGB888 = 1,
    VC_IMAGE_RGBA32 = 2
} VC_IMAGE_TYPE_T;
#else
#include "bcm_host.h"
#endif

#include <stdint.h>

typedef struct
{
	uint8_t *buffer;
	int width;
	int height;
	int bpp;
	int pitch;
	VC_IMAGE_TYPE_T type;
} Image;
