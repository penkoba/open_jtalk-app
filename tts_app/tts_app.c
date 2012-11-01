/*
 *  Copyright (c) Toshihiro Kobayashi <kobacha@mwa.biglobe.ne.jp>
 *
 *  this is based on open_jtalk.c from open_jtalk-1.05
 *  http://open-jtalk.sourceforge.net/
 *  Copyright (c) 2008-2011  Nagoya Institute of Technology
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

	/* file names of models */
	char *fn_ms_dur;
	char *fn_ms_mgc;
	char *fn_ms_lf0;
	char *fn_ms_lpf;

	/* file names of trees */
	char *fn_ts_dur;
	char *fn_ts_mgc;
	char *fn_ts_lf0;
	char *fn_ts_lpf;

	/* file names of windows */
	char **fn_ws_mgc;
	char **fn_ws_lf0;
	char **fn_ws_lpf;
	int num_ws_mgc, num_ws_lf0, num_ws_lpf;

	/* file names of global variance */
	char *fn_ms_gvm;
	char *fn_ms_gvl;
	char *fn_ms_gvf;

	/* file names of global variance trees */
	char *fn_ts_gvm;
	char *fn_ts_gvl;
	char *fn_ts_gvf;

	/* file names of global variance switch */
	char *fn_gv_switch;

	/* global parameter */
	int sampling_rate;
	int fperiod;
	double alpha;
	int stage;		/* gamma = -1.0/stage */
	double beta;
	int audio_buff_size;
	double uv_threshold;
	double gv_weight_mgc;
	double gv_weight_lf0;
	double gv_weight_lpf;
	HTS_Boolean use_log_gain;

	Mecab mecab;
	NJD njd;
	JPCommon jpcommon;
	HTS_Engine engine;
	short *pcm;

	play_handle_t play_h;
	play_info_t play_info;
};

static void setup(struct app *app)
{
	int nr_streams = (app->fn_ms_lpf &&
			  app->fn_ts_lpf &&
			  app->num_ws_lpf > 0) ? 3 : 2;
	double gv_weight[3] = {
		app->gv_weight_mgc,
		app->gv_weight_lf0,
		app->gv_weight_lpf
	};
	char *gv_pdf_fn[3] = {
		app->fn_ms_gvm,
		app->fn_ms_gvl,
		app->fn_ms_gvf
	};
	char *gv_tree_fn[3] = {
		app->fn_ts_gvm,
		app->fn_ts_gvl,
		app->fn_ts_gvf
	};
	int i;

	app->play_h = play_init(&app->play_info, "default",
				SND_PCM_FORMAT_S16_LE, 1, app->sampling_rate,
				500000, 8);

	Mecab_initialize(&app->mecab);
	Mecab_load(&app->mecab, app->dn_mecab);

	NJD_initialize(&app->njd);

	JPCommon_initialize(&app->jpcommon);

	HTS_Engine_initialize(&app->engine, nr_streams);
	HTS_Engine_set_sampling_rate(&app->engine, app->sampling_rate);
	HTS_Engine_set_fperiod(&app->engine, app->fperiod);
	HTS_Engine_set_alpha(&app->engine, app->alpha);
	HTS_Engine_set_gamma(&app->engine, app->stage);
	HTS_Engine_set_log_gain(&app->engine, app->use_log_gain);
	HTS_Engine_set_beta(&app->engine, app->beta);
	HTS_Engine_set_audio_buff_size(&app->engine, app->audio_buff_size);
	HTS_Engine_set_msd_threshold(&app->engine, 1, app->uv_threshold);
	for (i = 0; i < nr_streams; i++)
		HTS_Engine_set_gv_weight(&app->engine, i, gv_weight[i]);

	HTS_Engine_load_duration_from_fn(&app->engine, &app->fn_ms_dur,
					 &app->fn_ts_dur, 1);
	HTS_Engine_load_parameter_from_fn(&app->engine, &app->fn_ms_mgc,
					  &app->fn_ts_mgc, app->fn_ws_mgc, 0,
					  FALSE, app->num_ws_mgc, 1);
	HTS_Engine_load_parameter_from_fn(&app->engine, &app->fn_ms_lf0,
					  &app->fn_ts_lf0, app->fn_ws_lf0, 1,
					  TRUE, app->num_ws_lf0, 1);
	if (nr_streams == 3)
		HTS_Engine_load_parameter_from_fn(&app->engine,
						  &app->fn_ms_lpf,
						  &app->fn_ts_lpf,
						  app->fn_ws_lpf, 2, FALSE,
						  app->num_ws_lpf, 1);
	for (i = 0; i < nr_streams; i++) {
		if (gv_pdf_fn[i])
			HTS_Engine_load_gv_from_fn(&app->engine, &gv_pdf_fn[i],
						   &gv_tree_fn[i], i, 1);
	}
	if (app->fn_gv_switch)
		HTS_Engine_load_gv_switch_from_fn(&app->engine,
						  app->fn_gv_switch);
}

static void synthesize(struct app *app, char *txt)
{
	char buff[MAXBUFLEN];

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
	if (JPCommon_get_label_size(&app->jpcommon) > 2) {
		unsigned int pcm_len;
		HTS_Engine_load_label_from_string_list(
				&app->engine,
				JPCommon_get_label_feature(&app->jpcommon),
				JPCommon_get_label_size(&app->jpcommon));
		HTS_Engine_create_sstream(&app->engine);
		HTS_Engine_create_pstream(&app->engine);
		HTS_Engine_create_gstream(&app->engine);
		pcm_len = HTS_Engine_get_generated_speech_size(&app->engine);
		app->pcm = malloc(pcm_len * sizeof(short));
		HTS_Engine_get_generated_speech(&app->engine, app->pcm);
		play_write(app->play_h, app->pcm, pcm_len * sizeof(short));

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
		"\n"
		"The Japanese TTS System \"Open JTalk\"\n"
		"Open JTalk version 1.05 (http://open-jtalk.sourceforge.net/)\n"
		"Copyright (C) 2008-2011  Nagoya Institute of Technology\n"
		"All rights reserved.\n");
	HTS_show_copyright(stderr);
	fprintf(stderr,
		"\n"
		"Yet Another Part-of-Speech and Morphological Analyzer (Mecab)\n"
		"mecab version 0.98 (http://mecab.sourceforge.net/)\n"
		"Copyright (C) 2001-2008  Taku Kudo\n"
		"              2004-2008  Nippon Telegraph and Telephone Corporation\n"
		"All rights reserved.\n"
		"\n"
		"NAIST Japanese Dictionary\n"
		"mecab-naist-jdic version 0.6.1-20090630 (http://naist-jdic.sourceforge.jp/)\n"
		"Copyright (C) 2009  Nara Institute of Science and Technology\n"
		"All rights reserved.\n"
		"\n"
		"open_jtalk - The Japanese TTS system \"Open JTalk\"\n"
		"\n"
		"  usage:\n"
		"       open_jtalk [ options ] [ infile ] \n"
		"  options:                                                                   [  def][ min--max]\n"
		"    -x dir         : dictionary directory                                    [  N/A]\n"
		"    -td tree       : decision trees file for state duration                  [  N/A]\n"
		"    -tm tree       : decision trees file for spectrum                        [  N/A]\n"
		"    -tf tree       : decision trees file for Log F0                          [  N/A]\n"
		"    -tl tree       : decision trees file for low-pass filter                 [  N/A]\n"
		"    -md pdf        : model file for state duration                           [  N/A]\n"
		"    -mm pdf        : model file for spectrum                                 [  N/A]\n"
		"    -mf pdf        : model file for Log F0                                   [  N/A]\n"
		"    -ml pdf        : model file for low-pass filter                          [  N/A]\n"
		"    -dm win        : window files for calculation delta of spectrum          [  N/A]\n"
		"    -df win        : window files for calculation delta of Log F0            [  N/A]\n"
		"    -dl win        : window files for calculation delta of low-pass filter   [  N/A]\n"
		"    -ot s          : filename of output trace information                    [  N/A]\n"
		"    -s  i          : sampling frequency                                      [16000][   1--48000]\n"
		"    -p  i          : frame period (point)                                    [   80][   1--]\n"
		"    -a  f          : all-pass constant                                       [ 0.42][ 0.0--1.0]\n"
		"    -g  i          : gamma = -1 / i (if i=0 then gamma=0)                    [    0][   0-- ]\n"
		"    -b  f          : postfiltering coefficient                               [  0.0][-0.8--0.8]\n"
		"    -l             : regard input as log gain and output linear one (LSP)    [  N/A]\n"
		"    -u  f          : voiced/unvoiced threshold                               [  0.5][ 0.0--1.0]\n"
		"    -em tree       : decision tree file for GV of spectrum                   [  N/A]\n"
		"    -ef tree       : decision tree file for GV of Log F0                     [  N/A]\n"
		"    -el tree       : decision tree file for GV of low-pass filter            [  N/A]\n"
		"    -cm pdf        : filename of GV for spectrum                             [  N/A]\n"
		"    -cf pdf        : filename of GV for Log F0                               [  N/A]\n"
		"    -cl pdf        : filename of GV for low-pass filter                      [  N/A]\n"
		"    -jm f          : weight of GV for spectrum                               [  1.0][ 0.0--2.0]\n"
		"    -jf f          : weight of GV for Log F0                                 [  1.0][ 0.0--2.0]\n"
		"    -jl f          : weight of GV for low-pass filter                        [  1.0][ 0.0--2.0]\n"
		"    -k  tree       : use GV switch                                           [  N/A]\n"
		"    -z  i          : audio buffer size                                       [ 1600][   0--48000]\n"
		"  infile:\n"
		"    text file                                                                [stdin]\n"
		"  note:\n"
		"    option '-d' may be repeated to use multiple delta parameters.\n"
		"    generated spectrum, log F0, and low-pass filter coefficient\n"
		"    sequences are saved in natural endian, binary (float) format.\n"
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
		} else if (find_operand(argv, endv, "-td")) {
			app->fn_ts_dur = *++argv;
		} else if (find_operand(argv, endv, "-tm")) {
			app->fn_ts_mgc = *++argv;
		} else if (find_operand(argv, endv, "-tf")) {
			app->fn_ts_lf0 = *++argv;
		} else if (find_operand(argv, endv, "-tl")) {
			app->fn_ts_lpf = *++argv;
		} else if (find_operand(argv, endv, "-md")) {
			app->fn_ms_dur = *++argv;
		} else if (find_operand(argv, endv, "-mm")) {
			app->fn_ms_mgc = *++argv;
		} else if (find_operand(argv, endv, "-mf")) {
			app->fn_ms_lf0 = *++argv;
		} else if (find_operand(argv, endv, "-ml")) {
			app->fn_ms_lpf = *++argv;
		} else if (find_operand(argv, endv, "-dm")) {
			app->fn_ws_mgc[app->num_ws_mgc++] = *++argv;
		} else if (find_operand(argv, endv, "-df")) {
			app->fn_ws_lf0[app->num_ws_lf0++] = *++argv;
		} else if (find_operand(argv, endv, "-dl")) {
			app->fn_ws_lpf[app->num_ws_lpf++] = *++argv;
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
		} else if (find_operand(argv, endv, "-g")) {
			app->stage = atoi(*++argv);
		} else if (find_operand(argv, endv, "-l")) {
			app->use_log_gain = TRUE;
		} else if (find_operand(argv, endv, "-b")) {
			app->beta = atof(*++argv);
		} else if (find_operand(argv, endv, "-u")) {
			app->uv_threshold = atof(*++argv);
		} else if (find_operand(argv, endv, "-em")) {
			app->fn_ts_gvm = *++argv;
		} else if (find_operand(argv, endv, "-ef")) {
			app->fn_ts_gvl = *++argv;
		} else if (find_operand(argv, endv, "-el")) {
			app->fn_ts_gvf = *++argv;
		} else if (find_operand(argv, endv, "-cm")) {
			app->fn_ms_gvm = *++argv;
		} else if (find_operand(argv, endv, "-cf")) {
			app->fn_ms_gvl = *++argv;
		} else if (find_operand(argv, endv, "-cl")) {
			app->fn_ms_gvf = *++argv;
		} else if (find_operand(argv, endv, "-jm")) {
			app->gv_weight_mgc = atof(*++argv);
		} else if (find_operand(argv, endv, "-jf")) {
			app->gv_weight_lf0 = atof(*++argv);
		} else if (find_operand(argv, endv, "-jl")) {
			app->gv_weight_lpf = atof(*++argv);
		} else if (find_operand(argv, endv, "-k")) {
			app->fn_gv_switch = *++argv;
		} else if (find_operand(argv, endv, "-z")) {
			app->audio_buff_size = atoi(*++argv);
		} else if ((*argv)[0] == '-') {
			app_error("Invalid option %s.\n", *argv);
			exit(1);
		} else {
			app->txtfn = *argv;
		}
	}

	/* dictionary directory check */
	if (app->dn_mecab == NULL) {
		app_error("dictionary directory is not specified.\n");
		exit(1);
	}
	/* number of models,trees check */
	if (app->fn_ms_dur == NULL || app->fn_ms_mgc == NULL ||
	    app->fn_ms_lf0 == NULL || app->fn_ts_dur == NULL ||
	    app->fn_ts_mgc == NULL || app->fn_ts_lf0 == NULL ||
	    app->num_ws_mgc <= 0 || app->num_ws_lf0 <= 0) {
		app_error("Specify models (trees) for each parameter.\n");
		exit(1);
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct app app;
	FILE *txtfp;
	char buff[MAXBUFLEN];

	if (argc == 1)
		usage();

	/* init */
	memset(&app, 0, sizeof(app));

	app.fn_ws_mgc = (char **)calloc(argc, sizeof(char *));
	app.fn_ws_lf0 = (char **)calloc(argc, sizeof(char *));
	app.fn_ws_lpf = (char **)calloc(argc, sizeof(char *));
	app.sampling_rate = 16000;
	app.fperiod = 80;
	app.alpha = 0.42;
	app.stage = 0;
	app.beta = 0.0;
	app.audio_buff_size = 1600;
	app.uv_threshold = 0.5;
	app.gv_weight_mgc = 1.0;
	app.gv_weight_lf0 = 1.0;
	app.gv_weight_lpf = 1.0;
	app.use_log_gain = FALSE;

	parse_arg(&app, argc, argv);

	txtfp = (app.txtfn != NULL) ? get_fp(app.txtfn, "rt") : stdin;

	/* initialize and load */
	setup(&app);

	/* synthesis */
	fgets(buff, MAXBUFLEN - 1, txtfp);
	synthesize(&app, buff);

	/* cleanup */
	cleanup(&app);

	/* free */
	free(app.fn_ws_mgc);
	free(app.fn_ws_lf0);
	free(app.fn_ws_lpf);
	if (app.txtfn != NULL)
		fclose(txtfp);
	if (app.logfp != NULL)
		fclose(app.logfp);

	return 0;
}
