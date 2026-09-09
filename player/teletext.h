int teletext_init();
void teletext_close();
void teletext_set_video_filename(char *f);
void teletext_set_video_position(int pos);
void teletext_set_video_duration(int d);
void teletext_get_packet(unsigned char buf[42]);
void teletext_request_packets(int count);
