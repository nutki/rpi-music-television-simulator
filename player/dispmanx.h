
void load_strap(char *path);
void dispmanx_init();
void dispmanx_alpha(int a);
void blank_background();
void dispmanx_close();
void dispmanx_display_argb(const uint8_t *argb, unsigned width, unsigned height);
void hdmi_set_geometry(int sw, int sh, int sx, int sy, int dw, int dh, int dx, int dy);
int8_t *hdmi_get_frame(int w, int h, int planes);
void hdmi_commit_frame(void);
#define HDMI_WIDTH 1920
#define HDMI_HEIGHT 1080
void osd_text(const char *c, int align);
void osd_text_clear();
#define BG_MODE_BLACK 0
#define BG_MODE_BLUE 1
#define BG_MODE_NOISE 2
void bg_mode(int mode);
char *dispmanx_shifted_window(void);
