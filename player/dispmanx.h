
void load_strap(char *path);
void dispmanx_init();
void dispmanx_alpha(int a);
void blank_background();
void dispmanx_close();
void osd_text(const char *c, int align);
void osd_text_clear();
#define BG_MODE_BLACK 0
#define BG_MODE_BLUE 1
#define BG_MODE_NOISE 2
void bg_mode(int mode);
char *dispmanx_shifted_window(void);
