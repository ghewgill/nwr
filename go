#!/bin/sh

./rec -s - | \
./demux \
    "./splitter \
        './streamer -c wxk27.conf' \
        './decode -l wxk27.eas.log -' \
        './log /public/greg/nwr/wxk27'" \
    "cat >/dev/null"
