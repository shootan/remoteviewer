import io

# ---------------------------------------------------------------- plan header
p = 'apps/native_poc/src/update_relaunch_plan.hpp'
d = io.open(p, encoding='utf-8').read()

old = '''/**
 * The plan for a set of processes that were stopped.'''
new = '''/**
 * One image the product knows, and how it comes back.
 *
 * A table rather than a hard-coded chain so that it can be HANDED IN. That matters for one
 * reason: until it could be, the code that starts processes had no way to start anything but the
 * real product, so it was linked into nothing and had never executed. Passing a table of dummy
 * names lets the same code run for real against a temp directory.
 *
 * The allow-list property is unchanged by this. A name that is not in the table still cannot be
 * started. What moved is where the table comes from, not whether there is one.
 */
struct KnownImage {
  /** As spelled in the install directory. A launcher builds its path from this. */
  const wchar_t* name;
  /** The same, lowercased, for matching an observed image path. */
  const wchar_t* lower;
  RelaunchKind kind;
  const char* reason;
};

/** The product's own table. The default, and the only one production ever passes. */
const std::vector<KnownImage>& product_images();

/**
 * The plan for a set of processes that were stopped.'''
assert old in d, 'plan doc anchor'
d = d.replace(old, new, 1)

old = '''std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped);'''
new = '''std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped);

/** The same, against a table the caller supplies. Tests pass their own dummies. */
std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped,
                                         const std::vector<KnownImage>& table);'''
assert old in d, 'plan decl anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- plan impl
p = 'apps/native_poc/src/update_relaunch_plan.cpp'
d = io.open(p, encoding='utf-8').read()

start = d.find('struct KnownImage {')
end = d.find('const KnownImage* find_known(')
assert start != -1 and end != -1, 'impl table block'
table_body = d[start:end]
# keep the table entries, drop the struct definition (now in the header)
entries_start = table_body.find('const KnownImage kKnownImages[] = {')
assert entries_start != -1, 'entries'
entries = table_body[entries_start:]
entries = entries.replace('const KnownImage kKnownImages[] = {',
                          'const std::vector<KnownImage> kKnownImages = {')
new_block = '''/**
 * The product's processes and how each one comes back.
 *
 * A table rather than a chain of comparisons because the interesting content is the reasons, and
 * they are worth reading together. Everything not listed is unknown, and unknown means not
 * started -- see relaunch_plan.
 */
''' + entries
d = d[:start] + new_block + d[end:]

d = d.replace('''const KnownImage* find_known(const std::wstring& leafLower) {
  for (const KnownImage& image : kKnownImages) {
    if (leafLower == image.lower) return &image;
  }
  return nullptr;
}''', '''const KnownImage* find_known(const std::wstring& leafLower,
                             const std::vector<KnownImage>& table) {
  for (const KnownImage& image : table) {
    if (leafLower == image.lower) return &image;
  }
  return nullptr;
}''', 1)

d = d.replace('''std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped) {''',
'''const std::vector<KnownImage>& product_images() { return kKnownImages; }

std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped) {
  return relaunch_plan(stopped, product_images());
}

std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped,
                                         const std::vector<KnownImage>& table) {''', 1)
d = d.replace('    const KnownImage* known = find_known(leaf);',
              '    const KnownImage* known = find_known(leaf, table);', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('R1 done: image table is injectable')
