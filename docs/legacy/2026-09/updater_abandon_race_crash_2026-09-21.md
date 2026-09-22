# The 0xC0000409 in `remote60_update_stop_process_test` — root cause

2026-09-21. Raised ahead of item G's elevated run and ahead of any packaging, because the block the
crash appeared to die in (`--fixture-denied`, "a running process this updater cannot identify")
exercises product code this task changed. The question to settle first was **product or fixture**.

## Verdict

**Fixture. A test-only defect, in code I wrote in commit `edead98` (r6).** No product frame appears
anywhere in the failing stack, and no product file is changed by the fix.

## How it was established, in order

### 1. The status code does not name the cause

`0xC0000409` is `STATUS_STACK_BUFFER_OVERRUN` by name, and reading it that way is what pointed the
first look at the product's identity and stop paths. `__fastfail` raises it for a family of
reasons; the **first exception parameter** says which — `FAST_FAIL_STACK_COOKIE_CHECK_FAILURE` (2)
and `FAST_FAIL_FATAL_APP_EXIT` (7, which is how the CRT implements `abort()`) arrive identically.
`__fastfail` bypasses SEH and vectored handlers, so nothing inside the process can observe it.

`apps/native_poc/src/crash_probe.cpp` was written for this: a minimal debugger loop that prints the
exception record, its parameters, and a symbolised stack of the faulting thread.

### 2. The fault address is in `abort`, not in any of our code

Windows Error Reporting gave offset `0x455a1`, identical across five occurrences. Relinked with
`/MAP` and resolved against `Preferred load address 0x140000000`:

```
0001:0004456c  abort  000000014004556c  f  libucrt:abort.obj      → abort + 0x35
```

### 3. Under the debugger, the process dies at an unhandled C++ throw

Pre-fix binary, built from commit `6a30c55` with `/Zi /DEBUG` added and nothing else:

| artifact | SHA-256 |
| --- | --- |
| `remote60_update_stop_process_test.exe` | `9a52ec38f1f2b359506d02d03eab77eab06e5a12bbbd1d811378d1de01f25091` |
| `remote60_update_stop_process_test.pdb` | `448f4e07c46de5ae6c932c91df4508bcdf94c7808fd7e6e10d259e56e9dbe7a4` |

```
code           : 0xE06D7363
chance         : first        flags: 0x00000081 (noncontinuable)
parameters (4):
  [0] 0x19930520      = the MSVC C++ throw magic
  [1] 0xe194371700    = the thrown object
  [2] 0x7ff6ee777670  = its ThrowInfo
  [3] 0x7ff6ee640000  = the module the throw is in
stack of thread 15060:
  #0  KERNELBASE!RaiseException+0x8a
  #1  remote60_update_stop_process_te+0x8cd1b  _CxxThrowException+0x97   [throw.cpp:80]
  #2  remote60_update_stop_process_te+0x60292  std::_Xlength_error+0x22  [xthrow.cpp:19]
  #3  remote60_update_stop_process_te+0xe5d0   std::_Xlen_string+0x10    [xstring:531]
  #4  remote60_update_stop_process_te+0x3a2d8  main+0x7bc8
      [apps/native_poc/src/update_stop_process_test.cpp:1743]
  #5  __scrt_common_main_seh+0x10c
```

`std::_Xlen_string` → `length_error` → unhandled → `terminate()` → `abort()` → `0xC0000409`. Frame
#4 is test code. There is no product frame in the stack.

### 4. A known-answer control, both ways round

`crash_probe --selftest-uncaught` throws `std::length_error` and catches nothing:

* under the probe: `0xE06D7363`, magic `0x19930520`, first chance then second — the same signature;
* run directly, no debugger: exit `0xC0000409`, and WER logs exception code `0xc0000409`.

So the same cause produces both codes depending only on whether a debugger is attached. The
stack-cookie reading is excluded by evidence rather than by argument.

## The defect

```cpp
check("this run's scratch directory was removed whole", remove_scratch_run_dir(),
      std::string(scratch_run_dir().begin(), scratch_run_dir().end()));
```

`scratch_run_dir()` returned `std::wstring` **by value**, so the two calls made two separate
temporaries and the iterators came from different objects. The distance between them is whatever
the two addresses happen to be; large enough, and `std::string` throws. When the temporaries land
close together the run passes — which is the whole of the intermittency, and why two runs in five
were green.

### Why it always looked like the same place

This file prints with `std::printf`, buffered when stdout is redirected. About sixty lines is one
buffer, so a run that died at the end flushed its first buffer and lost the rest. The truncation
point was the buffer boundary, not the crash point — `check` 61 of 111 every time. Unbuffering
stdout during the investigation moved the apparent failure to the real one.

## The fix, and why it is not just the one line

1. The call site binds once and takes both iterators from that object.
2. `scratch_root()` and `scratch_run_dir()` return `const std::wstring&`. The expression above is
   then correct by construction, rather than correct as long as nobody writes it again.
3. A regression that pins the mechanism instead of waiting for it, since repetition cannot test
   this — two calls must denote one object, checked by comparing `.data()`, plus the exact
   expression that used to be undefined. Reverting the return types fails all three
   deterministically.
4. `--fixture-sweepprobe` runs the sweep block alone, so it can be sampled twenty times in twenty
   processes instead of once per forty-second suite run.

## Acceptance gate (fixed before running)

Same Release candidate, same machine: the sweep block **20×** in fresh processes and the whole stop
suite **10×**, every run exit 0, every suite run reaching all 111 checks with none failed, and zero
residue (no scratch directory, no stray fixture process). One failure fails the batch; the count
does not restart without a cause and a change.

## Still not confirmed (from the crash round)

* Whether other suites carry the same two-temporaries shape. A search of `apps/native_poc/src` for
  `X().begin(), X().end()` found exactly one occurrence, the one above. That search only catches
  this exact spelling.

---

# The 108/3, which was a second and separate defect

The crash round left one thing open: a run in fourteen that reported "108 PASS / 3 FAIL" without
crashing. It is now reproduced, explained and fixed, and it is **not** the same defect as the
abort.

## Verdict

**Fixture, again, and nothing to do with the product.** Every captured occurrence recorded
`residue=0`, `stray=0`, and its own scratch sweep passing — the shared boundary, the handle
discipline and the cleanup were untouched in all of them. The two suites the elevated run actually
depends on, `test_scratch_dir_test` and `update_service_stop_test`, ran 10/10 clean throughout,
including while contending with the capture loop.

## What it actually was

Every block of `update_stop_process_test` started its fixtures on **the same named event** and
called `ResetEvent` first. The children wait on that name, so a `SetEvent` meant for one block
releases every child of every block. And the signal need not arrive in program order: a parent
fixture's `WM_CLOSE` handler signals the name from its own message loop, whenever it is next
scheduled. Under load that can land after the following block has reset the event and started its
fixtures — and that block's child leaves at once, which is exactly "0 left" on a check whose whole
premise is a child that stays.

## Evidence

| step | result |
| --- | --- |
| First capture, under load | `0 left; fixture up=yes after 47ms of 15000, orphaned=NO after 15047ms` — so not a slow start: the fixture came up in its usual 47 ms and the child then vanished |
| Second capture | `pass=108 fail=3` in the second orphan block — the original symptom, reproduced |
| Forced signal on the shared name | `pass=108 fail=3`, same block, same three checks, `quitEvent=SIGNALLED`, `child pid=24132 exited 0 [open=ok wait=0 after 0ms]` — identical to the captured failure |
| Per-block events, then the shared name restored as a mutation | the isolation check fails deterministically: `1 of 2 still running`, cascading into `0 left` and `quitEvent=SIGNALLED` |

The child's note is what separated the two candidate explanations. "Something signalled it"
(`open=ok wait=0`) and "it never waited at all" (`open=FAILED err=…`) are opposite causes, and a
pid that is simply absent cannot tell them apart. That ambiguity cost two capture rounds before
the child was made to write down why it stopped.

## The fix

Each block takes its own event, its own name and its own command line (`FixtureRound`,
`next_round()`), so a signal for one block cannot reach another's children. One fixture keeps the
base name — the long-lived one that runs for the rest of the test — and after the change it is the
only user of it.

The mechanism is pinned by a deterministic regression rather than by repetition: with a fixture
up, the test signals a *different* block's event and asserts that this block's child is still
running. Before the change that check could not even be written, because there was only one event
and signalling it released everything.

## Two reporting defects found on the way, both mine

* **"108/3" was never three failures.** It was 107 passed, 1 failed, and **3 checks that never
  ran**, because the failing check guards three dependents. My harness reported the shortfall as
  failures. The suite now prints `checks: N run, M skipped, K failed`, names a skipped group, and
  returns `INCOMPLETE` rather than `PASS` when a run does less than a full one.
* **Output was buffered.** `std::printf` to a redirected stdout buffers about sixty lines, so any
  run that ended abnormally lost everything after its last flush. That is what made the r8 abort
  appear four hundred lines before it happened. Unbuffered now, permanently.

## Acceptance gate

Same candidate, quiet runs **and** runs under restarted load — the failure only ever appeared
under contention, so a quiet-only gate would prove nothing about what was fixed. Every run must
exit 0 with all 112 checks run, none skipped, none failed, and no residue or stray process.

## Still not confirmed

* That the stray signal in the wild came from a `WM_CLOSE` handler specifically. The shared event
  is the only mechanism that produces this symptom, and forcing a signal on it reproduces the
  failure exactly, but no capture recorded the signaller itself. The fix removes the whole class
  either way.

## Correction to the r7 report

r7 said the crash was "pre-existing, and not a regression from this work", on the grounds that the
file was unchanged since the previous commit. That commit is **my own, from r6** — so it was a
regression from this work, one round earlier, and the conclusion drawn from it ("not ours, report
and move on") was wrong.

## Kept

`crash_probe` and the `/Zi /DEBUG` on this suite are kept rather than reverted. Without symbols the
first two hours of this went into an offset that turned out to name `abort`; the next crash should
cost minutes.
