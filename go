#!/bin/sh

./rec -s - | \
./demux \
    "./splitter \
        \"./streamer -c wwf91.conf\" \
        \"./capture -d /public/file/0/nwr/wwf91 -l wwf91.capture.log -s \\\"./alert wwf91\\\" -\" \
        \"./log /public/greg/nwr/wwf91\"" \
    "./splitter \
        \"./streamer -c wxk27.conf\" \
        \"./capture -d /public/file/0/nwr/wxk27 -l wxk27.capture.log -s \\\"./alert wxk27\\\" -\" \
        \"./log /public/greg/nwr/wxk27\""
