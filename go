#!/bin/sh

./rec -s - | ./demux "./splitter ./streamer './decode -' './log /public/greg/nwr/wxk27'" "cat >/dev/null"
