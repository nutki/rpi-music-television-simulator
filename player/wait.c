#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#include <bcm_host.h>
#include "image.h"
#include "dbus.h"
#include "dispmanx.h"
#include "terminput.h"

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

pid_t cpid = 0;
int state = 0;
int64_t duration = 0;
#define PLAYER_STOPPED 0
#define PLAYER_STARTING 1
#define PLAYER_RUNNING 2
#define PLAYER_STOPPING 3

int aspect_mode = 0;
int crop_x = -1;
int crop_y = -1;
int crop_w = -1;
int crop_h = -1;
int video_width = -1;
int video_height = -1;
int video_start_pos = 0;
int video_end_pos = -1;
int start_player(char *f, int start) {
  char start_param[32], crop_param[128];
  start /= 1000000;
  sprintf(start_param, "-l%02d:%02d:%02d", start/3600, start/60%60, start%60);
  sprintf(crop_param, "%d %d %d %d", crop_x, crop_y, crop_w + crop_x, crop_h + crop_y);
  cpid = fork();
  if (cpid == -1) {
    perror("fork");
    cpid = 0;
    return -1;
  }
  if (cpid == 0) {
    printf("Child PID is %ld\n", (long) getpid());
    execlp("omxplayer.bin","omxplayer", "--no-keys", "--no-osd", "--aspect-mode", aspect_mode ? "fill" : "letterbox", start_param, f, crop_x>=0?"--crop": 0, crop_x>=0?crop_param: 0, 0);
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
} channel_state[MAXCHANNELS];
int current_channel = 1;
int current_position = 0;

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

static char *conf_filename;
#define CONF_EXT ".conf"
void read_video_conf(const char *filename) {
  crop_w = crop_h = crop_x = crop_y = -1;
  video_start_pos = 0;
  if (conf_filename) free(conf_filename);
  char *dotptr = strrchr(filename, '.');
  int dotpos = dotptr ? dotptr - filename : strlen(filename);
  conf_filename = malloc(dotpos + sizeof(CONF_EXT));
  memcpy(conf_filename, filename, dotpos);
  memcpy(conf_filename + dotpos, CONF_EXT, sizeof(CONF_EXT));
  FILE *f = fopen(conf_filename, "r");
  if (!f) return;
  char c;
  printf("loading conf\n");
  while (fscanf(f, " %c", &c) > 0) {
    printf("Loading config %c\n", c);
    if (c == 'C') {
      fscanf(f, "%d%d%d%d", &crop_x, &crop_y, &crop_w, &crop_h);
    }
    if (c == 'S') {
      fscanf(f, "%d", &video_start_pos);
    }
  }
  printf("loading conf end\n");
  fclose(f);
}
void save_video_conf() {
  FILE *f = fopen(conf_filename, "w");
  printf("CONF: %s\n", conf_filename);
  if (!f) return;
  if (crop_w >= 0 && (crop_w != video_width || crop_h != video_height)) fprintf(f, "C %d %d %d %d\n", crop_x, crop_y, crop_w, crop_h);
  if (video_start_pos) fprintf(f, "S %d\n", video_start_pos);
  if (video_end_pos) fprintf(f, "E %d\n", video_end_pos);
  fclose(f);
}


#define CHANNEL_STATE_FILE "channels_state.txt"
void read_channels_state() {
  FILE *f = fopen(CHANNEL_STATE_FILE, "r");
  if (!f) return;
  int ch_nr, ch_pos, ch_fpos;
  fscanf(f, "%d%d", &ch_nr, &aspect_mode);
  if (!(ch_nr < 0 || ch_nr >= MAXCHANNELS)) current_channel = ch_nr;
  while(fscanf(f, "%d%d%d", &ch_nr, &ch_pos, &ch_fpos) == 3) {
    printf("%d %d %d\n", ch_nr, ch_pos, ch_fpos);
    if (ch_nr < 0 || ch_nr >= MAXCHANNELS) continue;
    channel_state[ch_nr].index = ch_pos;
    channel_state[ch_nr].position = ch_fpos;
  }
  fclose(f);
  current_position = channel_state[current_channel].position;
}
void save_channels_state() {
  FILE *f = fopen(CHANNEL_STATE_FILE, "w");
  if (!f) return;
  fprintf(f, "%d %d\n", current_channel, aspect_mode);
  for (int i = 0; i < MAXCHANNELS; i++) {
    if (channels[i].length) {
      fprintf(f, "%d %d %d\n", i, channel_state[i].index, channel_state[i].position);
    }
  }
  fclose(f);
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
  save_channels_state();
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
  read_video_conf(ch->playlist[s->index].path);
  s->position = video_start_pos * 1000;
  current_position = video_start_pos * 1000;
  save_channels_state();
  return ch->playlist + s->index;
}
struct channel_entry *channel_prev() {
  struct channel *ch = channels  + current_channel;
  struct channel_state *s = channel_state + current_channel;
  if (!ch->length) return 0;
  s->index = (s->index + ch->length - 1) % ch->length;
  read_video_conf(ch->playlist[s->index].path);
  s->position = video_start_pos * 1000;
  current_position = video_start_pos * 1000;
  save_channels_state();
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
      read_video_conf(ch->playlist[s->index].path);
      s->position = video_start_pos * 1000;
      current_position = video_start_pos * 1000;
      save_channels_state();
      printf("found at index %d\n", i);
      return 1;
    }
  }
  return 0;
}

int reload_request = 0;
static void handle_sighup(int n) {
  printf("SIGHUP\n");
  reload_request = 1;
}
static void signalHandler(int signalNumber) {
  printf("Got signal\n");
  save_channels_state();
  dbus_quit();
  dispmanx_close();
  term_cleanup();
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
int osd_timeout = 0;
void osd_show(const char * s) {
  if (osd_timeout) osd_text_clear();
  osd_timeout = 60;
  osd_text(s, 0);
}
void osd_update() {
  if (osd_timeout) {
    if(!--osd_timeout) osd_text_clear();
  }
}
void crop_cycle() {
  if (video_height <= 0 || video_width <= 0) return;
  int video_aspect = (video_height * 16 + video_width / 2)/ video_width;
  if (crop_w < 0) crop_w = video_width;
  if (crop_h < 0) crop_h = video_width;
  int crop_aspect = (crop_h * 16 + crop_w/2) / crop_w;
  int n_options = 1;
  int options[3] = { video_aspect };
  if (video_aspect != 12) options[n_options++] = 12;
  if (video_aspect != 9) options[n_options++] = 9;
  int current_aspect = 0;
  for (int i = 0; i < n_options; i++) if (crop_aspect == options[i]) current_aspect = i;
  int new_aspect = options[(current_aspect + 1) % n_options];
  if (new_aspect == video_aspect) {
    crop_w = video_width;
    crop_h = video_height;
    osd_show(video_aspect == 9 ? "NO CROP (16:9)" : video_aspect == 12 ? "NO CROP (4:3)" : "NO CROP");
  } else {
    if (new_aspect > video_aspect) {
      crop_w = video_height * 16 / new_aspect;
      crop_h = video_height;
    } else {
      crop_w = video_width;
      crop_h = video_width * new_aspect / 16;
    }
    osd_show(new_aspect == 9 ? "CROP TO 16:9" : "CROP TO 4:3");
  }
  crop_x = (video_width - crop_w) / 2;
  crop_y = (video_height - crop_h) / 2;
  dbus_crop(crop_x, crop_y, crop_w + crop_x, crop_h + crop_y);
  save_video_conf();
}
int mark_video_start() {
  video_start_pos = video_start_pos ? 0 : current_position / 1000;
  char text[256];
  sprintf(text, "START MARK: %d.%ds", video_start_pos / 1000, video_start_pos / 100 % 10);
  osd_show(video_start_pos ? text : "START MARK RESET");
  save_video_conf();
}
int mark_video_end() {
  video_end_pos = video_end_pos >= 0 ? -1 : current_position / 1000;
  char text[256];
  sprintf(text, "END MARK: %d.%ds", video_end_pos / 1000, video_end_pos / 100 % 10);
  osd_show(video_end_pos ? text : "END MARK RESET");
  save_video_conf();
  video_end_pos = -1;
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
  term_setup();
  read_channels_state();
  for(int64_t frame = 0;; frame++) {
    osd_update();
    if (state != PLAYER_STOPPED) check_ifstopped();
    switch (state) {
      case PLAYER_STOPPED:
        ;
        struct channel_entry *ce = changing_video ? channel_current_entry() : channel_next();
        changing_video = false;
        start_player(ce->path, current_position);
        custom_show_strap_pos = -STRAP_DURATION_SEC * 1000LL * 1000LL;
        dispmanx_alpha(0);
        load_strap(ce->path);
        break;
      case PLAYER_STARTING:
        duration = query("Duration");
        if (duration > 0) {
          video_width = query("ResWidth");
          video_height = query("ResHeight");
          state = PLAYER_RUNNING;
        }
        break;
      case PLAYER_RUNNING:
        current_position = query("Position");
        if (current_position > 0) {
          channel_state[current_channel].position = current_position;
          printf("%10.3f/%10.3f\r", current_position / 1000000., duration / 1000000.); fflush(stdout);
          double a = strap_alpha(current_position, 2 * 1000 * 1000 + video_start_pos * 1000);
          double b = strap_alpha(current_position, duration - (2 + STRAP_DURATION_SEC) * 1000 * 1000);
          double max = a > b ? a : b;
          double c = strap_alpha(current_position, custom_show_strap_pos);
          if (c > max) max = c;
          dispmanx_alpha((int)(max * 200));
        }
        int keycode = 0;
        while ((keycode = term_getkey()) > 0) {
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
              custom_show_strap_pos = current_position;
          }
          if (keycode == ',' || keycode == '.') {
            dbus_seek(keycode == ',' ? -30 * 1000000LL : 30 * 1000000LL);
            osd_show(keycode == ',' ? "SEEK -30s" : "SEEK +30s");
          }
          if (keycode == 'q') {
            signalHandler(0);
          }
          if (keycode == 'c') {
            crop_cycle();
          }
          if (keycode == 'x') {
            aspect_mode = !aspect_mode;
            dbus_aspect_mode(aspect_mode ? "fill" : "letterbox");
            osd_show(aspect_mode ? "ASPECT: FILL FRAME" : "ASPECT: LETTERBOX");
            save_channels_state();
          }
          if (keycode == 'b') {
            mark_video_start();
          }
          if (keycode == 'n') {
            mark_video_end();
          }
        }
        if (channel_set_file(handle_requests())) {
          dbus_quit();
          state = PLAYER_STOPPING;
          changing_video = true;
        }
        if (video_end_pos != -1 && current_position >= video_end_pos * 1000) {
          dbus_quit();
          state = PLAYER_STOPPING;
        }
        break;
    }
    usleep(1000000 / 60);
  }
}
