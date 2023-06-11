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
#include "comm.h"

#define OMXPLAYER_PATH "omxplayer.bin"
#define CHANNELS_PATH "channels"
#define VIDEO_PATH "./videos"
#define CHANNEL_STATE_FILE "channels_state.txt"
#define COMM_SOCKET_PATH  "/tmp/.mpv.socket"

/* Playlist setup */

#define MAX_PLAYLIST_CALC (64*1024)
uint64_t rand_seed;
uint32_t rand_next () {
  rand_seed = rand_seed * 6364136223846793005ll + 1;
  return rand_seed >> 32;
}

static int random_order[MAX_PLAYLIST_CALC];
static int playlist_len = 1;
static void playlist_init(int len, int random, int channel) {
  playlist_len = len;
  rand_seed = channel * 7 + 12345;
  if (playlist_len > 0) for (int i = 0; i < MAX_PLAYLIST_CALC; i++) {
    random_order[i] = rand_next() % playlist_len;
  }
}


/* Channel setup END */

pid_t cpid = 0; // PID of the omxplayer process
int state = 0; // Player state (one of PLAYER_*)
int64_t duration = 0; // Current video duration in us queried after player starts
#define PLAYER_STOPPED 0
#define PLAYER_STARTING 1
#define PLAYER_RUNNING 2
#define PLAYER_STOPPING 3

int aspect_mode = 0; // Letterbox = 0, fit screen = 1
int show_subtitles = 0;
int has_subtitles = 0;
int crop_x = -1;
int crop_y = -1;
int crop_w = -1;
int crop_h = -1;
int video_width = -1;
int video_height = -1;
int video_start_pos = 0;
int video_end_pos = -1;
bool video_end_was_set = false;
int volume_correction = 0;
int start_player(char *f, int start) {
  char start_param[32], crop_param[128], volume_param[32];
  start /= 1000000;
  sprintf(start_param, "-l%02d:%02d:%02d", start/3600, start/60%60, start%60);
  sprintf(crop_param, "%d %d %d %d", crop_x, crop_y, crop_w + crop_x, crop_h + crop_y);
  sprintf(volume_param, "%d", volume_correction);
  cpid = fork();
  if (cpid == -1) {
    perror("fork");
    cpid = 0;
    return -1;
  }
  if (cpid == 0) {
    printf("Child PID is %ld\n", (long) getpid());
    execlp(OMXPLAYER_PATH, "omxplayer", "--vol", volume_param, "--align", "center", "--no-keys", "--no-osd", "--aspect-mode", aspect_mode ? "fill" : "letterbox", start_param, f, crop_x>=0?"--crop": 0, crop_x>=0?crop_param: 0, 0);
    perror("exec omxplayer\n");
    exit(EXIT_FAILURE);
  }
  state = PLAYER_STARTING;
  return 0;
}
int64_t last_reported_position = -1;
int check_ifstopped() {
  int status;
  if (cpid <= 0) return 0;
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
    cpid = 0;
    printf("Playback stoped at %.3f of %.3f\n", last_reported_position/1000000., duration/1000000.);
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

// struct {
//   char *file;
// } playlist[] = {
//   { "/home/pi/Remind Me-2285902.mp4" },
//   { "/home/pi/Ugly Kid Joe - Cats In The Cradle-B32yjbCSVpU.mp4" },
//   { "/home/pi/Enigma - Return To Innocence-Rk_sAHh9s08.mp4" },
// };
#define MAXCHANNELS 100
#define CHANNELDIGITS 2
struct channel {
  char *name;
  int length;
  struct channel_entry {
    char *path;
  } *playlist;
} channels[MAXCHANNELS];

struct channel_state {
  int index;
  int position;
  int is_random;
} channel_state[MAXCHANNELS];
int current_channel = 0;
int current_position = 0; // Play position in the current video in us
bool channel_is_random = false;

int read_channel(struct channel *channel, int current_channel) {
  char linebuf[1024], *s, tmp[1024*2];
  sprintf(linebuf, "%s/%d.txt", CHANNELS_PATH, current_channel);
  FILE *f = fopen(linebuf, "r");
  if (!f) return 0;
  printf("loading channel %d\n", current_channel);
  channel->length = 0;
  channel->name = 0;
  channel->playlist = 0;
  for (int ln = 1; (s = fgets(linebuf, sizeof(linebuf), f)); ln++) {
    int len = strlen(s);
    if (len == sizeof(linebuf) - 1) {
      printf("Line too long %s/%d.txt:%d\n", CHANNELS_PATH, current_channel, ln);
      return -1;
    }
    while (len && isspace(s[len - 1])) s[--len] = '\0';
    while (isspace(*s)) s++;
    if (ln == 1) {
      channel->name = strdup(s);
    } else {
      int pos = channel->length;
      if (!pos) channel->playlist = malloc(sizeof(struct channel_entry));
      else if (!(pos & pos - 1)) channel->playlist = realloc(channel->playlist, pos * 2 * sizeof(struct channel_entry));
      if (!channel->playlist) {
        printf("OOM\n");
        return -1;
      }
      struct channel_entry *newentry = &channel->playlist[pos];
      sprintf(tmp, "%s/%s", VIDEO_PATH, s);
      newentry->path = strdup(tmp);
      if (!newentry->path) {
        printf("OOM\n");
        return -1;
      }
      channel->length++;
    }
  }
  return 0;
}
int read_channels(struct channel *channels) {
  for (int i = 0; i < MAXCHANNELS; i++) {
    if (read_channel(channels + i, i) < 0) return -1;
  }
  return 0;
}

static char *conf_filename;
#define CONF_EXT ".conf"
void read_video_conf(const char *filename) {
  crop_w = crop_h = crop_x = crop_y = -1;
  video_start_pos = 0;
  video_end_pos = -1;
  video_end_was_set = false;
  volume_correction = 0;
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
    if (c == 'E') {
      fscanf(f, "%d", &video_end_pos);
    }
    if (c == 'V') {
      fscanf(f, "%d", &volume_correction);
    }
  }
  printf("loading conf end (%s)\n", conf_filename);
  fclose(f);
}
void save_video_conf() {
  FILE *f = fopen(conf_filename, "w");
  if (!f) return;
  int c = 0;
  if (crop_w >= 0 && (crop_w != video_width || crop_h != video_height)) fprintf(f, "C %d %d %d %d\n", crop_x, crop_y, crop_w, crop_h), c++;
  if (video_start_pos) fprintf(f, "S %d\n", video_start_pos), c++;
  if (video_end_pos != -1) fprintf(f, "E %d\n", video_end_pos), c++;
  if (volume_correction) fprintf(f, "V %d\n", volume_correction), c++;
  fclose(f);
  if (!c) unlink(conf_filename);
}


void read_channels_state() {
  FILE *f = fopen(CHANNEL_STATE_FILE, "r");
  if (!f) return;
  int ch_nr, ch_pos, ch_fpos, ch_random;
  fscanf(f, "%d%d%d", &ch_nr, &aspect_mode, &show_subtitles);
  if (!(ch_nr < 0 || ch_nr >= MAXCHANNELS)) current_channel = ch_nr;
  while(fscanf(f, "%d%d%d%d", &ch_nr, &ch_pos, &ch_fpos, &ch_random) == 4) {
    printf("%d %d %d\n", ch_nr, ch_pos, ch_fpos);
    if (ch_nr < 0 || ch_nr >= MAXCHANNELS) continue;
    channel_state[ch_nr].index = ch_pos;
    channel_state[ch_nr].position = ch_fpos;
    channel_state[ch_nr].is_random = ch_random;
  }
  fclose(f);
  current_position = channel_state[current_channel].position;
  channel_is_random = channel_state[current_channel].is_random;
}
void save_channels_state() {
  FILE *f = fopen(CHANNEL_STATE_FILE, "w");
  if (!f) return;
  fprintf(f, "%d %d %d\n", current_channel, aspect_mode, show_subtitles);
  for (int i = 0; i < MAXCHANNELS; i++) {
    if (channels[i].length) {
      fprintf(f, "%d %d %d %d\n", i, channel_state[i].index, channel_state[i].position, channel_state[i].is_random);
    }
  }
  fclose(f);
}

void free_channel(struct channel * channel) {
  for (int j = 0; j < channel->length; j++) if (channel->length) {
    free(channel->playlist[j].path);
  }
  channel->length = 0;
  if (channel->playlist) free(channel->playlist);
  channel->playlist = 0;
  if (channel->name) free(channel->name);
  channel->name = 0;
}
void free_channels(struct channel *channels) {
  for (int i = 0; i < MAXCHANNELS; i++) {
    free_channel(channels + i);
  }
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
void show_channel() {
  char buf[128];
  char *name = channels[current_channel].name;
  if (!name) name = "<NO SIGNAL>";
  snprintf(buf, 127, "CH %*d %s", CHANNELDIGITS, current_channel, name);
  osd_show(buf);
  printf("Switching to channel %d (%s) state =%d\n", current_channel, name, state);
}
void show_channel_prefix(int channel_digits, int channel_prefix) {
  char buf[128];
  snprintf(buf, 127, "CH %.*s%0*d", CHANNELDIGITS - channel_digits, "--------------", channel_digits, channel_prefix);
  osd_show(buf);
  osd_timeout = 1000;
}

bool changing_video = true;

uint32_t inthash(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

int channel_convert_index(int ch, int len, int i) {
  if (channel_is_random) {
    return inthash((ch << 24) ^ (len << 12) ^ i)%len;
  }
  int mod = i % len;
  return mod < 0 ? mod + len : mod;
}
int channel_reverse_convert_index(int ch, int len, int i, int old) {
  for (int j = 0; j < 10 * len; j++) {
    int cand = j%2 ? old - j/2 - 1: old + j/2;
    if (i == channel_convert_index(ch, len, cand)) {
      printf("Searching for index %d in channel %d, found %d (refrence %d)\n", i, ch, cand, old);
      return cand;
    }
  }
  printf("Reverse search failed for channel %d index %d\n", ch, i);
  return old;
}

void channel_set_random(bool v) {
  if (v == channel_is_random) return;
  struct channel *ch = channels + current_channel;
  struct channel_state *s = channel_state + current_channel;
  int i = channel_convert_index(current_channel, ch->length, s->index);
  s->is_random = channel_is_random = v;
  s->index = channel_reverse_convert_index(current_channel, ch->length, i, s->index);
  save_channels_state();
}

int channel_find_file(int nr, char *file) {
  struct channel *ch = channels + nr;
  for (int i = 0; i < ch->length; i++) {
    if (!strcmp(ch->playlist[i].path, file)) {
      channel_state[nr].index = channel_reverse_convert_index(nr, ch->length, i, channel_state[nr].index);
      return 1;
    }
  }
  return 0;
}

struct channel_entry *channel_entry(int nr) {
  struct channel *ch = channels + nr;
  struct channel_state *s = channel_state + nr;
  if (!ch->length) return 0;
  return ch->playlist + channel_convert_index(nr, ch->length, s->index);
}
struct channel_entry *channel_current_entry() {
  return channel_entry(current_channel);
}
struct channel_entry *channel_next() {
  struct channel *ch = channels  + current_channel;
  struct channel_state *s = channel_state + current_channel;
  if (!ch->length) return 0;
  s->index++;
  struct channel_entry *ce = channel_current_entry();
  read_video_conf(ce->path);
  s->position = video_start_pos * 1000;
  current_position = video_start_pos * 1000;
  save_channels_state();
  return ce;
}
struct channel_entry *channel_prev() {
  struct channel *ch = channels  + current_channel;
  struct channel_state *s = channel_state + current_channel;
  if (!ch->length) return 0;
  s->index--;
  struct channel_entry *ce = channel_current_entry();
  read_video_conf(ce->path);
  s->position = video_start_pos * 1000;
  current_position = video_start_pos * 1000;
  save_channels_state();
  return ce;
}
void switch_to_channel(int nr) {
  if (nr < 0 || nr >= MAXCHANNELS || nr == current_channel) {
    osd_text_clear();
    return;
  }
  printf("Switching to channel %d\n", nr);
  current_channel = nr;
  struct channel_state *s = channel_state + current_channel;
  struct channel_entry *ce = channel_current_entry();
  if (ce) {
    read_video_conf(ce->path);
    current_position = s->position;
    channel_is_random = s->is_random;
  }
  save_channels_state();
  show_channel();
  dispmanx_alpha(0);
  bg_mode(BG_MODE_BLUE);
  if (state == PLAYER_RUNNING) {
    dbus_quit();
    state = PLAYER_STOPPING;
  }
  changing_video = true;
}
void channel_up(void) {
  switch_to_channel(current_channel == MAXCHANNELS - 1 ? 0 : current_channel + 1);
}
void channel_down(void) {
  switch_to_channel(current_channel == 0 ? MAXCHANNELS - 1 : current_channel - 1);
}
int channel_prefix = 0;
int channel_digits = 0;
int channel_switch_timeout = 0;
void channel_select(int nr) {
  channel_digits++;
  channel_prefix *= 10;
  channel_prefix += nr;
  if (channel_digits == CHANNELDIGITS || channel_prefix * 10 >= MAXCHANNELS) {
    switch_to_channel(channel_prefix);
    channel_prefix = channel_digits = channel_switch_timeout = 0;
  } else {
    show_channel_prefix(channel_digits, channel_prefix);
    channel_switch_timeout = 100;
  }
}
void channel_select_tick() {
  if (channel_switch_timeout && !--channel_switch_timeout) {
    switch_to_channel(channel_prefix);
    channel_prefix = channel_digits = 0;
  }
}

void channel_set_file(char *file) {
  printf("should play: >>%s<< \n", file);
  if (!channel_find_file(current_channel, file)) {
    if (!channel_find_file(0, file)) {
      return;
    }
    current_channel = 0;
    channel_is_random = channel_state[current_channel].is_random;
  }
  struct channel_state *s = channel_state + current_channel;
  struct channel_entry *ce = channel_current_entry();
  read_video_conf(ce->path);
  s->position = video_start_pos * 1000;
  current_position = video_start_pos * 1000;
  save_channels_state();
  printf("found at index %d\n", s->index);
  if (state == PLAYER_RUNNING || state == PLAYER_STARTING) {
    dbus_quit();
    state = PLAYER_STOPPING;
  }
  changing_video = true;
}
void reload_channel(int i) {
  struct channel *ch = channels + i;
  struct channel old_channel = *ch;
  struct channel_entry *old_ce = channel_entry(i);
  read_channel(ch, i);
  if (ch->length) {
    if (!old_ce || !channel_find_file(i, old_ce->path)) {
      channel_state[i].index = 0;
      channel_state[i].position = 0;
    }
  }
  free_channel(&old_channel);
  save_channels_state();
}


static void signalHandler(int signalNumber) {
  printf("Got signal\n");
  save_channels_state();
  dbus_quit();
  dispmanx_close();
  comm_close();
  term_cleanup();
  exit(EXIT_SUCCESS);
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
void mark_video_start() {
  video_start_pos = video_start_pos ? 0 : current_position / 1000;
  char text[256];
  sprintf(text, "START MARK: %d.%ds", video_start_pos / 1000, video_start_pos / 100 % 10);
  osd_show(video_start_pos ? text : "START MARK RESET");
  save_video_conf();
}
void mark_video_end() {
  video_end_pos = video_end_pos >= 0 ? -1 : current_position / 1000;
  char text[256];
  sprintf(text, "END MARK: %d.%ds", video_end_pos / 1000, video_end_pos / 100 % 10);
  osd_show(video_end_pos >= 0 ? text : "END MARK RESET");
  save_video_conf();
  video_end_was_set = true;
}
void seek_video(int s) {
  dbus_seek(s * 1000000LL);
  int pos = current_position/1000000LL + s;
  int len = duration/1000000LL;
  char buf[128];
  if (pos<0) pos=0;
  if (pos>len) pos=len;
  sprintf(buf, "SEEK %ds: (%d:%02d/%d:%02d)", s, pos / 60, pos % 60, len / 60, len %60);
  osd_show(buf);
}
int start_volume_detect(void) {
  char *f = channel_current_entry()->path;
  int cpid = fork();
  if (cpid == -1) {
    perror("fork");
    cpid = 0;
    return -1;
  }
  if (cpid == 0) {
    execlp("node", "node", "webserver/volume_correct.js", f, 0);
    perror("exec node\n");
    exit(EXIT_FAILURE);
  }
  osd_show("VOLUME DETECT");
  return 0;
}
void set_video_volume(int s, bool absolute) {
  volume_correction = absolute ? s : volume_correction + s;
  dbus_volume(volume_correction);
  char buf[128];
  sprintf(buf, "VOL %+.1fdB", volume_correction/100.);
  osd_show(buf);
  save_video_conf();
}
void channel_toggle_random() {
  channel_set_random(!channel_is_random);
  osd_show(channel_is_random ? "SHUFFLE: ON": "SHUFFLE: OFF");
}
#define SUBTITLE_EXT ".srt"
void check_subtitles(char *filename) {
  char *subtitle_filename;
  char *dotptr = strrchr(filename, '.');
  int dotpos = dotptr ? dotptr - filename : strlen(filename);
  subtitle_filename = malloc(dotpos + sizeof(SUBTITLE_EXT));
  memcpy(subtitle_filename, filename, dotpos);
  memcpy(subtitle_filename + dotpos, SUBTITLE_EXT, sizeof(SUBTITLE_EXT));
  has_subtitles = access(subtitle_filename, F_OK) == 0;
}
void set_subtitles() {
  if (has_subtitles) dbus_action(show_subtitles ? "ShowSubtitles" : "HideSubtitles");
}
int64_t custom_show_strap_pos = 0;
void handle_keycode(int keycode) {
    if (state == PLAYER_STOPPED || state == PLAYER_RUNNING) {
      if (keycode == 'a' || keycode == 'd') {
        if (keycode == 'a') channel_prev();
        if (keycode == 'd') channel_next();
        if (state == PLAYER_RUNNING) {
          dbus_quit();
          state = PLAYER_STOPPING;
        }
        changing_video = true;
        dispmanx_alpha(0);
      }
      if (keycode == 'w') channel_up();
      if (keycode == 's') channel_down();
      if (keycode >= '0' && keycode <= '9') channel_select(keycode - '0');
      if (keycode == 'r') channel_toggle_random();
    }
    if (state == PLAYER_RUNNING) {
      if (keycode == ' ') {
        dbus_action("PlayPause");
      }
      if (keycode == 'i') {
        //if (custom_show_strap_pos + STRAP_DURATION_SEC * 1000LL * 1000LL >= position)
          custom_show_strap_pos = current_position;
      }
      if (keycode == ',') seek_video(-30);
      if (keycode == '.') seek_video(30);
      if (keycode == '<') seek_video(-5);
      if (keycode == '>') seek_video(5);
      if (keycode == '[') set_video_volume(-50, false);
      if (keycode == ']') set_video_volume(50, false);
      if (keycode == '{') set_video_volume(0, true);
      if (keycode == '}') start_volume_detect();
      if (keycode == 'c') {
        crop_cycle();
      }
      if (keycode == 'x') {
        aspect_mode = !aspect_mode;
        dbus_aspect_mode(aspect_mode ? "fill" : "letterbox");
        osd_show(aspect_mode ? "ASPECT: FILL FRAME" : "ASPECT: LETTERBOX");
        save_channels_state();
      }
      if (keycode == 't') {
        show_subtitles = !show_subtitles;
        set_subtitles();
        osd_show(show_subtitles ? "SUBTITLES ON" : "SUBTITLES OFF");
        save_channels_state();
      }
      if (keycode == 'b') {
        mark_video_start();
      }
      if (keycode == 'n') {
        mark_video_end();
      }
    }
    if (keycode == 'q') {
      signalHandler(0);
    }
}
void process_input(void) {
  int keycode = 0;
  while ((keycode = term_getkey()) > 0) {
    printf("GOT %d\n", keycode);
    handle_keycode(keycode);
  }
  char *msg;
  while(msg = comm_read()) {
    printf("%s\n", msg);
    if (msg[0] == 'P') {
      channel_set_file(msg + 1);
    }
    if (msg[0] == 'C') {
      printf("Handling requests\n");
      reload_channel(atoi(msg + 1));
    }
    if (msg[0] == 'K') {
      handle_keycode(msg[1]);
    }
    if (msg[0] == 'D') {
      osd_show(msg + 1);
    }
    if (msg[0] == 'V') {
      set_video_volume(atoi(msg + 1), true);
    }
  }
}
int main(int argc, char *argv[]) {
  read_channels(channels);
  if (signal(SIGINT, signalHandler) == SIG_ERR || signal(SIGTERM, signalHandler) == SIG_ERR) {
    perror("installing signal handler");
    exit(EXIT_FAILURE);
  }
  dispmanx_init();
  blank_background();
  dbus_init();
  comm_init(COMM_SOCKET_PATH);
  term_setup();
  read_channels_state();
  struct channel_entry *ce = channel_current_entry();
  read_video_conf(ce->path);
  for(int64_t frame = 0;; frame++) {
    osd_update();
    if (state != PLAYER_STOPPED) check_ifstopped();
    switch (state) {
      case PLAYER_STOPPED:
        ;
        struct channel_entry *ce = changing_video ? channel_current_entry() : channel_next();
        dispmanx_alpha(0);
        if (!ce) {
          bg_mode(BG_MODE_NOISE);
          break;
        }
        changing_video = false;
        start_player(ce->path, current_position);
        custom_show_strap_pos = -STRAP_DURATION_SEC * 1000LL * 1000LL;
        load_strap(ce->path);
        break;
      case PLAYER_STARTING:
        duration = query("Duration");
        if (duration > 0) {
          video_width = query("ResWidth");
          video_height = query("ResHeight");
          check_subtitles(ce->path);
          set_subtitles();
          state = PLAYER_RUNNING;
          bg_mode(BG_MODE_BLACK);
        }
        break;
      case PLAYER_RUNNING:
        current_position = query("Position");
        if (current_position > 0) {
          last_reported_position = current_position;
          channel_state[current_channel].position = current_position;
//          printf("%10.3f/%10.3f\r", current_position / 1000000., duration / 1000000.); fflush(stdout);
          double a = strap_alpha(current_position, 4 * 1000 * 1000 + video_start_pos * 1000);
          long long end_pos = video_end_pos >= 0 ? video_end_pos * 1000 : duration;
          double b = strap_alpha(current_position, end_pos - (4 + STRAP_DURATION_SEC) * 1000 * 1000);
          double max = a > b ? a : b;
          double c = strap_alpha(current_position, custom_show_strap_pos);
          if (c > max) max = c;
          dispmanx_alpha((int)(max * 200));
        }
        if (video_end_pos != -1 && current_position >= video_end_pos * 1000 && !video_end_was_set) {
          dbus_quit();
          state = PLAYER_STOPPING;
        }
        break;
    }
    if (state == PLAYER_STOPPED || state == PLAYER_RUNNING) channel_select_tick();
    process_input();
    usleep(1000000 / 60);
  }
}
