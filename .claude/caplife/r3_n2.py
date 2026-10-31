src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = src + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

p = src + 'host_backend_owner.hpp'
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n').replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

edit('host_main_loop_mailbox.hpp', [
    ('''  uint32_t seq = 0;
  uint32_t backend = 0;  // 1 = dxgi, 2 = wgc, 3 = gdi
};''',
     '''  uint32_t seq = 0;
  uint32_t backend = 0;  // 1 = dxgi, 2 = wgc, 3 = gdi
  // capture-lifecycle r3 (Codex N2 end): the Serve() it was posted from (SessionState::backendServeSeq; 0 =
  // unstamped). A request of a Serve() that has ended is dropped (host_backend_owner.hpp).
  uint64_t serve = 0;
};'''),
    ('''  std::optional<BackendRequest> TakeBackendRequestForOwner(uint64_t ownerEpoch) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!backend_) return std::nullopt;
    switch (ClassifyBackendRequest(backend_->epoch, ownerEpoch)) {''',
     '''  std::optional<BackendRequest> TakeBackendRequestForOwner(const BackendOwnerState& owner) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!backend_) return std::nullopt;
    switch (ClassifyBackendRequest(backend_->epoch, backend_->serve, owner)) {'''),
])

edit('host_session.hpp', [
    ('''  // capture-lifecycle r2 (Codex N2): the epoch whose control session really ended -- its Serve() returned while
  // it was still current and no resume was pending (the same rule that turns its stream off). The main loop
  // puts that owner's backend choice back to the host default, once (host_backend_owner.hpp).
  std::atomic<uint64_t> backendSessionEndedEpoch{0};''',
     '''  // capture-lifecycle r2 (Codex N2): the control session that really ended -- its Serve() returned while it was
  // still current and no resume was pending (the same rule that turns its stream off), packed with that
  // Serve()'s own sequence (r3: PackBackendSessionEnd). The main loop puts that owner's backend choice back to
  // the host default, once, and drops that Serve()'s requests (host_backend_owner.hpp). Every Serve() takes
  // the next backendServeSeq.
  std::atomic<uint64_t> backendSessionEnded{0};
  std::atomic<uint64_t> backendServeSeq{0};'''),
])

edit('host_control_session.cpp', [
    ('''  // Keys and buttons held from an earlier client are not this one's; a resume keeps its marks.
  clientSession.injectedInput.BeginServe(servedEpoch);''',
     '''  // Keys and buttons held from an earlier client are not this one's; a resume keeps its marks.
  clientSession.injectedInput.BeginServe(servedEpoch);
  // capture-lifecycle r3 (Codex N2 end): this Serve()'s own sequence -- its backend requests carry it, its end
  // names it.
  const uint64_t servedSeq =
      (clientSession.backendServeSeq.fetch_add(1, std::memory_order_acq_rel) + 1) & kBackendServeSeqMask;'''),
    ('''        mailbox.PostBackendRequest({servedEpoch, req.seq, req.backend});''',
     '''        mailbox.PostBackendRequest({servedEpoch, req.seq, req.backend, servedSeq});'''),
    ('''    clientSession.backendSessionEndedEpoch.store(servedEpoch, std::memory_order_release);''',
     '''    clientSession.backendSessionEnded.store(PackBackendSessionEnd(servedEpoch, servedSeq), std::memory_order_release);'''),
])

edit('host_stage_backend.cpp', [
    ('''  // taken and marked under; its real end (backendSessionEndedEpoch) puts the choice back too, once.''',
     '''  // taken and marked under; its real end (backendSessionEnded) puts the choice back too, once, and that ended
  // Serve()'s requests -- queued or late -- are dropped (r3).'''),
    ('''        backend.owner, curEpoch, clientSession.backendSessionEndedEpoch.load(std::memory_order_acquire));''',
     '''        backend.owner, curEpoch, clientSession.backendSessionEnded.load(std::memory_order_acquire));'''),
    ('''  if (const auto backendReq = hx.mailbox.TakeBackendRequestForOwner(backend.owner.ownerEpoch)) {''',
     '''  if (const auto backendReq = hx.mailbox.TakeBackendRequestForOwner(backend.owner)) {'''),
])
print('ok')
