#!/usr/bin/perl
# Rename whole-word identifiers everywhere EXCEPT inside double-quoted string literals and
# EXCEPT when the identifier is a member/scope access (preceded by '.', '->' or '::').
#
# usage: perl rename_outside_strings.pl MAPFILE < in.cpp > out.cpp
#   MAPFILE: one "old<TAB>new" per line.
#
# Used by the host split refactor (Phase 1 state structs) so that log labels such as
# " abrProfile=" keep their exact text while the variable abrProfile becomes rate.abrProfile,
# and so that args.directoryUrl (a member with the same name as a renamed local) is untouched.
# Comments are renamed too (that keeps them accurate). Char literals are not special-cased;
# the host has none containing a double quote.
use strict;
use warnings;

my $mapfile = shift @ARGV or die "usage: $0 MAPFILE < in > out\n";
my %map;
open(my $mf, '<', $mapfile) or die "cannot open $mapfile: $!";
while (my $l = <$mf>) {
  chomp $l;
  next unless $l =~ /\S/;
  my ($old, $new) = split /\t/, $l;
  $map{$old} = $new;
}
close $mf;

my $alt = join('|', map { quotemeta } sort { length($b) <=> length($a) } keys %map);
# Not after '.', '->' or '::' (member / scope access), and only whole words.
my $re = qr/(?<![.>:\w])($alt)\b/;

# A string literal: opening quote, then any run of non-quote/non-backslash chars or
# backslash-escaped chars, then the closing quote.
my $str = qr/"(?:[^"\\]|\\.)*"/;

while (my $line = <STDIN>) {
  my @parts = split /($str)/, $line;
  for my $p (@parts) {
    next if $p =~ /^"/;   # inside a string literal: untouched
    $p =~ s/$re/$map{$1}/g;
  }
  print join('', @parts);
}
