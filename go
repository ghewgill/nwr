#!/bin/sh

./rec - | ./squelch 10 | ./splitter ./streamer './decode -' './log /public/greg/nwr'
