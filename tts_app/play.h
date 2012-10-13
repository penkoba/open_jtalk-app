#ifndef _PLAY_H
#define _PLAY_H

#include <alsa/asoundlib.h>

typedef struct play_ctl *play_handle_t;

typedef struct play_info {
	size_t chunk_bytes;
	int buf_cnt;
	snd_pcm_format_t format;
	unsigned int channels;
	unsigned int rate;
} play_info_t;

extern play_handle_t
play_init(play_info_t *play_info, const char *pcm_name,
	  snd_pcm_format_t format, unsigned int channels, unsigned int rate,
	  snd_pcm_uframes_t buf_time_us, int buf_cnt_min);
extern void play_exit(play_handle_t play_h);
extern ssize_t play_write(play_handle_t play_h, void *data, size_t size);
extern int play_start(play_handle_t play_h);
extern void play_drain(play_handle_t play_h);

#endif	/* _PLAY_H */
