#!/usr/bin/perl -w

use strict;

while (<>) {
    s#(?<!")(http://\S+)#<a href="$1">$1</a>#g;
    s#&lt;(\S+@\S+)&gt;#<a href="mailto:$1">&lt;$1&gt;</a>#g;
    print;
}
