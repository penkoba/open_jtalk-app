#!/bin/sh

VOICE_FILE='/usr/local/share/hts_voice/nitech_jp_atr503_m001-1.05/nitech_jp_atr503_m001.htsvoice'
DIC_DIR='/usr/local/share/open_jtalk/open_jtalk_dic_utf_8-1.06'

TTS_TEXT="おはようございます。"

CMD="tts_app"
CMD=$CMD" -x  $DIC_DIR"
CMD=$CMD" -m  $VOICE_FILE"

echo $TTS_TEXT | $CMD
