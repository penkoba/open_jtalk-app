/*
 *  Copyright (c) by Toshihiro Kobayashi <kobacha@mwa.biglobe.ne.jp>
 *  Based on aplay.c in alsa-utils, by Jaroslav Kysela <perex@perex.cz>
 *  Based on vplay program by Michael Beck
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <alsa/asoundlib.h>
#include <time.h>
#include <sys/uio.h>
#include <sys/time.h>

#include "play.h"
#ifndef DEBUG_LEVEL_PLAY
#define DEBUG_LEVEL_PLAY	0
#endif
#define DEBUG_HEAD_PLAY		"[play] "

#include "debug.h"

typedef struct play_ctl {
	snd_pcm_t *pcm_h;
	snd_pcm_uframes_t chunk_size;
	snd_output_t *log;
	unsigned int bytes_per_frame;
	size_t chunk_bytes;
	int buf_cnt;
	struct {
		snd_pcm_format_t format;
		unsigned int channels;
		unsigned int rate;
	} hwparams;
} play_ctl_t;

static int sound_use_count;

static void sound_use(void)
{
	sound_use_count++;
}

static void sound_unuse(void)
{
	if (--sound_use_count == 0)
		snd_config_update_free_global();
}

static int set_params(play_ctl_t *play_ctl, unsigned int buf_time_req,
		      int period_cnt_min)
{
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_uframes_t buffer_size;
	snd_pcm_uframes_t start_threshold, stop_threshold;
	unsigned int period_time;
	unsigned int buffer_time;
	size_t bits_per_sample;
	int err;

	snd_pcm_hw_params_alloca(&params);
	snd_pcm_sw_params_alloca(&swparams);
	err = snd_pcm_hw_params_any(play_ctl->pcm_h, params);
	if (err < 0) {
		app_error("Broken configuration for this PCM: "
			  "no configurations available\n");
		return -1;
	}
	err = snd_pcm_hw_params_set_access(play_ctl->pcm_h, params,
					   SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		app_error("Access type not available\n");
		return -1;
	}
	err = snd_pcm_hw_params_set_format(play_ctl->pcm_h, params,
					   play_ctl->hwparams.format);
	if (err < 0) {
		app_error("Sample format not available\n");
		return -1;
	}
	err = snd_pcm_hw_params_set_channels(play_ctl->pcm_h, params,
					     play_ctl->hwparams.channels);
	if (err < 0) {
		app_error("Channels count (%d) not available\n",
			  play_ctl->hwparams.channels);
		return -1;
	}

#if 0
	err = snd_pcm_hw_params_set_periods_min(play_ctl->pcm_h, params, 2);
	if (err < 0) {
		app_error("snd_pcm_hw_params_set_periods_min() failed. (%s)\n",
			  snd_strerror(err));
		return -1;
	}
#endif
	err = snd_pcm_hw_params_set_rate_near(play_ctl->pcm_h, params,
					      &play_ctl->hwparams.rate, 0);
	if (err < 0) {
		app_error("snd_pcm_hw_params_set_rate_near() failed. (%s)\n",
			  snd_strerror(err));
		return -1;
	}

	err = snd_pcm_hw_params_get_buffer_time_max(params, &buffer_time, 0);
	if (err < 0) {
		app_error("snd_pcm_hw_params_get_buffer_time_max() failed."
			  " (%s)\n", snd_strerror(err));
		return -1;
	}
	if (buffer_time > buf_time_req)
		buffer_time = buf_time_req;
	period_time = buffer_time / period_cnt_min;
	err = snd_pcm_hw_params_set_period_time_near(play_ctl->pcm_h, params,
						     &period_time, 0);
	if (err < 0) {
		app_error("snd_pcm_hw_params_set_period_time_near() failed."
			  " (%s)\n", snd_strerror(err));
		return -1;
	}
	err = snd_pcm_hw_params_set_buffer_time_near(play_ctl->pcm_h, params,
						     &buffer_time, 0);
	if (err < 0) {
		app_error("snd_pcm_hw_params_set_buffer_time_near() failed."
			  " (%s)\n", snd_strerror(err));
		return -1;
	}
	err = snd_pcm_hw_params(play_ctl->pcm_h, params);
	if (err < 0) {
		app_error("Unable to install hw params:");
		snd_pcm_hw_params_dump(params, play_ctl->log);
		return -1;
	}
	snd_pcm_hw_params_get_period_size(params, &play_ctl->chunk_size, 0);
	snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	if (play_ctl->chunk_size == buffer_size) {
		app_error("Can't use period equal to buffer size (%lu == %lu)",
			  play_ctl->chunk_size, buffer_size);
		return -1;
	}
	snd_pcm_sw_params_current(play_ctl->pcm_h, swparams);
	err = snd_pcm_sw_params_set_avail_min(play_ctl->pcm_h, swparams,
					      play_ctl->chunk_size);

	/* round up to closest transfer boundary */
	start_threshold = buffer_size;
	err = snd_pcm_sw_params_set_start_threshold(play_ctl->pcm_h, swparams,
						    start_threshold);
	if (err < 0) {
		app_error("snd_pcm_sw_params_set_start_threshold() failed."
			  " (%s)\n", snd_strerror(err));
		return -1;
	}
	stop_threshold = buffer_size;
	err = snd_pcm_sw_params_set_stop_threshold(play_ctl->pcm_h, swparams,
						   stop_threshold);
	if (err < 0) {
		app_error("snd_pcm_sw_params_set_stop_threshold() failed."
			  " (%s)\n", snd_strerror(err));
		return -1;
	}

#ifndef DISABLE_ALSA_SW_PARAMS	/* some audio card doesn't work properly */
	if (snd_pcm_sw_params(play_ctl->pcm_h, swparams) < 0) {
		app_error("unable to install sw params:");
		snd_pcm_sw_params_dump(swparams, play_ctl->log);
		return -1;
	}
#endif

	bits_per_sample =
		snd_pcm_format_physical_width(play_ctl->hwparams.format);
	play_ctl->bytes_per_frame =
		bits_per_sample / 8 * play_ctl->hwparams.channels;
	play_ctl->chunk_bytes =
		play_ctl->chunk_size * play_ctl->bytes_per_frame;
	play_ctl->buf_cnt = buffer_size / play_ctl->chunk_size;

	return 0;
}

#ifndef timersub
#define	timersub(a, b, result) \
do { \
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
	if ((result)->tv_usec < 0) { \
		--(result)->tv_sec; \
		(result)->tv_usec += 1000000; \
	} \
} while (0)
#endif

/* I/O error handler */
static void xrun(play_ctl_t *play_ctl)
{
	snd_pcm_status_t *status;
	int res;
	
	snd_pcm_status_alloca(&status);
	if ((res = snd_pcm_status(play_ctl->pcm_h, status)) < 0) {
		app_error("status error: %s", snd_strerror(res));
		exit(EXIT_FAILURE);
	}
	if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
		struct timeval now, diff, tstamp;
		gettimeofday(&now, 0);
		snd_pcm_status_get_trigger_tstamp(status, &tstamp);
		timersub(&now, &tstamp, &diff);
		app_error("underrun!!! (at least %.3f ms long)\n",
			  diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
		if ((res = snd_pcm_prepare(play_ctl->pcm_h)) < 0) {
			app_error("xrun: prepare error: %s", snd_strerror(res));
			exit(EXIT_FAILURE);
		}
		return;		/* ok, data should be accepted again */
	} else if (snd_pcm_status_get_state(status) == SND_PCM_STATE_DRAINING) {
		app_error("capture stream format change? "
			  "attempting recover...\n");
		if ((res = snd_pcm_prepare(play_ctl->pcm_h))<0) {
			app_error("xrun(DRAINING): prepare error: %s",
				snd_strerror(res));
			exit(EXIT_FAILURE);
		}
		return;
	}
	app_error("read/write error, state = %s",
		  snd_pcm_state_name(snd_pcm_status_get_state(status)));
	exit(EXIT_FAILURE);
}

/* I/O suspend handler */
static void suspend(play_ctl_t *play_ctl)
{
	int res;

	fprintf(stderr, "Suspended. Trying resume. ");
	fflush(stderr);
	while ((res = snd_pcm_resume(play_ctl->pcm_h)) == -EAGAIN)
		sleep(1);	/* wait until suspend flag is released */
	if (res < 0) {
		fprintf(stderr, "Failed. Restarting stream. ");
		fflush(stderr);
		if ((res = snd_pcm_prepare(play_ctl->pcm_h)) < 0) {
			app_error("suspend: prepare error: %s",
				  snd_strerror(res));
			exit(EXIT_FAILURE);
		}
	}
	fprintf(stderr, "Done.\n");
}

static ssize_t
pcm_write(play_ctl_t *play_ctl, unsigned char *data, size_t wcount)
{
	ssize_t r;
	ssize_t result = 0;

	while (wcount > 0) {
		r = snd_pcm_writei(play_ctl->pcm_h, data, wcount);
		if (r == -EAGAIN || (r >= 0 && (size_t)r < wcount))
			snd_pcm_wait(play_ctl->pcm_h, 1000);
		else if (r == -EPIPE)
			xrun(play_ctl);
		else if (r == -ESTRPIPE)
			suspend(play_ctl);
		else if (r < 0)
			return r;
		if (r > 0) {
			result += r;
			wcount -= r;
			data += r * play_ctl->bytes_per_frame;
		}
	}
	return result;
}

play_handle_t
play_init(play_info_t *play_info, const char *pcm_name,
	  snd_pcm_format_t format, unsigned int channels, unsigned int rate,
	  snd_pcm_uframes_t buf_time_us, int buf_cnt_min)
{
	int err;
	snd_pcm_info_t *pcm_info;
	play_ctl_t *play_ctl;

	app_debug(PLAY, 3, "%s() in\n", __func__);
	play_ctl = malloc(sizeof(play_ctl_t));

	snd_pcm_info_alloca(&pcm_info);

	err = snd_output_stdio_attach(&play_ctl->log, stderr, 0);
	if (err < 0) {
		app_error("snd_output_stdio_attach() failed. (%s)\n",
			  snd_strerror(err));
		return NULL;
	}

	play_ctl->hwparams.format = format;
	play_ctl->hwparams.rate = rate;
	play_ctl->hwparams.channels = channels;

	err = snd_pcm_open(&play_ctl->pcm_h, pcm_name,
			   SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		app_error("audio open error: %s", snd_strerror(err));
		return NULL;
	}

	if ((err = snd_pcm_info(play_ctl->pcm_h, pcm_info)) < 0) {
		app_error("info error: %s", snd_strerror(err));
		return NULL;
	}

	/* setup sound hardware */
	if (set_params(play_ctl, buf_time_us, buf_cnt_min) < 0) {
		snd_pcm_close(play_ctl->pcm_h);
		return NULL;
	}
	app_debug(PLAY, 1,
		  "chunk_size = %ld, chunk_bytes = %zd, buf_cnt = %d\n",
		  play_ctl->chunk_size, play_ctl->chunk_bytes,
		  play_ctl->buf_cnt);

	play_info->chunk_bytes = play_ctl->chunk_bytes;
	play_info->buf_cnt = play_ctl->buf_cnt;
	play_info->format = play_ctl->hwparams.format;
	play_info->rate = play_ctl->hwparams.rate;
	play_info->channels = play_ctl->hwparams.channels;

	sound_use();

	app_debug(PLAY, 3, "%s() out\n", __func__);
	return play_ctl;
}

void play_exit(play_handle_t play_h)
{
	play_ctl_t *play_ctl = play_h;

	app_debug(PLAY, 3, "%s() in\n", __func__);
	snd_pcm_close(play_ctl->pcm_h);
	snd_output_close(play_ctl->log);
	sound_unuse();
	free(play_ctl);
	app_debug(PLAY, 3, "%s() out\n", __func__);
}

ssize_t play_write(play_handle_t play_h, void *data, size_t bytes)
{
	play_ctl_t *play_ctl = play_h;
	size_t size = bytes / play_ctl->bytes_per_frame;
	ssize_t wsize;

	app_debug(PLAY, 3, "%s() in\n", __func__);

	app_debug(PLAY, 2, "play buffer: data = %p, size = %zd\n", data, size);
	wsize = pcm_write(play_ctl, data, size);
	if (wsize < 0) {
		app_error("write error: %s\n", snd_strerror(wsize));
		return -1;
	} else if (wsize < (ssize_t)size) {
		app_debug(PLAY, 2,
			  "pcm_write() did not write all of data:\n"
			  "  written = %zd\n", wsize);
		return -1;
	}

	app_debug(PLAY, 3, "%s() out\n", __func__);
	return wsize * play_ctl->bytes_per_frame;
}

int play_start(play_handle_t play_h)
{
	play_ctl_t *play_ctl = play_h;
	int res;

	app_debug(PLAY, 3, "%s() in\n", __func__);

	if ((res = snd_pcm_reset(play_ctl->pcm_h)) < 0) {
		app_error("snd_pcm_reset error: %s", snd_strerror(res));
		return -1;
	}
	if ((res = snd_pcm_prepare(play_ctl->pcm_h)) < 0) {
		app_error("snd_pcm_prepare error: %s", snd_strerror(res));
		return -1;
	}

	app_debug(PLAY, 3, "%s() out\n", __func__);
	return 0;
}

void play_drain(play_handle_t play_h)
{
	play_ctl_t *play_ctl = play_h;

	app_debug(PLAY, 3, "%s() in\n", __func__);
	snd_pcm_drain(play_ctl->pcm_h);
	app_debug(PLAY, 3, "%s() out\n", __func__);
}
