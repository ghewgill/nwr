#!/bin/sh

./rec -s - | \
./demux \
    "./splitter \
        './streamer -c wwf91.conf' \
        './decode -l wwf91.eas.log -n wwf91.notify -' \
        './log /public/greg/nwr/wwf91'" \
    "./splitter \
        './streamer -c wxk27.conf' \
        './decode -l wxk27.eas.log -n wxk27.notify -' \
        './log /public/greg/nwr/wxk27'"
