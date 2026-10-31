src = 'C:\\work\\gnlink-wt\\ka-r3\\apps\\native_poc\\src\\'

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

edit('host_startup_connect.cpp', [
    ('''            [&](const std::string& token, const sockaddr_in& from) {
              return clientSession.directoryAgent.AuthorizePeer(token, from);
            },
            GetTickCount64());
        if (sb.action != remote60::native_poc::StandbyAction::NotStandby) {
          if (!sb.log.empty()) std::cout << sb.log << "\\n";''',
     '''            [&](const std::string& token, const sockaddr_in& from) {
              return clientSession.directoryAgent.AuthorizePeer(token, from);
            },
            GetTickCount64(),
            clientSession.backendSessionEnded.load(std::memory_order_acquire) >> remote60::native_poc::kBackendServeSeqBits);
        if (sb.action != remote60::native_poc::StandbyAction::NotStandby) {
          if (!sb.log.empty()) std::cout << sb.log << "\\n";'''),
    ('''          (void)clientSession.AdmitHello(/*directoryAuth=*/true, /*startsSession=*/false);
          (void)sendto(readySock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                       reinterpret_cast<const sockaddr*>(&peer), peerLen);
          std::cout << "[native-video-host][standby] first session by activation entries=" << clientSession.standby.size()''',
     '''          const uint64_t firstEpoch = clientSession.AdmitHello(/*directoryAuth=*/true, /*startsSession=*/false);
          if (sb.fresh) clientSession.standby.BindActiveEpoch(sb.entry.grantId, firstEpoch);  // r3 S3: ends with it
          // r3 (Codex decision 2): the result names its operation (entry + generation), then the HelloAck.
          (void)sendto(readySock, reinterpret_cast<const char*>(&sb.activationPong), sizeof(sb.activationPong), 0,
                       reinterpret_cast<const sockaddr*>(&peer), peerLen);
          (void)sendto(readySock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                       reinterpret_cast<const sockaddr*>(&peer), peerLen);
          std::cout << "[native-video-host][standby] first session by activation entries=" << clientSession.standby.size()'''),
    ('''        clientSession.standby.SessionTakenByHello();
        helloAuthenticated = true;''',
     '''        clientSession.standby.SessionTakenByHello(GetTickCount64());
        helloAuthenticated = true;'''),
])

edit('host_startup_control.cpp', [
    ('''              [&](const std::string& token, const sockaddr_in& from) {
                return clientSession.directoryAgent.AuthorizePeer(token, from);
              },
              GetTickCount64());''',
     '''              [&](const std::string& token, const sockaddr_in& from) {
                return clientSession.directoryAgent.AuthorizePeer(token, from);
              },
              GetTickCount64(),
              // r3 S3: the active grant ends with its control session's real end (a resume/pause is none).
              clientSession.backendSessionEnded.load(std::memory_order_acquire) >> remote60::native_poc::kBackendServeSeqBits);'''),
    ('''              const uint64_t epoch = clientSession.AdmitHello(true, true);
              std::cout << "[native-video-host][control] session epoch=" << epoch << (changed ? " peer=new" : " peer=same")
                        << " (standby activation, entries=" << clientSession.standby.size() << ")\\n";
              clientSession.AwaitControlReady(epoch);
            }
            (void)sendto(clientSession.clientSock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                         reinterpret_cast<const sockaddr*>(&peer), peerLen);''',
     '''              const uint64_t epoch = clientSession.AdmitHello(true, true);
              clientSession.standby.BindActiveEpoch(sb.entry.grantId, epoch);  // r3 S3: the grant ends with it
              std::cout << "[native-video-host][control] session epoch=" << epoch << (changed ? " peer=new" : " peer=same")
                        << " (standby activation, entries=" << clientSession.standby.size() << ")\\n";
              clientSession.AwaitControlReady(epoch);
            }
            // r3 (Codex decision 2): the result names its operation (entry + generation), then the HelloAck.
            (void)sendto(clientSession.clientSock, reinterpret_cast<const char*>(&sb.activationPong), sizeof(sb.activationPong),
                         0, reinterpret_cast<const sockaddr*>(&peer), peerLen);
            (void)sendto(clientSession.clientSock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                         reinterpret_cast<const sockaddr*>(&peer), peerLen);'''),
    ('''            if (startsSession) clientSession.standby.SessionTakenByHello();''',
     '''            if (startsSession) clientSession.standby.SessionTakenByHello(GetTickCount64());'''),
])
print('ok')
