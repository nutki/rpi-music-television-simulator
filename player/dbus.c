#ifdef USE_LIBVLC

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <vlc/vlc.h>
#include "dbus.h"

static libvlc_instance_t *vlc_instance;
static libvlc_media_player_t *vlc_player;

struct libvlc_frame_log {
   unsigned width;
   unsigned height;
   unsigned pitch;
   unsigned lines;
   unsigned bytes;
   unsigned char *buffer;
};

static struct libvlc_frame_log libvlc_frame_log = { 0 };

static void libvlc_log_cleanup(void *opaque) {
   struct libvlc_frame_log *frame = opaque;
   if (!frame) return;
   free(frame->buffer);
   frame->buffer = NULL;
   frame->width = 0;
   frame->height = 0;
   frame->pitch = 0;
   frame->lines = 0;
   frame->bytes = 0;
   printf("libvlc: cleanup callback\n");
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

   unsigned bpp = 4;
   if (chroma && (!strncmp(chroma, "YUYV", 4) || !strncmp(chroma, "UYVY", 4) ||
       !strncmp(chroma, "YV12", 4) || !strncmp(chroma, "I420", 4))) {
      bpp = 2;
   }

   frame->width = *width;
   frame->height = *height;
   frame->pitch = *width * bpp;
   frame->lines = *height;
   frame->bytes = frame->pitch * frame->lines;

   free(frame->buffer);
   frame->buffer = calloc(1, frame->bytes > 0 ? frame->bytes : 1);
   if (!frame->buffer) {
      fprintf(stderr, "libvlc: frame buffer alloc failed\n");
      return 0;
   }

   pitches[0] = frame->pitch;
   lines[0] = frame->lines;
   if (opaque) *opaque = frame;
   printf("libvlc: format callback prepared %ux%u pitch=%u bytes=%u\n",
          frame->width, frame->height, frame->pitch, frame->bytes);
   return 1;
}

static void *libvlc_log_lock(void *opaque, void **planes) {
   struct libvlc_frame_log *frame = opaque;
//   printf("libvlc: lock callback: opaque=%p\n", opaque);
   if (!frame) return NULL;
   if (!frame->buffer && frame->bytes > 0) {
      frame->buffer = calloc(1, frame->bytes);
   }
   if (!planes) return frame;
   for (int i = 0; i < 4; ++i) planes[i] = NULL;
   planes[0] = frame->buffer;
   return frame;
}

static void libvlc_log_unlock(void *opaque, void *picture, void *const *planes) {
   (void)picture;
   // printf("libvlc: unlock callback: opaque=%p planes0=%p\n", opaque,
   //        planes ? planes[0] : NULL);
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
   int vol = libvlc_volume_from_correction(volume_correction);
   snprintf(start_time, sizeof(start_time), ":volume=%lld", vol);
   if (start_us > 0) {
      char start_time[1024];
      snprintf(start_time, sizeof(start_time), ":start-time=%lld", start_us / 1000000LL);
      libvlc_media_add_option(media, start_time);
   }
   libvlc_media_player_set_media(vlc_player, media);
   libvlc_media_release(media);
   // if (start_us > 0) {
   //    printf("set time %ld\n", start_ms);
   //    libvlc_media_player_set_time(vlc_player, (libvlc_time_t)(start_ms / 1000LL));
   // }
   libvlc_media_player_play(vlc_player);
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
   char geom[64];
   if (!vlc_player) return -1;
   if (x < 0 || y < 0 || w <= x || h <= y) {
      libvlc_video_set_crop_geometry(vlc_player, NULL);
      return 0;
   }
   snprintf(geom, sizeof(geom), "%d,%d,%d,%d", x, y, w - x, h - y);
   printf("Setting crop geometry: %s\n", geom);
   libvlc_video_set_crop_geometry(vlc_player, geom);
   return 0;
}

int64_t dbus_aspect_mode(const char *mode) {
   if (!vlc_player) return -1;
   libvlc_video_set_aspect_ratio(vlc_player, mode && !strcmp(mode, "fill") ? "16:9" : "4:3");
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
