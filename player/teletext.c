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
