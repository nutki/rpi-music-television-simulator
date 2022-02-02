#define VCR_FONT_H 17
#define VCR_FONT_W 12
int render_text(const char *s, void (*put_pixel)(int x, int y, int v), int max_width);

