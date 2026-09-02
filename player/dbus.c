#ifdef USE_LIBVLC

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <sys/types.h>
#include <libyuv.h>
#include <vlc/vlc.h>
#include "dbus.h"
#include "dispmanx.h"

static libvlc_instance_t *vlc_instance;
static libvlc_media_player_t *vlc_player;

struct libvlc_frame_log {
   unsigned width;
   unsigned height;
   unsigned bytes;
   unsigned char *buffer;
   unsigned plane_count;
   char chroma[5];
};

static int fill_frame = 0;
static int crop_x = -1, crop_y = -1, crop_w = -1, crop_h = -1;
static struct libvlc_frame_log libvlc_frame_log = { 0 };
static unsigned target_w = 720, target_h = 576;
static int aspect_x = 4;
static int aspect_y = 3;
static unsigned target_aspect_w;
static unsigned target_aspect_h;
static unsigned target_offset_h;
static unsigned target_offset_w;

static void libvlc_log_cleanup(void *opaque) {
   struct libvlc_frame_log *frame = opaque;
   if (!frame) return;
   free(frame->buffer);
   frame->buffer = NULL;
   frame->width = 0;
   frame->height = 0;
   frame->bytes = 0;
   frame->plane_count = 0;
   frame->chroma[0] = '\0';
   printf("libvlc: cleanup callback\n");
}
// Setting crop geometry: 0,45,480,270
// crop corrected: 0,44,320,258
// 258 44 258 576 0
// crop corrected: 0,0,320,258
// 258 0 258 576 0
static void recalculate_geometry(struct libvlc_frame_log *frame) {
   unsigned source_w = frame->width;
   unsigned source_h = frame->height;
   if (!source_w || !source_h) return;
   printf("Recalculating @ %d %d with crop y %d\n", source_w, source_h, crop_y);
   // printf("crop: %d,%d,%d,%d\n", crop_x, crop_y, crop_w, crop_h);
   //crop_x = crop_y = crop_w = crop_h = -1;
   if (crop_x < 0) crop_x = 0;
   if (crop_y < 0) crop_y = 0;
   if (crop_w < 0 || crop_w > source_w) crop_w = source_w;
   if (crop_h < 0 || crop_h > source_h) crop_h = source_h;
   if (crop_x & 1) crop_x--;
   if (crop_y & 1) crop_y--;
   if (crop_w & 1) crop_w++;
   if (crop_h & 1) crop_h++;
   printf("crop corrected: %d,%d,%d,%d\n", crop_x, crop_y, crop_w, crop_h);

   if ((int64_t)crop_w * aspect_y >= (int64_t)crop_h * aspect_x) {
      // source is wider than target aspect
      target_aspect_w = target_w;
      target_aspect_h = (int64_t)target_h * aspect_x * crop_h
                     / ((int64_t)aspect_y * crop_w);
      target_offset_h = (target_h - target_aspect_h) / 2;
      target_offset_w = 0;
   } else {
      // source is taller / narrower
      target_aspect_h = target_h;
      target_aspect_w = (int64_t)target_w * aspect_y * crop_w
                     / ((int64_t)aspect_x * crop_h);
      target_offset_w = (target_w - target_aspect_w) / 2;
      target_offset_h = 0;
   }
   printf("%d %d %d %d %d\n", source_h, crop_y, crop_h, target_aspect_h, target_offset_h);
   printf("aspect corrected size: %dx%d\n", target_aspect_w, target_aspect_h);
}

static unsigned libvlc_log_format(void **opaque, char *chroma,
                                  unsigned *width, unsigned *height,
                                  unsigned *pitches, unsigned *lines) {
   struct libvlc_frame_log *frame = opaque ? *opaque : &libvlc_frame_log;
   if (!frame) frame = &libvlc_frame_log;

   printf("libvlc: format callback: chroma=%4.4s width=%u height=%u\n",
          chroma ? chroma : "????", width ? *width : 0, height ? *height : 0);

   if (!width || !height || !pitches || !lines) {
      return 1;
   }

   unsigned y_stride = 0;
   unsigned uv_stride = 0;
   unsigned y_lines = *height;
   unsigned uv_lines = (*height + 1U) / 2U;
   unsigned plane_count = 1;
   unsigned bpp = 4;

   if (chroma && (!strncmp(chroma, "YUYV", 4) || !strncmp(chroma, "UYVY", 4))) {
      bpp = 2;
      y_stride = *width * bpp;
      pitches[0] = y_stride;
      lines[0] = *height;
      frame->bytes = y_stride * *height;
   } else if (chroma && (!strncmp(chroma, "YV12", 4) || !strncmp(chroma, "I420", 4))) {
      y_stride = *width;
      uv_stride = (*width + 1U) / 2U;
      frame->bytes = (size_t)y_stride * *height + (size_t)uv_stride * uv_lines * 2U;
      pitches[0] = y_stride;
      lines[0] = *height;
      pitches[1] = uv_stride;
      lines[1] = uv_lines;
      pitches[2] = uv_stride;
      lines[2] = uv_lines;
      plane_count = 3;
      bpp = 1;
   } else {
      y_stride = *width * bpp;
      pitches[0] = y_stride;
      lines[0] = *height;
      frame->bytes = y_stride * *height;
   }

   frame->width = *width;
   frame->height = *height;
   frame->plane_count = plane_count;
   if (chroma) {
      memcpy(frame->chroma, chroma, 4);
      frame->chroma[4] = '\0';
   } else {
      frame->chroma[0] = '\0';
   }

   free(frame->buffer);
   frame->buffer = calloc(1, frame->bytes > 0 ? frame->bytes : 1);
   if (!frame->buffer) {
      fprintf(stderr, "libvlc: frame buffer alloc failed\n");
      return 0;
   }

   if (opaque) *opaque = frame;
   recalculate_geometry(frame);
   printf("libvlc: format callback prepared %ux%u plane_count=%u bytes=%u\n",
          frame->width, frame->height, frame->plane_count, frame->bytes);
   return 1;
}

static void *libvlc_log_lock(void *opaque, void **planes) {
   struct libvlc_frame_log *frame = opaque;
   if (!frame) return NULL;
   if (!frame->buffer && frame->bytes > 0) {
      frame->buffer = calloc(1, frame->bytes);
   }
   if (!planes) return frame;
   for (int i = 0; i < 4; ++i) planes[i] = NULL;
   if (frame->plane_count == 3 && frame->buffer) {
      unsigned uv_stride = (frame->width + 1U) / 2U;
      unsigned uv_lines = (frame->height + 1U) / 2U;
      unsigned y_size = frame->width * frame->height;
      unsigned uv_size = uv_stride * uv_lines;
      planes[0] = frame->buffer;
      planes[1] = (unsigned char *)planes[0] + y_size;
      planes[2] = (unsigned char *)planes[1] + uv_size;
   } else {
      planes[0] = frame->buffer;
   }
   return frame;
}

static void libvlc_log_unlock(void *opaque, void *picture, void *const *planes) {
   (void)picture;
   struct libvlc_frame_log *frame = opaque;
   if (!frame || !planes || !planes[0]) {
      return;
   }

   if (frame->plane_count == 1) {
      printf("libvlc: packed frame chroma=%s width=%u height=%u -> display directly\n",
             frame->chroma[0] ? frame->chroma : "????", frame->width, frame->height);
      dispmanx_display_argb((const uint8_t *)planes[0], frame->width, frame->height);
      return;
   }

   if (!planes[1] || !planes[2] || frame->plane_count != 3) {
      return;
   }

   unsigned target_y_stride = target_w;
   unsigned target_uv_stride = (target_w + 1u)/2u;
   unsigned target_uv_h = (target_h + 1u)/2u;
   static uint8_t tmp_buf[720 * 576 * 4], rgb_buffer[720 * 576 * 4];
   //uint8_t tmp_buf[target_y_stride * target_h + target_uv_stride * target_uv_h * 2];
   uint8_t *tmp_y = tmp_buf, *tmp_u = tmp_y + target_y_stride * target_h, *tmp_v = tmp_u + target_uv_stride * target_uv_h;
   //uint32_t rgb_buffer[target_w * target_h * 10];
   unsigned y_stride = frame->width;
   unsigned uv_stride = (frame->width + 1U) / 2U;
   unsigned target_stride = target_w * 4U;
   int r1 = I420Scale((const uint8_t *)planes[0] + crop_x + crop_y * y_stride, y_stride,
                      (const uint8_t *)planes[1] + crop_x/2 + crop_y/2 * uv_stride, uv_stride,
                      (const uint8_t *)planes[2] + crop_x/2 + crop_y/2 * uv_stride, uv_stride,
                      crop_w, crop_h,
                      tmp_y, target_y_stride,
                      tmp_u, target_uv_stride,
                      tmp_v, target_uv_stride,
                      target_aspect_w, target_aspect_h, kFilterBilinear);
   int ret = I420ToARGB(tmp_y, target_y_stride,
                        tmp_u, target_uv_stride,
                        tmp_v, target_uv_stride,
                        rgb_buffer + target_stride * target_offset_h + target_offset_w * 4, target_stride,
                        (int)target_aspect_w, (int)target_aspect_h);
   if (ret == 0) {
      // printf("libvlc: converted %ux%u %s frame to ARGB via libyuv\n",
      //        frame->width, frame->height, frame->chroma[0] ? frame->chroma : "I420");
     dispmanx_display_argb((uint8_t*)rgb_buffer, target_w, target_h);
   }
}

static void libvlc_log_display(void *opaque, void *picture) {
   // printf("libvlc: display callback: opaque=%p picture=%p\n", opaque, picture);
}

static void libvlc_ensure(void) {
   if (!vlc_instance) {
      const char *vlc_argv[] = { "--intf=none", "--no-video-title-show" };
      vlc_instance = libvlc_new(2, vlc_argv);
   }
   if (!vlc_player && vlc_instance) {
      vlc_player = libvlc_media_player_new(vlc_instance);
      libvlc_video_set_callbacks(vlc_player, libvlc_log_lock,
                                 libvlc_log_unlock, libvlc_log_display,
                                 &libvlc_frame_log);
      libvlc_video_set_format_callbacks(vlc_player, libvlc_log_format,
                                        libvlc_log_cleanup);
   }
}

static int libvlc_volume_from_correction(int volume_correction) {
   double linear = 100.0 * pow(10.0, (double)volume_correction / 2000.0);
   if (linear < 0.0) return 0;
   return (int)lround(linear);
}

int libvlc_open_file(const char *path, int64_t start_us, int volume_correction) {
   if (!path) return -1;
   libvlc_ensure();
   if (!vlc_player) return -1;
   libvlc_media_t *media = libvlc_media_new_path(vlc_instance, path);
   if (!media) return -1;

   char volume_option[128];
   char start_option[128];
   int vol = libvlc_volume_from_correction(volume_correction);
   snprintf(volume_option, sizeof(volume_option), ":volume=%d", vol);
   libvlc_media_add_option(media, volume_option);

   if (start_us > 0) {
      snprintf(start_option, sizeof(start_option), ":start-time=%lld", start_us / 1000000LL);
      libvlc_media_add_option(media, start_option);
   }

   libvlc_media_player_set_media(vlc_player, media);
   libvlc_media_release(media);
   // if (start_us > 0) {
   //    printf("set time %ld\n", start_ms);
   //    libvlc_media_player_set_time(vlc_player, (libvlc_time_t)(start_ms / 1000LL));
   // }
   libvlc_media_player_play(vlc_player);
   crop_x = crop_y = crop_w = crop_h = -1;
   return 0;
}

int libvlc_player_has_ended(void) {
   if (!vlc_player) return 1;
   libvlc_state_t state = libvlc_media_player_get_state(vlc_player);
   return state == libvlc_Ended || state == libvlc_Stopped;
}

static int64_t libvlc_query(const char *property) {
   if (!vlc_player) return -1;
   if (!strcmp(property, "Duration")) {
      return libvlc_media_player_get_length(vlc_player) * 1000LL;
   }
   if (!strcmp(property, "Position")) {
      return libvlc_media_player_get_time(vlc_player) * 1000LL;
   }
   if (!strcmp(property, "ResWidth")) {
      return libvlc_video_get_width(vlc_player);
   }
   if (!strcmp(property, "ResHeight")) {
      return libvlc_video_get_height(vlc_player);
   }
   return -1;
}

void dbus_init(void) {
   libvlc_ensure();
}

int64_t query(char *param) {
   return libvlc_query(param);
}

int64_t dbus_action(char *action_name) {
   libvlc_ensure();
   if (!vlc_player) return -1;
   if (!strcmp(action_name, "Stop")) {
      libvlc_media_player_stop(vlc_player);
      return 0;
   }
   if (!strcmp(action_name, "PlayPause")) {
      libvlc_state_t state = libvlc_media_player_get_state(vlc_player);
      if (state == libvlc_Playing) libvlc_media_player_pause(vlc_player);
      else if (state != libvlc_Ended && state != libvlc_Error) libvlc_media_player_play(vlc_player);
      return 0;
   }
   if (!strcmp(action_name, "ShowSubtitles")) return 0;
   if (!strcmp(action_name, "HideSubtitles")) return 0;
   return -1;
}

int dbus_quit(void) { return (int)dbus_action("Stop"); }

int64_t dbus_seek(int64_t seek) {
   if (!vlc_player) return -1;
   libvlc_time_t c = libvlc_media_player_get_time(vlc_player) + (libvlc_time_t)(seek / 1000LL);
   if (c < 0) c = 0;
   libvlc_media_player_set_time(vlc_player, c);
   return libvlc_query("Position");
}

int64_t dbus_volume(int64_t vol) {
   if (!vlc_player) return -1;
   int v = libvlc_volume_from_correction((int)vol);
   int res = libvlc_audio_set_volume(vlc_player, v);
   printf("Corrected volume: %lld = %d result is %d\n", vol, v, res);
   return v;
}

int64_t dbus_crop(int x, int y, int w, int h) {
   printf("Setting crop geometry: %d,%d,%d,%d\n", x, y, w-x, h-y);
   crop_x = x;
   crop_y = y;
   crop_w = w < 0 ? w : w - x;
   crop_h = h < 0 ? h : h - y;
   recalculate_geometry(&libvlc_frame_log);
   return 0;
}

int64_t dbus_aspect_mode(const char *mode) {
   if (!vlc_player) return -1;
   fill_frame = strcmp(mode, "fill") == 0;
   return 0;
}

#else

#include <dbus/dbus.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "dbus.h"

/**
 * Call a method on a remote object
 */
   DBusConnection* conn;
   DBusError err;
void dbus_init() {
   int ret;
   dbus_error_init(&err);

   // connect to the system bus and check for errors
   conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
   if (dbus_error_is_set(&err)) {
      fprintf(stderr, "Connection Error (%s)\n", err.message);
      dbus_error_free(&err);
   }
   if (NULL == conn) {
      printf("--0\n");
      exit(1);
   }
}

int64_t query(char* param)
{
   DBusMessage* msg;
   DBusMessageIter args;
   DBusPendingCall* pending;
   int ret;
   char b[200];
   char* stat = b;
   dbus_uint32_t level;
   dbus_int64_t val = -1;

   // create a new method call and check for errors
   msg = dbus_message_new_method_call("org.mpris.MediaPlayer2.omxplayer", // target for the method call
                                      "/org/mpris/MediaPlayer2", // object to call on
                                      "org.freedesktop.DBus.Properties", // interface to call on
                                      "Get"); // method name
   if (NULL == msg) {
      fprintf(stderr, "Message Null\n");
      return -1;
   }

   char *p1 = "org.mpris.MediaPlayer2.Player";
   // append arguments
   dbus_message_iter_init_append(msg, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &p1)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &param)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }

   // send message and get a handle for a reply
   if (!dbus_connection_send_with_reply (conn, msg, &pending, -1)) { // -1 is default timeout
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (NULL == pending) {
      fprintf(stderr, "Pending Call Null\n");
      return -1;
   }
   dbus_connection_flush(conn);

//   printf("Request Sent\n");

   // free message
   dbus_message_unref(msg);

   // block until we recieve a reply
   dbus_pending_call_block(pending);

   // get the reply message
   msg = dbus_pending_call_steal_reply(pending);
   if (NULL == msg) {
      fprintf(stderr, "Reply Null\n");
      exit(1);
   }
   // free the pending message handle
   dbus_pending_call_unref(pending);

   // read the parameters
   if (!dbus_message_iter_init(msg, &args))
      fprintf(stderr, "Message has no arguments!\n");
   else if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(&args))
;//      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &stat);

   if (DBUS_TYPE_INT64 != dbus_message_iter_get_arg_type(&args))
;//      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &val);


//   printf("Got Reply: %lld, %s\n", val, stat);

   // free reply
   dbus_message_unref(msg);
   return val;
}

int64_t dbus_action(char *action_name)
{
   DBusMessage* msg;
   DBusMessageIter args;
   DBusPendingCall* pending;
   int ret;
   char b[200];
   char* stat = b;
   dbus_uint32_t level;
   dbus_int64_t val = -1;

   // printf(">>> CMD %s\n", action_name);
   // create a new method call and check for errors
   msg = dbus_message_new_method_call("org.mpris.MediaPlayer2.omxplayer", // target for the method call
                                      "/org/mpris/MediaPlayer2", // object to call on
                                      "org.mpris.MediaPlayer2.Player", // interface to call on
                                      action_name); // method name
   // printf(">>> END\n");
   if (NULL == msg) {
      fprintf(stderr, "Message Null\n");
      return -1;
   }

   // append arguments
   int p1 = 15;
   dbus_message_iter_init_append(msg, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &p1)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }

   // send message and get a handle for a reply
   if (!dbus_connection_send_with_reply (conn, msg, &pending, -1)) { // -1 is default timeout
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (NULL == pending) {
      fprintf(stderr, "Pending Call Null\n");
      return -1;
   }
   dbus_connection_flush(conn);

//   printf("Request Sent\n");

   // free message
   dbus_message_unref(msg);

   // block until we recieve a reply
   dbus_pending_call_block(pending);

   // get the reply message
   msg = dbus_pending_call_steal_reply(pending);
   if (NULL == msg) {
      fprintf(stderr, "Reply Null\n");
      exit(1);
   }
   // free the pending message handle
   dbus_pending_call_unref(pending);

   // read the parameters
   if (!dbus_message_iter_init(msg, &args))
      fprintf(stderr, "Message has no arguments!\n");
   else if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &stat);

   if (DBUS_TYPE_INT64 != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &val);


   // printf("Got Reply: %lld, %s\n", val, stat);

   // free reply
   dbus_message_unref(msg);
   return val;
}
int dbus_quit() { return dbus_action("Stop"); }
int64_t dbus_seek(int64_t seek)
{
   DBusMessage* msg;
   DBusMessageIter args;
   DBusPendingCall* pending;
   int ret;
   char b[200];
   char* stat = b;
   dbus_uint32_t level;
   dbus_int64_t val = -1;

   // create a new method call and check for errors
   msg = dbus_message_new_method_call("org.mpris.MediaPlayer2.omxplayer", // target for the method call
                                      "/org/mpris/MediaPlayer2", // object to call on
                                      "org.mpris.MediaPlayer2.Player", // interface to call on
                                      "Seek"); // method name
   if (NULL == msg) {
      fprintf(stderr, "Message Null\n");
      return -1;
   }

   // append arguments
   dbus_message_iter_init_append(msg, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT64, &seek)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }

   // send message and get a handle for a reply
   if (!dbus_connection_send_with_reply (conn, msg, &pending, -1)) { // -1 is default timeout
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (NULL == pending) {
      fprintf(stderr, "Pending Call Null\n");
      return -1;
   }
   dbus_connection_flush(conn);

//   printf("Request Sent\n");

   // free message
   dbus_message_unref(msg);

   // block until we recieve a reply
   dbus_pending_call_block(pending);

   // get the reply message
   msg = dbus_pending_call_steal_reply(pending);
   if (NULL == msg) {
      fprintf(stderr, "Reply Null\n");
      exit(1);
   }
   // free the pending message handle
   dbus_pending_call_unref(pending);

   // read the parameters
   if (!dbus_message_iter_init(msg, &args))
      fprintf(stderr, "Message has no arguments!\n");
   else if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &stat);

   if (DBUS_TYPE_INT64 != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &val);


   // printf("Got Reply: %lld, %s\n", val, stat);

   // free reply
   dbus_message_unref(msg);
   return val;
}

int64_t dbus_volume(int64_t vol)
{
   DBusMessage* msg;
   DBusMessageIter args;
   DBusPendingCall* pending;
   int ret;
   char b[200];
   char* stat = b;
   dbus_uint32_t level;
   dbus_int64_t val = -1;
   double dvol = pow(10, vol / 2000.);

   // create a new method call and check for errors
   msg = dbus_message_new_method_call("org.mpris.MediaPlayer2.omxplayer", // target for the method call
                                      "/org/mpris/MediaPlayer2", // object to call on
                                      "org.freedesktop.DBus.Properties", // interface to call on
                                      "Set"); // method name
   if (NULL == msg) {
      fprintf(stderr, "Message Null\n");
      return -1;
   }

   char *p1 = "org.mpris.MediaPlayer2.Player";
   char *p2 = "Volume";
   // append arguments
   dbus_message_iter_init_append(msg, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &p1)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &p2)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   printf("XXX! %lf\n", dvol);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &dvol)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }

   // send message and get a handle for a reply
   if (!dbus_connection_send_with_reply (conn, msg, &pending, -1)) { // -1 is default timeout
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (NULL == pending) {
      fprintf(stderr, "Pending Call Null\n");
      return -1;
   }
   dbus_connection_flush(conn);

//   printf("Request Sent\n");

   // free message
   dbus_message_unref(msg);

   // block until we recieve a reply
   dbus_pending_call_block(pending);

   // get the reply message
   msg = dbus_pending_call_steal_reply(pending);
   if (NULL == msg) {
      fprintf(stderr, "Reply Null\n");
      exit(1);
   }
   // free the pending message handle
   dbus_pending_call_unref(pending);

   // read the parameters
   if (!dbus_message_iter_init(msg, &args))
      fprintf(stderr, "Message has no arguments!\n");
   else if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &stat);

   if (DBUS_TYPE_INT64 != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &val);


   printf("Got Reply: %lld, %s\n", val, stat);

   // free reply
   dbus_message_unref(msg);
   return val;
}

int64_t dbus_crop(int x, int y, int w, int h)
{
   DBusMessage* msg;
   DBusMessageIter args;
   DBusPendingCall* pending;
   int ret;
   char b[200], crop_buf[200], *crop_buf_ptr = crop_buf;
   char* stat = b;
   char* fake_obj_str = "/";
   dbus_uint32_t level;
   dbus_int64_t val = -1;
   sprintf(crop_buf, "%d %d %d %d", x, y, w, h);
   printf("{%s}\n", crop_buf);

   // create a new method call and check for errors
   msg = dbus_message_new_method_call("org.mpris.MediaPlayer2.omxplayer", // target for the method call
                                      "/org/mpris/MediaPlayer2", // object to call on
                                      "org.mpris.MediaPlayer2.Player", // interface to call on
                                      "SetVideoCropPos"); // method name
   if (NULL == msg) {
      fprintf(stderr, "Message Null\n");
      return -1;
   }

   // append arguments
   dbus_message_iter_init_append(msg, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &fake_obj_str)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &crop_buf_ptr)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }

   // send message and get a handle for a reply
   if (!dbus_connection_send_with_reply (conn, msg, &pending, -1)) { // -1 is default timeout
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (NULL == pending) {
      fprintf(stderr, "Pending Call Null\n");
      return -1;
   }
   dbus_connection_flush(conn);

//   printf("Request Sent\n");

   // free message
   dbus_message_unref(msg);

   // block until we recieve a reply
   dbus_pending_call_block(pending);

   // get the reply message
   msg = dbus_pending_call_steal_reply(pending);
   if (NULL == msg) {
      fprintf(stderr, "Reply Null\n");
      exit(1);
   }
   // free the pending message handle
   dbus_pending_call_unref(pending);

   // read the parameters
   if (!dbus_message_iter_init(msg, &args))
      fprintf(stderr, "Message has no arguments!\n");
   else if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &stat);

   if (DBUS_TYPE_INT64 != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &val);


   // printf("Got Reply: %lld, %s\n", val, stat);

   // free reply
   dbus_message_unref(msg);
   return val;
}


int64_t dbus_aspect_mode(const char *mode)
{
   DBusMessage* msg;
   DBusMessageIter args;
   DBusPendingCall* pending;
   int ret;
   char b[200], crop_buf[200], *crop_buf_ptr = crop_buf;
   char* stat = b;
   char* fake_obj_str = "/";
   dbus_uint32_t level;
   dbus_int64_t val = -1;

   // create a new method call and check for errors
   msg = dbus_message_new_method_call("org.mpris.MediaPlayer2.omxplayer", // target for the method call
                                      "/org/mpris/MediaPlayer2", // object to call on
                                      "org.mpris.MediaPlayer2.Player", // interface to call on
                                      "SetAspectMode"); // method name
   if (NULL == msg) {
      fprintf(stderr, "Message Null\n");
      return -1;
   }

   // append arguments
   dbus_message_iter_init_append(msg, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &fake_obj_str)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &mode)) {
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }

   // send message and get a handle for a reply
   if (!dbus_connection_send_with_reply (conn, msg, &pending, -1)) { // -1 is default timeout
      fprintf(stderr, "Out Of Memory!\n");
      return -1;
   }
   if (NULL == pending) {
      fprintf(stderr, "Pending Call Null\n");
      return -1;
   }
   dbus_connection_flush(conn);

//   printf("Request Sent\n");

   // free message
   dbus_message_unref(msg);

   // block until we recieve a reply
   dbus_pending_call_block(pending);

   // get the reply message
   msg = dbus_pending_call_steal_reply(pending);
   if (NULL == msg) {
      fprintf(stderr, "Reply Null\n");
      exit(1);
   }
   // free the pending message handle
   dbus_pending_call_unref(pending);

   // read the parameters
   if (!dbus_message_iter_init(msg, &args))
      fprintf(stderr, "Message has no arguments!\n");
   else if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &stat);

   if (DBUS_TYPE_INT64 != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not string! %c\n", dbus_message_iter_get_arg_type(&args));
   else
      dbus_message_iter_get_basic(&args, &val);


   // printf("Got Reply: %lld, %s\n", val, stat);

   // free reply
   dbus_message_unref(msg);
   return val;
}
#endif
