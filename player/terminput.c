#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include "terminput.h"
struct termios old, new;
int fd = 0;
int oldflags = 0;
void term_setup() {
	if (tcgetattr(fd, &old) == -1) perror("tcgetattr");
  new = old;
//	new.c_lflag &= ~((tcflag_t)(ICANON | ECHO | ISIG));
	new.c_lflag &= ~((tcflag_t)(ICANON | ECHO));
//  printf("TERMIOS %d %d %04x\n", new.c_cc[VMIN], new.c_cc[VTIME], new.c_iflag);
//	new.c_iflag     = 0;
	new.c_cc[VMIN]  = 0;
	new.c_cc[VTIME] = 0; /* 0.1 sec intercharacter timeout */
	if (tcsetattr(fd, TCSAFLUSH, &new) == -1) perror("tcsetattr");
  oldflags = fcntl(fd, F_GETFL, 0);
//  fcntl(0, F_SETFL, oldflags | O_NONBLOCK);
}
void term_cleanup() {
  if (tcsetattr(fd, 0, &old) == -1) perror("tcsetattr");
  if (fcntl(fd, F_SETFL, oldflags)) perror("fcntl");
}
int term_getkey() {
  char c = 0;
  read(fd, &c, 1);
  return c;
}
