#if defined(USE_LIBVLC)

#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include "preview_shm.h"

static int open_tt_socket() {
    signal(SIGPIPE, SIG_IGN);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return -1;
    }
    struct sockaddr_un  addr = { .sun_family = AF_UNIX, .sun_path = "/tmp/.mpv.tt.sock" };
    if (connect(fd, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
        // perror("connect tt socket");
        close(fd);
        return -1;
    }
    // Set non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        close(fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        close(fd);
        return -1;
    }
    printf("Opened TTDMP = %d\n", fd);
    return fd;
}

static int teletext_fd = -1;
static void reconnect() {
    if (teletext_fd == -1) {
        teletext_fd = open_tt_socket();
    }
}
int teletext_init(void) {
    reconnect();
    return 0;
}

void teletext_close(void) {
}

void teletext_set_video_filename(char *fname) {
    preview_shm_publish_name(fname);
    if (teletext_fd < 0) return;
    write(teletext_fd, "F", 1);
    write(teletext_fd, fname, strlen(fname));
    write(teletext_fd, "\n", 1);
}

void teletext_set_video_position(int pos) {
    preview_shm_publish_position(pos);
    if (teletext_fd < 0) return;
    char buf[20];
    sprintf(buf, "P%d\n", pos);
    write(teletext_fd, buf, strlen(buf));
}

void teletext_set_video_duration(int d) {
    preview_shm_publish_duration(d);
    if (teletext_fd < 0) return;
    char buf[20];
    sprintf(buf, "D%d\n", d);
    write(teletext_fd, buf, strlen(buf));
}

void teletext_get_packet(unsigned char buf[42]) {
    uint8_t packet[42];
    int res = read(teletext_fd, buf, 42);
}
void teletext_request_packets(int count) {
    reconnect();
    if (teletext_fd < 0) return;
    char buf[20];
    sprintf(buf, "T%d\n", count);
    int r = write(teletext_fd, buf, strlen(buf));
    if (r < 0) teletext_fd = -1;
}

#else

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int init = 0;
FILE *f;
static void set_screen_offset(int offset) {
    char soffset[32];
    sprintf(soffset, "%d", offset);
    int pid = fork();
    if (!pid) {
        execl("player/sdtv_ctl", "sdtv_ctl", "-pos", soffset, 0);
        printf("cannot run sdtv_ctl");
        exit(0);
    }
}
#define LINES_PER_FIELD 16
int teletext_init() {
    init = 1;
    set_screen_offset(LINES_PER_FIELD);
    f = popen("node teletext/teletext.js", "w");
    return LINES_PER_FIELD * 2;
}
void teletext_close() {
    if (!init) return;
//    if (f) fclose(f);
    set_screen_offset(0);
}
void teletext_set_video_filename(char *fname) {
    if (!f) return;
    fputc('F', f);
    fwrite(fname, strlen(fname), 1, f);
    fputc('\n', f);
    fflush(f);
}
void teletext_set_video_position(int pos) {
    if (!f) return;
    char buf[20];
    sprintf(buf, "P%d\n", pos);
    fwrite(buf, strlen(buf), 1, f);
    fflush(f);
}
void teletext_set_video_duration(int d) {
    if (!f) return;
    char buf[20];
    sprintf(buf, "D%d\n", d);
    fwrite(buf, strlen(buf), 1, f);
    fflush(f);
}

#endif
