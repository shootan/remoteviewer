#!/usr/bin/perl
# Report every continue / break / return inside the host main loop that acts on the MAIN loop (or
# returns from main), i.e. not nested inside an inner for/while/do/switch or a lambda body.
# usage: perl loop_exits.pl START END < file     (START = the "while (!stop.load()) {" line)
use strict;
use warnings;
my ($start, $end) = @ARGV;
my $depth = 0;
my @scopes;   # stack of [kind, depth_when_opened] for inner loops / switches / lambdas
my $n = 0;
while (my $l = <STDIN>) {
  $n++;
  next if $n < $start;
  last if $n > $end;
  my $c = $l;
  $c =~ s/"(?:[^"\\]|\\.)*"//g;
  $c =~ s{//.*$}{};
  my $before = $depth;
  if ($n > $start) {
    push @scopes, ['loop', $before]   if $c =~ /\b(for|while)\s*\(/ || $c =~ /\bdo\s*\{/;
    push @scopes, ['switch', $before] if $c =~ /\bswitch\s*\(/;
    push @scopes, ['lambda', $before] if $c =~ /\[[&=]?[^\]]*\]\s*\(/ || $c =~ /\[[&=]\]\s*\{/;
    my $inner_loop   = grep { $_->[0] eq 'loop' } @scopes;
    my $inner_switch = grep { $_->[0] eq 'switch' } @scopes;
    my $in_lambda    = grep { $_->[0] eq 'lambda' } @scopes;
    if (!$in_lambda) {
      if ($c =~ /\bcontinue\s*;/ && !$inner_loop) { printf "%d: CONTINUE  %s", $n, $l; }
      if ($c =~ /\bbreak\s*;/ && !$inner_loop && !$inner_switch) { printf "%d: BREAK     %s", $n, $l; }
      if ($c =~ /\breturn\b/) { printf "%d: RETURN    %s", $n, $l; }
    }
  }
  my $o = () = $c =~ /\{/g;
  my $cl = () = $c =~ /\}/g;
  $depth += $o - $cl;
  # pop scopes whose block has closed (their opening depth is >= the new depth)
  while (@scopes && $scopes[-1][1] >= $depth) { pop @scopes; }
}
