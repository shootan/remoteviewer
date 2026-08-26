#!/usr/bin/perl
# Print the local declarations at brace depth 1 (i.e. function top level) between lines START and END,
# counting depth from START (the line after the function's "{"). Comments and string literals are
# stripped first. Output: line|qualifier|type|name   usage: perl decls_at_depth1.pl START END < file
use strict;
use warnings;
my ($start, $end) = @ARGV;
my $d = 1;
my $n = 0;
while (my $l = <STDIN>) {
  $n++;
  next if $n < $start;
  last if $n > $end;
  my $c = $l;
  $c =~ s/"(?:[^"\\]|\\.)*"//g;
  $c =~ s{//.*$}{};
  if ($d == 1
      && $c =~ /^\s*(const |constexpr |static )?((?:[A-Za-z_][\w:]*)(?:<[^;=]*?>)?(?:\s*[&*])?)\s+([A-Za-z_]\w*)\s*(=|\{|;)/
      && $c !~ /^\s*auto&/
      && $c !~ /^\s*(return|if|for|while|else|switch|case)\b/) {
    my ($q, $t, $nm) = ($1 // '', $2, $3);
    $q =~ s/\s+$//;
    print "$n|$q|$t|$nm\n";
  }
  my $o = () = $c =~ /\{/g;
  my $cl = () = $c =~ /\}/g;
  $d += $o - $cl;
}
