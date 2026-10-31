# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/update_stop_process_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''      SetEvent(quitEvent);
      wait_until([op] { return fixture_children(op).empty(); }, 10000);
    }
  }
'''
new = '''      SetEvent(quitEvent);
      wait_until([op] { return fixture_children(op).empty(); }, 10000);
    }
  }

  // ---------------------------------------------------------------- the orphan that will not go
  //
  // The other branch, and the one that decides whether waiting is a fix or only a better message.
  // An orphan that is on its way out gets picked up by the wait above. An orphan that simply keeps
  // running cannot be helped by waiting: the deadline expires and the update is abandoned, exactly
  // as it is today. What must be different is the REASON -- "nobody was left to ask" is a different
  // problem from "it refused", and they used to print the same sentence.
  {
    ResetEvent(quitEvent);
    PROCESS_INFORMATION pk{};
    std::vector<wchar_t> cmdK(cmd.begin(), cmd.end());
    cmdK.push_back(L'\\0');
    if (CreateProcessW(nullptr, cmdK.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &si, &pk)) {
      CloseHandle(pk.hThread);
      const uint32_t kp = pk.dwProcessId;
      wait_until([kp] { return fixture_children(kp).size() == 2; }, 15000);
      TerminateProcess(pk.hProcess, 0);
      WaitForSingleObject(pk.hProcess, 10000);
      CloseHandle(pk.hProcess);
      const bool orphaned = wait_until([kp] {
        const std::vector<ProcessTarget> t = fixture_children(kp);
        return t.size() == 1 && !t[0].hasWindow;
      }, 15000);
      check("an orphan that keeps running is still there", orphaned,
            std::to_string(fixture_children(kp).size()) + " left");

      wchar_t temp[MAX_PATH]{};
      GetTempPathW(MAX_PATH, temp);
      const std::wstring inst = std::wstring(temp) + L"gnlink-orphan2-install";
      const std::wstring stg = std::wstring(temp) + L"gnlink-orphan2-staging";
      CreateDirectoryW(inst.c_str(), nullptr);
      CreateDirectoryW(stg.c_str(), nullptr);
      UpdateEffectsConfig c = config_for(inst, stg, kp);
      c.quiesceTimeoutMs = 2000;  // short: the point is what the expiry says
      WindowsUpdateEffects e(c);
      const bool prepared = e.PrepareForSwap();
      check("...PrepareForSwap still does not refuse it", prepared, e.last_error());
      const bool quiesced = prepared && e.Quiesce();
      check("...but Quiesce gives up when it never leaves", !quiesced, e.last_error());
      check("...and the reason says nobody was left to ask",
            e.last_error().find("orphaned windowless") != std::string::npos &&
                e.last_error().find("nobody to ask") != std::string::npos,
            e.last_error());
      check("...and it is NOT reported as a refusal to be asked",
            e.last_error().find("could not ask") == std::string::npos, e.last_error());
      RemoveDirectoryW(stg.c_str());
      RemoveDirectoryW(inst.c_str());

      SetEvent(quitEvent);
      wait_until([kp] { return fixture_children(kp).empty(); }, 10000);
    }
  }
'''
assert d.count(old) == 1, 'anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
