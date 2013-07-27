/*
 *  Copyright (c) Toshihiro Kobayashi <kobacha@mwa.biglobe.ne.jp>
 *
 *  this is based on open_jtalk.c from open_jtalk-1.05
 *  http://open-jtalk.sourceforge.net/
 *  Copyright (c) 2008-2012  Nagoya Institute of Technology
 *                           Department of Computer Science
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 * - Neither the name of the HTS working group nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

/* Main headers */
#include "mecab.h"
#include "njd.h"
#include "jpcommon.h"
#include "HTS_engine.h"

/* Sub headers */
#include "text2mecab.h"
#include "mecab2njd.h"
#include "njd_set_pronunciation.h"
#include "njd_set_digit.h"
#include "njd_set_accent_phrase.h"
#include "njd_set_accent_type.h"
#include "njd_set_unvoiced_vowel.h"
#include "njd_set_long_vowel.h"
#include "njd2jpcommon.h"

#include "play.h"
#include "debug.h"

#define MAXBUFLEN 1024

struct app {
	char *txtfn;
	FILE *logfp;

	/* directory name of dictionary */
	char *dn_mecab;

	/* HTS voice file name */
	char *fn_voice;

	/* global parameter */
	int sampling_rate;
	int fperiod;
	double alpha;
	double beta;
	double half_tone;
	int audio_buff_size;
	double uv_threshold;
	double gv_weight_mgc;
	double gv_weight_lf0;
#ifdef HTS_MELP
	double gv_weight_lpf;
#endif	/* HTS_MELP */

	double speed;

	Mecab mecab;
	NJD njd;
	JPCommon jpcommon;
	HTS_Engine engine;
	short *pcm;

	play_handle_t play_h;
	play_info_t play_info;
};

static int setup(struct app *app)
{
#ifdef HTS_MELP
#define NR_STREAMS	3
#else
#define NR_STREAMS	2
#endif	/* HTS_MELP */
	double gv_weight[] = {
		app->gv_weight_mgc,
		app->gv_weight_lf0,
#ifdef HTS_MELP
		app->gv_weight_lpf
#endif	/* HTS_MELP */
	};
	int i;

	app->play_h = play_init(&app->play_info, "default",
				SND_PCM_FORMAT_S16_LE, 1, app->sampling_rate,
				500000, 8);

	Mecab_initialize(&app->mecab);
	if (Mecab_load(&app->mecab, app->dn_mecab) != TRUE)
		return -1;

	NJD_initialize(&app->njd);

	JPCommon_initialize(&app->jpcommon);

	HTS_Engine_initialize(&app->engine);
	if (HTS_Engine_load(&app->engine, &app->fn_voice, 1) != TRUE)
		return -1;
	HTS_Engine_set_sampling_frequency(&app->engine,
					  (size_t)app->sampling_rate);
	if (app->fperiod >= 0)
		HTS_Engine_set_fperiod(&app->engine, app->fperiod);
	if (app->alpha >= 0.0)
		HTS_Engine_set_alpha(&app->engine, app->alpha);
	if (app->beta >= 0.0)
		HTS_Engine_set_beta(&app->engine, app->beta);
	if (app->half_tone >= 0.0)
		HTS_Engine_add_half_tone(&app->engine, app->half_tone);
	if (app->audio_buff_size > 0)
		HTS_Engine_set_audio_buff_size(&app->engine,
					       app->audio_buff_size);
	if (app->uv_threshold >= 0.0)
		HTS_Engine_set_msd_threshold(&app->engine, 1,
					     app->uv_threshold);
	if (app->speed >= 0.0)
		HTS_Engine_set_speed(&app->engine, app->speed);
	for (i = 0; i < NR_STREAMS; i++)
		if (gv_weight[i] >= 0.0)
			HTS_Engine_set_gv_weight(&app->engine, i, gv_weight[i]);

	return 0;
}

static int synthesize(struct app *app, char *txt)
{
	char buff[MAXBUFLEN];
	int label_size;
	int r = -1;

	text2mecab(buff, txt);
	Mecab_analysis(&app->mecab, buff);
	mecab2njd(&app->njd, Mecab_get_feature(&app->mecab),
		  Mecab_get_size(&app->mecab));
	njd_set_pronunciation(&app->njd);
	njd_set_digit(&app->njd);
	njd_set_accent_phrase(&app->njd);
	njd_set_accent_type(&app->njd);
	njd_set_unvoiced_vowel(&app->njd);
	njd_set_long_vowel(&app->njd);
	njd2jpcommon(&app->jpcommon, &app->njd);
	JPCommon_make_label(&app->jpcommon);
	label_size = JPCommon_get_label_size(&app->jpcommon);
	if (label_size > 2) {
		if (HTS_Engine_synthesize_from_strings(
				&app->engine,
				JPCommon_get_label_feature(&app->jpcommon),
				label_size) == TRUE) {
			unsigned int pcm_len;
			r = 0;	/* success */
			pcm_len = HTS_Engine_get_generated_speech_size(
					&app->engine);
			app->pcm = malloc(pcm_len * sizeof(short));
			HTS_Engine_get_generated_speech(&app->engine, app->pcm);
			play_write(app->play_h, app->pcm,
				   pcm_len * sizeof(short));
		}

		if (app->logfp) {
			fprintf(app->logfp, "[Text analysis result]\n");
			NJD_fprint(&app->njd, app->logfp);
			fprintf(app->logfp, "\n[Output label]\n");
			HTS_Engine_save_label(&app->engine, app->logfp);
			fprintf(app->logfp, "\n");
			HTS_Engine_save_information(&app->engine, app->logfp);
		}
		HTS_Engine_refresh(&app->engine);
	}
	JPCommon_refresh(&app->jpcommon);
	NJD_refresh(&app->njd);
	Mecab_refresh(&app->mecab);

	return r;
}

static void cleanup(struct app *app)
{
	Mecab_clear(&app->mecab);
	NJD_clear(&app->njd);
	JPCommon_clear(&app->jpcommon);
	HTS_Engine_clear(&app->engine);
	play_drain(app->play_h);
	play_exit(app->play_h);
	free(app->pcm);
}

static void usage(void)
{
	fprintf(stderr,
		"The Japanese TTS System \"Open JTalk\"\n"
		"Version 1.06 (http://open-jtalk.sourceforge.net/)\n"
		"Copyright (C) 2008-2012  Nagoya Institute of Technology\n"
		"All rights reserved.\n");
	fprintf(stderr, "%s", HTS_COPYRIGHT);
	fprintf(stderr,
		"\n"
		"Yet Another Part-of-Speech and Morphological Analyzer \"Mecab\"\n"
		"Version 0.994 (http://mecab.sourceforge.net/)\n"
		"Copyright (C) 2001-2008 Taku Kudo\n"
		"              2004-2008 Nippon Telegraph and Telephone Corporation\n"
		"All rights reserved.\n"
		"\n"
		"NAIST Japanese Dictionary\n"
		"Version 0.6.1-20090630 (http://naist-jdic.sourceforge.jp/)\n"
		"Copyright (C) 2009 Nara Institute of Science and Technology\n"
		"All rights reserved.\n"
		"\n"
		"open_jtalk - The Japanese TTS system \"Open JTalk\"\n"
		"\n"
		"  usage:\n"
		"       open_jtalk [ options ] [ infile ] \n"
		"  options:                                                                   [  def][ min-- max]\n"
		"    -x  dir         : dictionary directory                                    [  N/A]\n"
		"    -m  htsvoice   : HTS voice files                                         [  N/A]\n"
		"    -ot s          : filename of output trace information                    [  N/A]\n"
		"    -s  i          : sampling frequency                                      [48000][   1--48000]\n"
		"    -p  i          : frame period (point)                                    [ auto][   1--    ]\n"
		"    -a  f          : all-pass constant                                       [ auto][ 0.0-- 1.0]\n"
		"    -b  f          : postfiltering coefficient                               [  0.0][ 0.0-- 1.0]\n"
		"    -r  f          : speech speed rate                                       [  1.0][ 0.0--    ]\n"
		"    -fm f          : additional half-tone                                    [  0.0][    --    ]\n"
		"    -u  f          : voiced/unvoiced threshold                               [  0.5][ 0.0-- 1.0]\n"
		"    -jm f          : weight of GV for spectrum                               [  1.0][ 0.0--    ]\n"
		"    -jf f          : weight of GV for log F0                                 [  1.0][ 0.0--    ]\n"
#ifdef HTS_MELP
		"    -jl f          : weight of GV for low-pass filter                        [  1.0][ 0.0--    ]\n"
#endif	/* HTS_MELP */
		"    -z  i          : audio buffer size (if 0, turn off)                      [    0][   0--    ]\n"
		"  infile:\n"
		"    text file                                                                [stdin]\n"
		"\n");

	exit(0);
}

/* wrapper for fopen */
static FILE *get_fp(const char *name, const char *mode)
{
	FILE *fp = fopen(name, mode);

	if (fp == NULL) {
		app_error("Cannot open %s.\n", name);
		exit(1);
	}

	return (fp);
}

static int find_operand(char **argv, char **endv, const char *opt)
{
	if (strcmp(*argv, opt))
		return 0;
	if (argv + 1 == endv) {
		app_error("operand for %s is missing.\n", opt);
		exit(1);
	}
	return 1;
}

static int parse_arg(struct app *app, int argc, char **argv)
{
	char **endv = &argv[argc];

	/* read command */
	for (argv++; argv < endv; argv++) {
		if (find_operand(argv, endv, "-x")) {
			app->dn_mecab = *++argv;
		} else if (find_operand(argv, endv, "-m")) {
			app->fn_voice = *++argv;
		} else if (find_operand(argv, endv, "-ot")) {
			app->logfp = get_fp(*++argv, "w");
		} else if (!strcmp(*argv, "-h")) {
			usage();
		} else if (find_operand(argv, endv, "-s")) {
			app->sampling_rate = atoi(*++argv);
		} else if (find_operand(argv, endv, "-p")) {
			app->fperiod = atoi(*++argv);
		} else if (find_operand(argv, endv, "-a")) {
			app->alpha = atof(*++argv);
		} else if (find_operand(argv, endv, "-b")) {
			app->beta = atof(*++argv);
		} else if (find_operand(argv, endv, "-r")) {
			app->speed = atof(*++argv);
		} else if (find_operand(argv, endv, "-fm")) {
			app->half_tone = atof(*++argv);
		} else if (find_operand(argv, endv, "-u")) {
			app->uv_threshold = atof(*++argv);
		} else if (find_operand(argv, endv, "-jm")) {
			app->gv_weight_mgc = atof(*++argv);
		} else if (find_operand(argv, endv, "-jf")) {
			app->gv_weight_lf0 = atof(*++argv);
#ifdef HTS_MELP
		} else if (find_operand(argv, endv, "-jl")) {
			app->gv_weight_lpf = atof(*++argv);
#endif	/* HTS_MELP */
		} else if (find_operand(argv, endv, "-z")) {
			app->audio_buff_size = atoi(*++argv);
		} else if ((*argv)[0] == '-') {
			app_error("Invalid option %s.\n", *argv);
			exit(1);
		} else {
			app->txtfn = *argv;
		}
	}

	/* sanity check */
	if (app->fn_voice == NULL) {
		app_error("HTS void is not specified.\n");
		exit(1);
	} else if (app->dn_mecab == NULL) {
		app_error("dictionary directory is not specified.\n");
		exit(1);
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct app app;
	FILE *txtfp;
	char buff[MAXBUFLEN];
	int ret = 0;

	if (argc == 1)
		usage();

	/* init */
	memset(&app, 0, sizeof(app));

	app.sampling_rate = 48000;
	app.fperiod = -1;
	app.alpha = -1.0;
	app.beta = -1.0;
	app.half_tone = -1.0;
	app.audio_buff_size = 0;
	app.uv_threshold = -1.0;
	app.gv_weight_mgc = -1.0;
	app.gv_weight_lf0 = -1.0;
#ifdef HTS_MELP
	app.gv_weight_lpf = -1.0;
#endif	/* HTS_MELP */
	app.speed = -1.0;

	parse_arg(&app, argc, argv);

	txtfp = (app.txtfn != NULL) ? get_fp(app.txtfn, "rt") : stdin;

	/* initialize and load */
	if (setup(&app) < 0)
		goto out;

	/* synthesis */
	fgets(buff, MAXBUFLEN - 1, txtfp);
	if (synthesize(&app, buff) < 0) {
		fprintf(stderr, "failed to synthesize.\n");
		ret = 1;
	}

out:
	/* cleanup */
	cleanup(&app);

	/* free */
	if (app.txtfn != NULL)
		fclose(txtfp);
	if (app.logfp != NULL)
		fclose(app.logfp);

	return ret;
}
