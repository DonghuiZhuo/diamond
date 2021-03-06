#!/usr/bin/perl

use warnings;
use strict;

my $usage = "usage: ./generate-fill-protocol.pl";

while(<>) {
    my $key = $_;
    chomp($key);
    my $keyLength = length($key);
    my $protocolString = "*3\r\n\$3\r\nSET\r\n\$$keyLength\r\n$key\r\n\$4\r\ntemp\r\n";
    print($protocolString);
}
