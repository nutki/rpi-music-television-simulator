#include <dbus/dbus.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#include <bcm_host.h>
#include "image.h"
#include <termios.h>

/* Playlist setup */

#define MAX_PLAYLIST_CALC (64*1024)
uint64_t rand_seed;
uint32_t rand_next () {
  rand_seed = rand_seed * 6364136223846793005ll + 1;
  return rand_seed >> 32;
}



static int random_order[MAX_PLAYLIST_CALC];
static int playlist_len = 1;
static int playlist_is_random;
static void playlist_init(int len, int random, int channel) {
  playlist_len = len;
  playlist_is_random = random;
  rand_seed = channel * 7 + 12345;
  if (playlist_len > 0) for (int i = 0; i < MAX_PLAYLIST_CALC; i++) {
    random_order[i] = rand_next() % playlist_len;
  }
}
int playlist_update(int newlen, int oldpos, int newpos) {
  // todo
  return 0; // new pos
}
int playlist_set_random(int random) {
  playlist_is_random = random;
  return 0; // new pos
}
int playlist_current(int pos) {
  if (!playlist_len) return 0;
  if (playlist_is_random) {
    return random_order[pos % MAX_PLAYLIST_CALC];
  } else {
    return pos % playlist_len;
  }
}


/* Channel setup END */

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
static char empty[SCREENY * 4];

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
static void blank_background()
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

   printf(">>> CMD %s\n", action_name);
   // create a new method call and check for errors
   msg = dbus_message_new_method_call("org.mpris.MediaPlayer2.omxplayer", // target for the method call
                                      "/org/mpris/MediaPlayer2", // object to call on
                                      "org.mpris.MediaPlayer2.Player", // interface to call on
                                      action_name); // method name
   printf(">>> END\n");
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


   printf("Got Reply: %lld, %s\n", val, stat);

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


   printf("Got Reply: %lld, %s\n", val, stat);

   // free reply
   dbus_message_unref(msg);
   return val;
}


pid_t cpid = 0;
int state = 0;
int64_t duration = 0, position = 0;
int playlist_pos = 0;
#define PLAYER_STOPPED 0
#define PLAYER_STARTING 1
#define PLAYER_RUNNING 2
#define PLAYER_STOPPING 3

int start_player(char *f, int start) {
  char start_param[32];
  sprintf(start_param, "-l%02d:%02d:%02d", start/3600, start/60%60, start%60);
  cpid = fork();
  if (cpid == -1) {
    perror("fork");
    cpid = 0;
    return -1;
  }
  if (cpid == 0) {
    printf("Child PID is %ld\n", (long) getpid());
    execlp("omxplayer.bin","omxplayer", "--no-keys", "--no-osd", "-b", "--aspect-mode", "fill", start_param, f, 0);
    perror("exec omxplayer\n");
    exit(EXIT_FAILURE);
  }
  state = PLAYER_STARTING;
  return 0;
}
int check_ifstopped() {
  int status;
  pid_t w = waitpid(cpid, &status, WNOHANG);
  if (w == -1) {
      perror("waitpid");
      return -1;
  } else if (w > 0) {
    if (WIFEXITED(status)) {
      printf("player exited, status=%d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      printf("player killed by signal %d\n", WTERMSIG(status));
    }
    state = PLAYER_STOPPED;
  }
  return 0;
}

#define STRAP_DURATION_SEC 7
double strap_alpha(int64_t now, int64_t start) {
  double diff = (now - start) / 1000. / 1000.;
  if (diff <= 0) return 0;
  else if (diff <= 1) return diff;
  else if (diff <= STRAP_DURATION_SEC - 1) return 1;
  else if (diff <= STRAP_DURATION_SEC) return STRAP_DURATION_SEC - diff;
  else return 0;
}

struct {
  char *file;
} playlist[] = {
  { "/home/pi/Remind Me-2285902.mp4" },
  { "/home/pi/Ugly Kid Joe - Cats In The Cradle-B32yjbCSVpU.mp4" },
  { "/home/pi/Enigma - Return To Innocence-Rk_sAHh9s08.mp4" },
};
#define MAXCHANNELS 100
struct channel {
  char *logopath;
  int length;
  struct channel_entry {
    char *path;
    int flags;
  } *playlist;
} channels[MAXCHANNELS];

struct channel_state {
  int index;
  int position;
  int seed; // ?
} channel_state[MAXCHANNELS];
int current_channel = 1;

int read_channels(char *path, struct channel *channels) {
  char linebuf[1024], *s;
  FILE *f = fopen(path, "r");
  if (!f) {
    printf("cannot open %s\n", path);
    return -1;
  }
  int current_channel = 0;
  for (int ln = 1; (s = fgets(linebuf, sizeof(linebuf), f)); ln++) {
    int len = strlen(s);
    if (len == sizeof(linebuf) - 1) {
      printf("Line too long %s:%d\n", path, ln);
      return -1;
    }
    while (len && isspace(s[len - 1])) s[--len] = '\0';
    while (isspace(*s)) s++;
    if (*s == '#') {
      current_channel = atoi(s+1);
      if (current_channel < 0 || current_channel >= MAXCHANNELS) {
        printf("Bad channel number %d at %s:%d\n", current_channel, path, ln);
        current_channel = 0;
      }
    } else {
      int pos = channels[current_channel].length;
      if (!pos) channels[current_channel].playlist = malloc(sizeof(struct channel_entry));
      else if (!(pos & pos - 1)) channels[current_channel].playlist = realloc(channels[current_channel].playlist, pos * 2 * sizeof(struct channel_entry));
      if (!channels[current_channel].playlist) {
        printf("OOM\n");
        return -1;
      }
      struct channel_entry *newentry = &channels[current_channel].playlist[pos];
      newentry->path = strdup(s);
      if (!newentry->path) {
        printf("OOM\n");
        return -1;
      }
      newentry->flags = 0;
      channels[current_channel].length++;
    }
  }
  return 0;
}

void free_channels(struct channel *channels) {
  for (int i = 0; i < MAXCHANNELS; i++) {
    for (int j = 0; j < channels[i].length; j++) if (channels[i].length) {
      free(channels[i].playlist[j].path);
    }
    free(channels[i].playlist);
  }
}

void reload_channels() {
  struct channel new_channels[MAXCHANNELS];
  memset(new_channels, 0, sizeof(new_channels));
  read_channels("channels.txt", new_channels);
  for (int i = 0; i < MAXCHANNELS; i++) if (channels[i].length) {
    char *current_path = channels[i].playlist[channel_state[i].index].path;
    int new_pos = 0;
    for (int j = 0; j < new_channels[i].length; j++) {
      if (!strcmp(current_path, new_channels[i].playlist[j].path)) {
        new_pos = j;
        printf("channel %d pos %d is now %d \n", i, channel_state[i].index, j);
        break;
      }
    }
    channel_state[i].index = new_pos;
  }
  free_channels(channels);
  memcpy(channels, new_channels, sizeof(new_channels));
}

void print_channels() {
  for (int i = 0; i < MAXCHANNELS; i++) {
    int len = channels[i].length;
    if (!len) continue;
    printf("# %d\n", i);
    for (int j = 0; j < len; j++) {
      // printf("%s\n", channels[i].playlist[j].path);
    }
  }
}

struct channel_entry *channel_current_entry() {
  struct channel *ch = channels  + current_channel;
  struct channel_state *s = channel_state + current_channel;
  if (!ch->length) return 0;
  return ch->playlist + s->index;
}
struct channel_entry *channel_next() {
  struct channel *ch = channels  + current_channel;
  struct channel_state *s = channel_state + current_channel;
  if (!ch->length) return 0;
  s->index = (s->index + 1) % ch->length;
  s->position = 0;
  return ch->playlist + s->index;
}
struct channel_entry *channel_prev() {
  struct channel *ch = channels  + current_channel;
  struct channel_state *s = channel_state + current_channel;
  if (!ch->length) return 0;
  s->index = (s->index + ch->length - 1) % ch->length;
  s->position = 0;
  return ch->playlist + s->index;
}
struct channel_entry *channel_up(int current_pos) {}
struct channel_entry *channel_down(int current_pos) {}
struct channel_entry *channel_select(int nr, int current_pos) {}
struct channel_entry *channel_select_prefix(int nr, int current_pos) {}

int is_video_suffix(char *name) {
  if (!strcmp(name, ".mkv")) return 1;
  if (!strcmp(name, ".mp4")) return 1;
  if (!strcmp(name, ".mov")) return 1;
  return 0;
}

int channel_set_file(char *file) {
  struct channel *ch = channels  + current_channel;
  struct channel_state *s = channel_state + current_channel;
  if (!file) return 0;
  printf("should play: >>%s<< \n", file);
  for (int i = 0; i < ch->length; i++) {
    if (!strncmp(ch->playlist[i].path, file, strlen(file)) && is_video_suffix(ch->playlist[i].path + strlen(file))) {
      s->index = i;
      s->position = 0;
      printf("found at index %d\n", i);
      return 1;
    }
  }
  return 0;
}

struct termios old, new;
int fd = 0;
int oldflags = 0;
void kbd_error(char *text) {
  perror(text);
  exit(EXIT_FAILURE);
}
void setup() {
	if (tcgetattr(fd, &old) == -1) kbd_error("tcgetattr");
  new = old;
//	new.c_lflag &= ~((tcflag_t)(ICANON | ECHO | ISIG));
	new.c_lflag &= ~((tcflag_t)(ICANON | ECHO));
  printf("TERMIOS %d %d %04x\n", new.c_cc[VMIN], new.c_cc[VTIME], new.c_iflag);
//	new.c_iflag     = 0;
	new.c_cc[VMIN]  = 0;
	new.c_cc[VTIME] = 0; /* 0.1 sec intercharacter timeout */
	if (tcsetattr(fd, TCSAFLUSH, &new) == -1) kbd_error("tcsetattr");
  oldflags = fcntl(0, F_GETFL, 0);
//  fcntl(0, F_SETFL, oldflags | O_NONBLOCK);
}
void cleanup() {
  if (tcsetattr(fd, 0, &old) == -1) perror("tcsetattr");
  if (fcntl(0, F_SETFL, oldflags)) perror("fcntl");
}

int reload_request = 0;
static void handle_sighup(int n) {
  printf("SIGHUP\n");
  reload_request = 1;
}
static void signalHandler(int signalNumber) {
  printf("Got signal\n");
  dbus_quit();
  dispmanx_close();
  cleanup();
  exit(EXIT_SUCCESS);
}
char *handle_requests() {
  char linebuf[1024];
  if (reload_request) {
    reload_request = 0;
    printf("Handling requests\n");
    reload_channels();
    FILE *f = fopen("/tmp/.mpv.playnow", "r+");
    if (f) {
      char *s = fgets(linebuf, sizeof(linebuf), f);
      fclose(f);
      remove("/tmp/.mpv.playnow");
      return s;
    }
  }
  return 0;
}

int getchar2() {
  char c = 0;
  read(0, &c, 1);
  return c;
}

int main(int argc, char *argv[]) {
  read_channels("channels.txt", channels);
  print_channels();
  signal(SIGHUP, handle_sighup);
  FILE *pidfile = fopen("/tmp/.mpv.pid", "w+");
  fprintf(pidfile, "%d\n", getpid());
  fclose(pidfile);
  int64_t custom_show_strap_pos = 0;
  bool changing_video = true;
  if (signal(SIGINT, signalHandler) == SIG_ERR || signal(SIGTERM, signalHandler) == SIG_ERR) {
    perror("installing signal handler");
    exit(EXIT_FAILURE);
  }
  dispmanx_init();
  blank_background();
  dbus_init();
  setup();
  for(int64_t frame = 0;; frame++) {
    if (state != PLAYER_STOPPED) check_ifstopped();
    switch (state) {
      case PLAYER_STOPPED:
        ;
        struct channel_entry *ce = changing_video ? channel_current_entry() : channel_next();
        changing_video = false;
        start_player(ce->path, 0);
        custom_show_strap_pos = -STRAP_DURATION_SEC * 1000LL * 1000LL;
        dispmanx_alpha(0);
        load_strap(ce->path);
        break;
      case PLAYER_STARTING:
        duration = query("Duration");
        if (duration > 0) state = PLAYER_RUNNING;
        break;
      case PLAYER_RUNNING:
        position = query("Position");
        if (position > 0) {
          printf("%10.3f/%10.3f\r", position / 1000000., duration / 1000000.); fflush(stdout);
          double dpos = position / 1000./1000.;
          double a = strap_alpha(position, 2 * 1000 * 1000);
          double b = strap_alpha(position, duration - (2 + STRAP_DURATION_SEC) * 1000 * 1000);
          double max = a > b ? a : b;
          double c = strap_alpha(position, custom_show_strap_pos);
          if (c > max) max = c;
          dispmanx_alpha((int)(max * 200));
        }
        int keycode = 0;
        while ((keycode = getchar2()) > 0) {
          printf("GOT %d\n", keycode);
          if (keycode == 'a' || keycode == 'd') {
            if (keycode == 'a') channel_prev();
            if (keycode == 'd') channel_next();
            dbus_quit();
            state = PLAYER_STOPPING;
            changing_video = true;
          }
          if (keycode == ' ') {
            dbus_action("PlayPause");
          }
          if (keycode == 'i') {
            //if (custom_show_strap_pos + STRAP_DURATION_SEC * 1000LL * 1000LL >= position)
              custom_show_strap_pos = position;
          }
          if (keycode == ',' || keycode == '.') {
            dbus_seek(keycode == ',' ? -30 * 1000000LL : 30 * 1000000LL);
          }
          if (keycode == 'q') {
            signalHandler(0);
          }
        }
        if (channel_set_file(handle_requests())) {
          dbus_quit();
          state = PLAYER_STOPPING;
          changing_video = true;
        }
        if (position > 2000 * 1000000LL) {
          dbus_quit();
          state = PLAYER_STOPPING;
        }
        break;
    }
    usleep(1000000 / 60);
  }
}
