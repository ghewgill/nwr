#! /usr/bin/perl -w

use strict;

use DBI;

my $db = DBI->connect("DBI:Pg:dbname=nwr") or die;
print "<?xml version=\"1.0\"?>\n";
print "<eas>\n";
print "  <station>$ARGV[0]</station>\n";
my $s = $db->prepare("select id, raw, originator, event, issued, received, purge, sender, filename from message where station = upper(?) order by issued desc");
my $r = $s->execute($ARGV[0]);
while (my @a = $s->fetchrow) {
    print "  <message>\n";
    print "    <raw>$a[1]</raw>\n";
    print "    <originator>$a[2]</originator>\n";
    print "    <event>$a[3]</event>\n";
    print "    <issued>$a[4]</issued>\n";
    print "    <received>$a[5]</received>\n";
    print "    <purge>$a[6]</purge>\n";
    print "    <sender>$a[7]</sender>\n";
    print "    <filename>$a[8]</filename>\n";
    my $sa = $db->prepare("select part, state, county from message_area where message_id = ?");
    my $ra = $sa->execute($a[0]);
    while (my @aa = $sa->fetchrow) {
        print "    <area part=\"$aa[0]\" state=\"$aa[1]\" county=\"$aa[2]\" />\n";
    }
    print "  </message>\n";
}
print "</eas>\n";
$db->disconnect;
