#!/usr/bin/perl -w

my %fips;
while (<>) {
    my @a = split /\t/;
    $fips{$a[4]*1000+$a[5]} = $a[6];
}
foreach (sort { $a <=> $b } keys %fips) {
    print "{$_, \"$fips{$_}\"},\n";
}
