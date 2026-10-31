import io

p = 'apps/native_poc/src/update_manifest.cpp'
d = io.open(p, encoding='utf-8').read()

# Schema 1's single-artifact handler still runs first, so an artifact=name|size|... line lands
# there and the schema-2 branch is never reached. Schema 1 is no longer accepted, so it goes.
old = '''    } else if (key == "artifact") {
      out->artifact = value;
    } else if (key == "size") {
      if (!parse_u64(value, &out->size)) {
        *detail = "size is not a number";
        return false;
      }
    } else if (key == "sha256") {
      out->sha256 = value;
    } else if (key == "releaseId") {'''
new = '''    } else if (key == "releaseId") {'''
assert old in d, 'schema-1 handlers not found'
d = d.replace(old, new, 1)

# and the artifact branch no longer needs to guard on a pipe, since nothing else claims the key
old2 = """    } else if (key == "artifact" && value.find('|') != std::string::npos) {"""
new2 = '''    } else if (key == "artifact") {'''
assert old2 in d, 'artifact branch not found'
d = d.replace(old2, new2, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('schema-1 artifact handlers removed')
