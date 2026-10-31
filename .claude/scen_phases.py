import io

BS = chr(92)
p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

start = d.find('      wrapped.relaunch = [real, knobs, started, install]() mutable {')
assert start != -1, 'wrapper start'
end = d.find('        return verdict;\n      };\n', start)
assert end != -1, 'wrapper end'
end += len('        return verdict;\n      };\n')

new = '''      // Both phases, wrapped the same way. The knobs act on whichever phase starts the image
      // they name, so a scenario says "the host does not start" once and does not have to know
      // which phase that lands in.
      const auto wrap = [real, knobs, started, install](bool requiredPhase) mutable {
        // Refusing to start something is done by removing its file, so the failure is a real
        // CreateProcessW failure rather than a lambda saying no. The client is refused through
        // the seam built for it -- deleting ITS file makes the shell put a modal error dialog on
        // the desktop, which hangs an unattended run.
        std::string savedHost;
        if (requiredPhase && !knobs.startHost) {
          savedHost = read_file(install + L"''' + BS + BS + '''" + kHostName);
          DeleteFileW((install + L"''' + BS + BS + '''" + kHostName).c_str());
        }
        if (!requiredPhase && !knobs.startClient) set_shell_launch_disabled_for_test(true);
        RelaunchVerdict verdict =
            requiredPhase ? real.relaunchRequired() : real.relaunchOptional();
        if (knobs.forceVerdict && !requiredPhase) verdict = knobs.verdict;
        if (!requiredPhase && !knobs.startClient) set_shell_launch_disabled_for_test(false);
        if (!savedHost.empty()) write_file(install + L"''' + BS + BS + '''" + kHostName, savedHost);
        for (const RelaunchOutcome& o : real.lastOutcomes()) {
          if (o.startedPid != 0) started->push_back(o.startedPid);
        }
        return verdict;
      };
      wrapped.relaunchRequired = [wrap]() mutable { return wrap(true); };
      wrapped.relaunchOptional = [wrap]() mutable { return wrap(false); };
'''
d = d[:start] + new + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('scenario wrapper drives both phases')
