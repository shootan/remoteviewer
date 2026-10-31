import io

p = 'apps/native_poc/src/update_relaunch.hpp'
d = io.open(p, encoding='utf-8').read()

old = '''struct RelaunchConfig {'''
new = '''/**
 * What happened to one entry of the plan.
 *
 * Structured rather than a line of text, and structured rather than folded into one bool for the
 * whole relaunch, because the failures are not equivalent. A client that did not come back is an
 * inconvenience -- the user starts it from the Start menu. A HOST that did not come back locks a
 * remote user out of the machine entirely: there is no way in to fix it. Reporting both as
 * `false` loses exactly the distinction that decides how bad the situation is.
 */
struct RelaunchOutcome {
  std::wstring imageName;
  RelaunchKind kind = RelaunchKind::ElevatedProcess;
  /** True only when the thing is actually running now. Skipped entries are not "started". */
  bool started = false;
  /** True when the plan deliberately did not start it (a supervised child). Not a failure. */
  bool skipped = false;
  /** Why, for the log and for what the user is told. */
  std::string detail;

  /** A failure that matters: something that should have come back did not. */
  bool failed() const { return !started && !skipped; }
};

struct RelaunchConfig {'''
assert old in d, 'config anchor'
d = d.replace(old, new, 1)

old = '''  /** What the last relaunch did, one line per entry, for the log. */
  std::function<std::vector<std::string>()> lastActions;'''
new = '''  /** What the last relaunch did, per entry. Empty until relaunch() has run. */
  std::function<std::vector<RelaunchOutcome>()> lastOutcomes;
  /**
   * One sentence for the user when something did not come back, empty when everything did.
   *
   * Exists because the worst version of this failure is the silent one: the update finishes, a
   * program the user had open never reappears, and nothing says why. They conclude the update
   * deleted it.
   */
  std::function<std::string()> userNotice;'''
assert old in d, 'lastActions anchor'
d = d.replace(old, new, 1)

old = '''RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped);'''
new = '''RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped);

/** The same, against a table the caller supplies. Tests pass their own dummies. */
RelaunchEffects make_relaunch_effects(RelaunchConfig config,
                                      const std::vector<ProcessTarget>& stopped,
                                      const std::vector<KnownImage>& table);

/**
 * What to tell the user about a relaunch, or empty when there is nothing to say.
 *
 * Separated from the effects so it can be tested without starting anything, and so the wording
 * lives in one place rather than being reinvented by the host and the client separately.
 */
std::string relaunch_user_notice(const std::vector<RelaunchOutcome>& outcomes);'''
assert old in d, 'make anchor'
d = d.replace(old, new, 1)

# the shell launcher gains a seam so the no-shell path can be exercised
old = '''bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments);'''
new = '''bool launch_via_shell(const std::wstring& exePath, const std::wstring& arguments);

/**
 * Makes launch_via_shell fail without a shell being involved, for the one case that has to be
 * tested and cannot be arranged otherwise: what happens when there is no route to the user's
 * context. The answer must be "nothing is started", never "started elevated instead".
 *
 * Production never calls this. It exists because the absence of a fallback is a property worth
 * asserting, and an absence cannot be asserted without reaching the branch.
 */
void set_shell_launch_disabled_for_test(bool disabled);'''
assert old in d, 'shell decl anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('header updated')
