import io
p='apps/native_poc/src/update_state_machine.cpp'
d=io.open(p,encoding='utf-8').read()
health='''  enter(UpdateState::Health);
  if (!effects.HealthCheck()) return rollback("new build did not come up healthy");

'''
i=d.find(health); assert i!=-1
d=d[:i]+d[i+len(health):]
j=d.find('  if (relaunched == RelaunchVerdict::OptionalMissing) {')
k=d.find('  enter(UpdateState::Done);')
assert j!=-1 and k>j
d=d[:k]+health+d[k:]
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('reverted to the old order')
