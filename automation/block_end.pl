#!/usr/bin/perl
# Print the line where the block opened on line START closes (brace depth returns to 0), with
# comments and string literals stripped before counting. usage: perl block_end.pl START < file
use strict;
use warnings;
my ($start) = @ARGV;
my $d = 0;
my $n = 0;
while (my $l = <STDIN>) {
  $n++;
  next if $n < $start;
  my $c = $l;
  $c =~ s/"(?:[^"\\]|\\.)*"//g;
  $c =~ s{//.*$}{};
  my $o = () = $c =~ /\{/g;
  my $cl = () = $c =~ /\}/g;
  $d += $o - $cl;
  if ($n > $start && $d == 0) { print "$n\n"; last; }
  if ($n == $start && $d == 0) { print "$n\n"; last; }
}
