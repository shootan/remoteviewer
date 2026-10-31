import io
p='apps/native_poc/src/update_handoff_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  // ---------------------------------------------------- the step, which the verdict could not say'''
new='''  // ---------------------------------------------------- the clock, at the values that broke it
  {
    check("plenty of time left", remaining_ms(1000, 2000, 10000) == 9000,
          std::to_string(remaining_ms(1000, 2000, 10000)));
    check("exactly out of time", remaining_ms(1000, 11000, 10000) == 0);
    check("past the deadline stays zero, it does not wrap",
          remaining_ms(1000, 999999, 10000) == 0);

    // THE case. A 32-bit tick count near its ceiling: the old form computed
    // `deadline = now + timeout` in 32 bits, which wrapped past zero, and every wait then ended
    // immediately reporting that the updater had run out of time. On a machine up for 49.7 days
    // no update could ever hand over, and it would have looked like a flaky server.
    const uint64_t nearWrap = 0xFFFFFF00ull;  // ~4.29e9 ms, a little under 2^32
    check("near the 32-bit ceiling: the full timeout is still available",
          remaining_ms(nearWrap, nearWrap, 600000) == 600000,
          std::to_string(remaining_ms(nearWrap, nearWrap, 600000)));
    // And ACROSS it -- the point where a 32-bit sum would have gone backwards.
    check("across the 32-bit ceiling: time still counts down normally",
          remaining_ms(nearWrap, nearWrap + 1000, 600000) == 599000,
          std::to_string(remaining_ms(nearWrap, nearWrap + 1000, 600000)));

    // A clock that appears to move backwards is not evidence that time has passed. Treating it as
    // elapsed would end the wait early for a reason that has nothing to do with the update.
    check("a clock that went backwards does not shorten the wait",
          remaining_ms(5000, 4000, 10000) == 10000,
          std::to_string(remaining_ms(5000, 4000, 10000)));
  }

  // ---------------------------------------------------- the step, which the verdict could not say'''
assert old in d,'step anchor'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('the clock has cases at the boundary')
