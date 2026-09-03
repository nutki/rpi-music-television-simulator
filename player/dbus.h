#include <stdint.h>

#ifdef USE_LIBVLC
#include <vlc/vlc.h>
#else
#include <dbus/dbus.h>
#endif

void dbus_init(void);
int64_t query(char* param);
int64_t dbus_action(char *action_name);
int dbus_quit(void);
int64_t dbus_seek(int64_t seek);
int64_t dbus_volume(int64_t vol);
int64_t dbus_crop(int x, int y, int w, int h);
int64_t dbus_aspect_mode(const char *s);

#ifdef USE_LIBVLC
int libvlc_open_file(const char *path, int64_t start_ms);
int libvlc_player_has_ended(void);
#endif
