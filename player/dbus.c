#include <dbus/dbus.h>
#include <stdlib.h>
#include <stdio.h>
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
