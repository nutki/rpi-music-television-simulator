#include <sys/inotify.h>
#include <sys/types.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define EVENTS (IN_CLOSE_WRITE|IN_MOVED_FROM|IN_MOVED_TO|IN_DELETE)

static void print_event_json(struct inotify_event *i) {
  printf("{ \"type\": \"");
  if (i->mask & (IN_CLOSE_WRITE|IN_MOVED_TO)) putchar('>');
  if (i->mask & (IN_DELETE|IN_MOVED_FROM)) putchar('<');
  printf("\", \"name\": \"");
  for (char j = 0; i->name[j]; j++) {
    char c = i->name[j];
    if (c == '\n') putchar('\\'), putchar('n');
    else if (c == '\b') putchar('\\'), putchar('b');
    else if (c == '\t') putchar('\\'), putchar('t');
    else if (c == '\r') putchar('\\'), putchar('r');
    else if (c == '\f') putchar('\\'), putchar('f');
    else if (c == '\\' || c == '"') putchar('\\'), putchar(c);
    else if (c < 32) printf("\\u%04x",c);
    else putchar(c);
  }
  printf("\" }\n");
  fflush(stdout);
}

void errExit(char *str) {
  perror(str);
  exit(-1);
}

int main(int argc, char **argv) {
  int fd;
  char buf[10 * (sizeof(struct inotify_event) + NAME_MAX + 1)] __attribute__ ((aligned(8)));

  if (argc < 2) {
    fprintf(stderr, "Usage: %s path\n", argv[0]);
    exit(-1);
  }
  if ((fd = inotify_init()) < 0) errExit("inotify_init");
  if (inotify_add_watch(fd, argv[1], EVENTS) < 0) errExit("inotify_add_watch");

  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0) errExit("read");
    for (char *p = buf; p - buf < n; ) {
      struct inotify_event *event = (struct inotify_event *) p;
      print_event_json(event);
      p += sizeof(struct inotify_event) + event->len;
    }
  }
  return 0;
}
