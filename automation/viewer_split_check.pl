#!/usr/bin/perl
# Viewer split refactor, gate B ("move identity"): every line range moved out of the monolith must
# reappear verbatim, as one contiguous block, in the new file.
#
# usage: perl automation/viewer_split_check.pl BASE_REV RANGES NEWFILE [SRC]
#   BASE_REV : git revision holding the pre-move source (e.g. HEAD~1 or a commit hash)
#   RANGES   : comma-separated "a-b" line ranges of SRC at BASE_REV (1-based, inclusive)
#   NEWFILE  : the file the ranges were moved into (working tree path)
#   SRC      : source path at BASE_REV (default apps/native_poc/src/native_video_client_main.cpp)
#
# Normalisation (the only differences header separation is allowed to introduce):
#   - CRLF -> LF
#   - default arguments dropped from a definition ("= nullptr", "= false") when the declaration
#     in the header carries them (C++ forbids repeating them on the definition)
#   - a definition that was `inline`/`static` in the monolith may lose or gain that keyword
# Everything else must be byte-identical. Exit 0 only when every range is found.
use strict;
use warnings;

my ($rev, $ranges, $newfile, $src) = @ARGV;
$src //= 'apps/native_poc/src/native_video_client_main.cpp';
die "usage: $0 BASE_REV RANGES NEWFILE [SRC]\n" unless defined $newfile;

my $base = `git show "$rev:$src"`;
die "git show failed for $rev:$src\n" if $? != 0 || !length $base;
$base =~ s/\r\n/\n/g;
my @lines = split /\n/, $base, -1;

open(my $nh, '<:raw', $newfile) or die "open $newfile: $!";
my $new = do { local $/; <$nh> };
close $nh;
$new =~ s/\r\n/\n/g;

sub norm {
  my ($s) = @_;
  $s =~ s/ = nullptr\)/)/g;
  $s =~ s/ = nullptr,/,/g;
  $s =~ s/ = false\)/)/g;
  $s =~ s/ = false,/,/g;
  $s =~ s/^inline //mg;
  $s =~ s/^static //mg;
  return $s;
}
my $newn = norm($new);

my $fail = 0;
for my $r (split /,/, $ranges) {
  my ($a, $b) = $r =~ /^(\d+)-(\d+)$/ or die "bad range '$r'\n";
  die "range $r beyond file (" . scalar(@lines) . " lines)\n" if $b > @lines;
  my $block = join("\n", @lines[$a - 1 .. $b - 1]);
  my $blockn = norm($block);
  my $pos = index($newn, $blockn);
  if ($pos < 0) {
    # Find the first line that breaks the match to make the report actionable.
    my @bl = split /\n/, $blockn, -1;
    my $ok = 0;
    for my $i (0 .. $#bl) {
      my $prefix = join("\n", @bl[0 .. $i]);
      last if index($newn, $prefix) < 0;
      $ok = $i + 1;
    }
    printf "FAIL %s (%d lines): diverges at block line %d (src line %d): %s\n",
      $r, $b - $a + 1, $ok + 1, $a + $ok, ($bl[$ok] // '<eof>');
    $fail = 1;
  } else {
    printf "ok   %s (%d lines) found at offset %d\n", $r, $b - $a + 1, $pos;
  }
}
print $fail ? "MOVE CHECK: FAIL\n" : "MOVE CHECK: PASS\n";
exit $fail;
