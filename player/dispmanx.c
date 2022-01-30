#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <bcm_host.h>
#include "image.h"

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
static int screenX, screenY, screenXoffset;
#define STRAP_EXT ".strap.png"

void load_strap(char *path) {
	// Load image file to structure Image
  char *dotptr = strrchr(path, '.');
  int dotpos = dotptr ? dotptr - path : strlen(path);
  char *strapname = alloca(dotpos + sizeof(STRAP_EXT));
  memcpy(strapname, path, dotpos);
  memcpy(strapname + dotpos, STRAP_EXT, sizeof(STRAP_EXT));
  printf("%ld size %s\n", sizeof(STRAP_EXT), strapname);
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
void dispmanx_init() {
	int32_t layer = 10;
	u_int32_t displayNumber = 0;
	int result = 0;

	// Init BCM
	bcm_host_init();

	display
		= vc_dispmanx_display_open(displayNumber);
	assert(display != 0);

  DISPMANX_MODEINFO_T display_info;
  int ret = vc_dispmanx_display_get_info(display, &display_info);
  assert(ret == 0);
  screenX = display_info.width;
  screenY = display_info.height;
  int aspectX = 4;
  int aspectY = 3;
  screenXoffset = (screenX - screenX * aspectY * 16 / 9 / aspectX) / 2;
  printf("Screen size: %d %d\n", display_info.width, display_info.height);

	// Create a resource and copy bitmap to resource
	uint32_t vc_image_ptr = 0;
	resource = vc_dispmanx_resource_create(
		VC_IMAGE_RGBA32, SCREENX, SCREENY, &vc_image_ptr);

	assert(resource != 0);


	// Notify vc that an update is takng place
	DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
	assert(update != 0);

	// Calculate source and destination rect values
	VC_RECT_T srcRect, dstRect;
	vc_dispmanx_rect_set(&srcRect, 0, 0, SCREENX << 16, SCREENY << 16);
	vc_dispmanx_rect_set(&dstRect, screenXoffset, 0, screenX - 2 * screenXoffset, screenY);

	// Add element to vc
        last_strap_alpha = 0;
	VC_DISPMANX_ALPHA_T alpha = { DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_MIX, 0, 0 };
	element = vc_dispmanx_element_add(
		update, display, layer, &dstRect, resource, &srcRect,
		DISPMANX_PROTECTION_NONE, &alpha, NULL, DISPMANX_NO_ROTATE);

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
void blank_background()
{
  uint32_t rgba = 0xff000000;
  DISPMANX_UPDATE_HANDLE_T    update;
  DISPMANX_RESOURCE_HANDLE_T  resource;
  DISPMANX_ELEMENT_HANDLE_T   element;
  int             ret;
  uint32_t vc_image_ptr;
  VC_IMAGE_TYPE_T type = VC_IMAGE_ARGB8888;
  int             layer = - 1;

  VC_RECT_T dst_rect, src_rect;

  resource = vc_dispmanx_resource_create( type, 1 /*width*/, 1 /*height*/, &vc_image_ptr );
  assert( resource );

  vc_dispmanx_rect_set( &dst_rect, 0, 0, 1, 1);

  ret = vc_dispmanx_resource_write_data( resource, type, sizeof(rgba), &rgba, &dst_rect );
  assert(ret == 0);

  vc_dispmanx_rect_set( &src_rect, 0, 0, 1<<16, 1<<16);
  vc_dispmanx_rect_set( &dst_rect, 0, 0, 0, 0);

  update = vc_dispmanx_update_start(0);
  assert(update);

  element = vc_dispmanx_element_add(update, display, layer, &dst_rect, resource, &src_rect,
                                    DISPMANX_PROTECTION_NONE, NULL, NULL, DISPMANX_STEREOSCOPIC_MONO );
  assert(element);

  ret = vc_dispmanx_update_submit_sync( update );
  assert( ret == 0 );
}
void dispmanx_close() {
        int result;
        if (element) {
	DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
	assert(update != 0);
	result = vc_dispmanx_element_remove(update, element);
	assert(result == 0);
	result = vc_dispmanx_update_submit_sync(update);
	assert(result == 0);
        }
        if (resource) {
	result = vc_dispmanx_resource_delete(resource);
	assert(result == 0);
        }
        if (display) {
	result = vc_dispmanx_display_close(display);
	assert(result == 0);
        }
}