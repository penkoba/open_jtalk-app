# adjust this variable to your environment
OJT_BUILD_DIR :=	$(HOME)/build/open_jtalk-1.06

CC :=	gcc
CFLAGS := \
	-O2 -Wall -Wextra \
	-I $(OJT_BUILD_DIR)/text2mecab \
	-I $(OJT_BUILD_DIR)/mecab/src \
	-I $(OJT_BUILD_DIR)/mecab2njd \
	-I $(OJT_BUILD_DIR)/njd \
	-I $(OJT_BUILD_DIR)/njd_set_pronunciation \
	-I $(OJT_BUILD_DIR)/njd_set_digit \
	-I $(OJT_BUILD_DIR)/njd_set_accent_phrase \
	-I $(OJT_BUILD_DIR)/njd_set_accent_type \
	-I $(OJT_BUILD_DIR)/njd_set_unvoiced_vowel \
	-I $(OJT_BUILD_DIR)/njd_set_long_vowel \
	-I $(OJT_BUILD_DIR)/njd2jpcommon \
	-I $(OJT_BUILD_DIR)/jpcommon \
	-I /usr/local/include

OBJS := tts_app.o play.o
LDLIBS := \
	$(OJT_BUILD_DIR)/text2mecab/libtext2mecab.a \
	$(OJT_BUILD_DIR)/mecab/src/libmecab.a \
	$(OJT_BUILD_DIR)/mecab2njd/libmecab2njd.a \
	$(OJT_BUILD_DIR)/njd/libnjd.a \
	$(OJT_BUILD_DIR)/njd_set_pronunciation/libnjd_set_pronunciation.a \
	$(OJT_BUILD_DIR)/njd_set_digit/libnjd_set_digit.a \
	$(OJT_BUILD_DIR)/njd_set_accent_phrase/libnjd_set_accent_phrase.a \
	$(OJT_BUILD_DIR)/njd_set_accent_type/libnjd_set_accent_type.a \
	$(OJT_BUILD_DIR)/njd_set_unvoiced_vowel/libnjd_set_unvoiced_vowel.a \
	$(OJT_BUILD_DIR)/njd_set_long_vowel/libnjd_set_long_vowel.a \
	$(OJT_BUILD_DIR)/njd2jpcommon/libnjd2jpcommon.a \
	$(OJT_BUILD_DIR)/jpcommon/libjpcommon.a \
	-lHTSEngine -lstdc++ -lasound -lm

all: tts_app

clean:
	-rm *.o tts_app

tts_app: $(OBJS)
