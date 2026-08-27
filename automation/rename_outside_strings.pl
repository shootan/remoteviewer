#!/usr/bin/perl
# Rename whole-word identifiers everywhere EXCEPT inside double-quoted string literals and
# EXCEPT when the identifier is a member/scope access (preceded by '.', '->' or '::').
#
# usage: perl rename_outside_strings.pl [--code-only] MAPFILE < in.cpp > out.cpp
#   MAPFILE: one "old<TAB>new" per line.
#   --code-only: leave // and /* */ comments untouched too (viewer split Phase 1 renames prose
#                words such as `decoder` and `transport`, which would otherwise mangle comments).
#
# Used by the host split refactor (Phase 1 state structs) so that log labels such as
# " abrProfile=" keep their exact text while the variable abrProfile becomes rate.abrProfile,
# and so that args.directoryUrl (a member with the same name as a renamed local) is untouched.
# Without --code-only, comments are renamed too (that keeps them accurate). Char literals are
# not special-cased; the host has none containing a double quote.
use strict;
use warnings;

my $codeOnly = 0;
if (@ARGV && $ARGV[0] eq '--code-only') { $codeOnly = 1; shift @ARGV; }
my $mapfile = shift @ARGV or die "usage: $0 [--code-only] MAPFILE < in > out\n";
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

my $inBlock = 0;   # inside a /* ... */ comment (only tracked with --code-only)

sub rename_code {
  my ($p) = @_;
  $p =~ s/$re/$map{$1}/g;
  return $p;
}

# Rename only the code portions of a non-string part, leaving // and /* */ comments alone.
sub rename_code_only {
  my ($p) = @_;
  my $out = '';
  while (length $p) {
    if ($inBlock) {
      if ($p =~ /^(.*?\*\/)(.*)$/s) { $out .= $1; $p = $2; $inBlock = 0; next; }
      $out .= $p; $p = ''; next;
    }
    if ($p =~ /^(.*?)(\/\/.*|\/\*)(.*)$/s) {
      my ($code, $tok, $rest) = ($1, $2, $3);
      $out .= rename_code($code);
      if ($tok eq '/*') { $out .= $tok; $p = $rest; $inBlock = 1; next; }
      $out .= $tok; $p = '';   # // comment runs to the end of the line
      next;
    }
    $out .= rename_code($p); $p = '';
  }
  return $out;
}

while (my $line = <STDIN>) {
  if ($codeOnly && $inBlock) {
    # still inside a block comment: only a closing */ ends it; strings inside comments are not strings
    my @parts = ($line);
    print rename_code_only($line);
    next;
  }
  my @parts = split /($str)/, $line;
  for my $p (@parts) {
    next if $p =~ /^"/;   # inside a string literal: untouched
    $p = $codeOnly ? rename_code_only($p) : rename_code($p);
  }
  print join('', @parts);
}
