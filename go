#!/bin/sh

./rec - | ./splitter ./streamer './decode -' './log /public/greg/nwr'
