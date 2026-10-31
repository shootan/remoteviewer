import io
p='apps/native_poc/src/update_relaunch.cpp'
d=io.open(p,encoding='utf-8').read()
old='''    std::vector<std::pair<std::wstring, HANDLE>> ownedHandles;
    std::string healthDetail;
  };'''
new='''    std::vector<std::pair<std::wstring, HANDLE>> ownedHandles;
    std::string healthDetail;

    /**
     * Every handle this attempt kept is released when the attempt is over.
     *
     * Not housekeeping. An open handle to a process that has ALREADY EXITED keeps the process
     * object alive, and while that object is alive Windows still holds its image file -- so the
     * file cannot be replaced or deleted, and it reports ACCESS_DENIED as if something were still
     * running it. Nothing appears in a process snapshot, because nothing is running; the lock is
     * held by the handle alone.
     *
     * Keeping the handles is what makes ownership provable, so this is the price of that, and it
     * is paid here rather than left to whoever noticed a file that would not move.
     */
    ~Shared() {
      for (const auto& owned : ownedHandles) {
        if (owned.second) CloseHandle(owned.second);
      }
    }
  };'''
assert old in d,'shared struct'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('handles are released with the attempt')
