#!/usr/bin/perl
# Viewer split refactor: move whole top-level blocks (functions, structs, enums, constants) out of a
# source file into a new module, verbatim, and print the line ranges that were moved so that
# viewer_split_check.pl can prove identity against the pre-move revision.
#
# usage: perl automation/viewer_split_move.pl --src FILE --cpp OUT.cpp --hpp OUT.hpp \
#          --cpp-prelude FILE --hpp-prelude FILE [--footer TEXT] [--header-only] \
#          --start 'REGEX' [--start 'REGEX' ...] [--no-leading-comments]
#
# Each --start REGEX must match exactly one line at column 0 in --src; the block runs from that
# line (plus the comment lines directly above it, unless --no-leading-comments) to the first
# following line that is exactly "}" or "};" (a one-line block ends on its own line when it
# already contains ";" and no "{").
#
# Classification of a block by its first code line:
#   struct/class/enum/constexpr/inline/template/using/typedef  -> header, verbatim
#   anything else (a function definition)                       -> .cpp verbatim (+ a declaration
#                                                                  in the header, default args kept
#                                                                  in the declaration and dropped
#                                                                  from the definition)
# --header-only puts every block in the header; function definitions get an `inline ` prefix.
#
# The source file is rewritten without the moved lines (CRLF preserved). Output files are written
# with CRLF. Prints one line "RANGES a-b,c-d,..." (line numbers of the ORIGINAL source) for gate B.
use strict;
use warnings;
use Getopt::Long;

my ($src, $cpp, $hpp, $cppPrelude, $hppPrelude, $footer, $headerOnly, $noLead);
my @starts;
$footer = "}  // namespace remote60::native_poc::viewer\n";
GetOptions(
  'src=s' => \$src, 'cpp=s' => \$cpp, 'hpp=s' => \$hpp,
  'cpp-prelude=s' => \$cppPrelude, 'hpp-prelude=s' => \$hppPrelude,
  'footer=s' => \$footer, 'header-only' => \$headerOnly, 'start=s' => \@starts,
  'no-leading-comments' => \$noLead,
) or die "bad args\n";
die "need --src, --hpp, --hpp-prelude and at least one --start\n" unless $src && $hpp && $hppPrelude && @starts;
die "need --cpp and --cpp-prelude unless --header-only\n" unless $headerOnly || ($cpp && $cppPrelude);

sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "open $f: $!"; local $/; my $s = <$h>; close $h; $s =~ s/\r\n/\n/g; return $s; }
sub spew  { my ($f, $s) = @_; $s =~ s/\r?\n/\r\n/g; open(my $h, '>:raw', $f) or die "write $f: $!"; print $h $s; close $h; }

my $text = slurp($src);
my @lines = split /\n/, $text, -1;
pop @lines if @lines && $lines[-1] eq '';   # trailing newline
my $n = scalar @lines;

# ---- locate blocks ----
my @blocks;   # [startIdx, endIdx, firstCodeIdx]
for my $re (@starts) {
  my @hits = grep { $lines[$_] =~ /$re/ } 0 .. $n - 1;
  die "start /$re/ matched " . scalar(@hits) . " lines (need 1): " . join(',', map { $_ + 1 } @hits) . "\n" unless @hits == 1;
  my $s = $hits[0];
  my $e;
  if ($lines[$s] =~ /;\s*(\/\/.*)?$/ && $lines[$s] !~ /\{/) {
    $e = $s;   # one-liner (constexpr, using, forward declaration)
  } elsif ($lines[$s] =~ /\{.*\}\s*;?\s*$/ && $lines[$s] !~ /\{\s*$/) {
    $e = $s;   # one-line body: inline int f() { return 1; }
  } else {
    $e = $s;
    $e++ while $e < $n && $lines[$e] !~ /^\};?\s*$/;
    die "no closing brace for block at line " . ($s + 1) . "\n" if $e >= $n;
  }
  my $first = $s;
  unless ($noLead) {
    # leading // comments and /** ... */ doc blocks directly above the start line
    while ($first > 0 && $lines[$first - 1] =~ /^\/\//) { $first--; }
    if ($first > 0 && $lines[$first - 1] =~ /^\/\*\*.*\*\/\s*$/) {
      $first--;   # one-line /** doc */
    } elsif ($first > 0 && $lines[$first - 1] =~ /^\s*\*\/\s*$/) {
      my $k = $first - 1;
      $k-- while $k > 0 && $lines[$k] !~ /^\/\*\*/;
      $first = $k if $lines[$k] =~ /^\/\*\*/;
    }
  }
  push @blocks, [$first, $e, $s];
}
@blocks = sort { $a->[0] <=> $b->[0] } @blocks;
for my $i (1 .. $#blocks) {
  die "blocks overlap at lines " . ($blocks[$i - 1][1] + 1) . "/" . ($blocks[$i][0] + 1) . "\n"
    if $blocks[$i][0] <= $blocks[$i - 1][1];
}

# ---- classify and emit ----
my @hppOut;
my @cppOut;
my @ranges;
for my $b (@blocks) {
  my ($f, $e, $s) = @$b;
  my @blk = @lines[$f .. $e];
  push @ranges, ($f + 1) . '-' . ($e + 1);
  my $code = $lines[$s];
  my $isDecl = $code =~ /^(struct|class|enum|constexpr|inline|template|using|typedef|static const)\b/;
  if ($headerOnly || $isDecl) {
    if ($headerOnly && !$isDecl) {
      # function definition -> inline in the header
      $blk[$s - $f] = 'inline ' . $blk[$s - $f];
    }
    push @hppOut, join("\n", @blk), '';
  } else {
    # declaration: signature lines up to the one ending with "{" (or the one-line body's "{")
    my @sig;
    my $decl;
    if ($s == $e && $lines[$s] =~ /^(.*?)\s*\{.*\}\s*$/) {
      @sig = ($lines[$s]);
      $decl = "$1;";
    } else {
      my $k = $s;
      while ($k <= $e) {
        push @sig, $lines[$k];
        last if $lines[$k] =~ /\{\s*$/;
        $k++;
      }
      die "no '{' in signature at line " . ($s + 1) . "\n" if $k > $e;
      $decl = join("\n", @sig);
      $decl =~ s/\s*\{\s*$/;/;
    }
    my $lead = ($s > $f) ? join("\n", @lines[$f .. $s - 1]) . "\n" : '';
    push @hppOut, $lead . $decl, '';
    # definition: drop default arguments from the signature lines
    for my $j ($s - $f .. $s - $f + $#sig) {
      $blk[$j] =~ s/ = (nullptr|false|true|0|-?\d+)(?=[,)])//g;
    }
    push @cppOut, join("\n", @blk), '';
  }
}

# ---- write outputs ----
my $hppText = slurp($hppPrelude) . join("\n", @hppOut) . "\n" . $footer;
spew($hpp, $hppText);
if (!$headerOnly) {
  my $cppText = slurp($cppPrelude) . join("\n", @cppOut) . "\n" . $footer;
  spew($cpp, $cppText);
}

# ---- rewrite the source without the moved lines ----
my %drop;
for my $b (@blocks) { $drop{$_} = 1 for $b->[0] .. $b->[1]; }
my @kept = map { $lines[$_] } grep { !$drop{$_} } 0 .. $n - 1;
spew($src, join("\n", @kept) . "\n");

print "RANGES " . join(',', @ranges) . "\n";
printf "moved %d block(s), %d lines; %s now %d lines\n", scalar(@blocks), scalar(keys %drop), $src, scalar(@kept);
