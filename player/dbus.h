#include <stdint.h>
#include <dbus/dbus.h>

void dbus_init(void);
int64_t query(char* param);
int64_t dbus_action(char *action_name);
int dbus_quit();
int64_t dbus_seek(int64_t seek);
