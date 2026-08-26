#!/usr/bin/perl
# Print the brace depth (comments/strings stripped) at the START of each requested line, counting
# from line START. usage: perl loop_depth_at.pl START END LINE... < file
use strict;
use warnings;
my ($start, $end, @lines) = @ARGV;
my %want = map { $_ => 1 } @lines;
my $d = 0;
my $n = 0;
while (my $l = <STDIN>) {
  $n++;
  next if $n < $start;
  last if $n > $end;
  print "$n:d=$d " if $want{$n};
  my $c = $l;
  $c =~ s/"(?:[^"\\]|\\.)*"//g;
  $c =~ s{//.*$}{};
  my $o = () = $c =~ /\{/g;
  my $cl = () = $c =~ /\}/g;
  $d += $o - $cl;
}
print "\n";
