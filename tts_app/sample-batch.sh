#!/bin/sh

VOICE_DIR='/usr/local/share/hts_voice/mei_normal'
DIC_DIR='/usr/local/share/open_jtalk/open_jtalk_dic_utf_8-1.05'

TTS_TEXT="おはようございます。"

CMD="tts_app"
CMD=$CMD" -x  $DIC_DIR"
CMD=$CMD" -td $VOICE_DIR/tree-dur.inf"
CMD=$CMD" -td $VOICE_DIR/tree-dur.inf"
CMD=$CMD" -tm $VOICE_DIR/tree-mgc.inf"
CMD=$CMD" -tf $VOICE_DIR/tree-lf0.inf"
CMD=$CMD" -tl $VOICE_DIR/tree-lpf.inf"
CMD=$CMD" -md $VOICE_DIR/dur.pdf"
CMD=$CMD" -mm $VOICE_DIR/mgc.pdf"
CMD=$CMD" -mf $VOICE_DIR/lf0.pdf"
CMD=$CMD" -ml $VOICE_DIR/lpf.pdf"
CMD=$CMD" -dm $VOICE_DIR/mgc.win1"
CMD=$CMD" -dm $VOICE_DIR/mgc.win2"
CMD=$CMD" -dm $VOICE_DIR/mgc.win3"
CMD=$CMD" -df $VOICE_DIR/lf0.win1"
CMD=$CMD" -df $VOICE_DIR/lf0.win2"
CMD=$CMD" -df $VOICE_DIR/lf0.win3"
CMD=$CMD" -dl $VOICE_DIR/lpf.win1"
CMD=$CMD" -s  48000"
CMD=$CMD" -p  240"
CMD=$CMD" -a  0.58"
CMD=$CMD" -u  0.0"
CMD=$CMD" -em $VOICE_DIR/tree-gv-mgc.inf"
CMD=$CMD" -ef $VOICE_DIR/tree-gv-lf0.inf"
CMD=$CMD" -cm $VOICE_DIR/gv-mgc.pdf"
CMD=$CMD" -cf $VOICE_DIR/gv-lf0.pdf"
CMD=$CMD" -jm 0.7"
CMD=$CMD" -jf 0.5"
CMD=$CMD" -k  $VOICE_DIR/gv-switch.inf"
CMD=$CMD" -z  6000"

echo $TTS_TEXT | $CMD
