#!/usr/bin/perl -w

use strict;

my $xmlheader = <>;
my @body = <>;
die unless $xmlheader && @body;
foreach (@body) {
    s#/># />#g;
    s#xmlns:xhtml="\S+" ##;
}
if ($body[0] !~ /^<!DOCTYPE/) {
    print $xmlheader;
    print qq#<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "DTD/xhtml1-strict.dtd">\n#;
    print @body;
}
