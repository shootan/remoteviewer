# Host / PC 클라이언트 조사 파일별 목록 (2026-09-12)

[발견 원장](host_pc_thread_recovery_audit_2026-09-12.md)과 함께 읽는다. 아래 해시는 조사 시점 사본의 SHA-256이다.

- 기준 HEAD: `8bc9d1c2e5b5ae6f617e6ede93cb384993367753` + 워킹트리. 364파일, 97,904줄.
- `분기 추적`: 66파일. 해당 파일 전체의 안전성 인증이 아니라 본 원장에 연결되는 함수/호출·실패 분기를 직접 읽고 대조했다.
- `검색`: 298파일. 목록·해시·전역 위험 패턴 검색만 수행했으며 전체 함수 정독/동적 검증을 주장하지 않는다.
- 패턴: T=thread 생성/detach/join, W=blocking I/O/wait, S=mutex/atomic/UI message, R=restart/retry/watchdog, E=실패/예외/강제 종료. 주석·문자열도 매치되므로 개수는 결함 수가 아니다.
- 검색 대상: rg가 열거하는 apps/native_poc, apps/directory, libs의 .cpp/.hpp/.h/.js/.html, CMakeLists.txt, package.json. Android 전용 및 third_party 제외. 심층 추적이 필요한 잔여 영역은 원장 미검증에 남긴다.

## 스레드 생성·종료 패턴이 있는 파일

| 파일 | 구분 | 매치 줄 |
|---|---|---|
| `apps/directory/server.js` | 제품/공유 | 78, 246, 328, 376, 377, 425, 426, 1520, 1522 |
| `apps/directory/test/logs_test.js` | 테스트 | 72, 91, 124 |
| `apps/directory/test/observe_contract_test.js` | 테스트 | 74, 76, 94, 108, 115 |
| `apps/directory/test/proxy_contamination_test.js` | 테스트 | 117, 118, 183 |
| `apps/directory/test/run.js` | 테스트 | 8, 9, 28, 145, 170, 189, 204, 210, 226, 232, 233, 234, 241, 264 |
| `apps/directory/test/update_manifest_test.js` | 테스트 | 16, 33, 34, 35 |
| `apps/directory/test/update_publish_contract_test.js` | 테스트 | 46, 72, 97 |
| `apps/directory/test/version_compare_test.js` | 테스트 | 15 |
| `apps/directory/update_manifest.js` | 제품/공유 | 219 |
| `apps/native_poc/src/client_shell_main.cpp` | 제품/공유 | 548, 640, 754, 811, 826, 838, 1006, 1015, 1025, 1189 |
| `apps/native_poc/src/client_update_flow_test.cpp` | 테스트 | 108, 117, 154 |
| `apps/native_poc/src/d3d_capture_readback.cpp` | 제품/공유 | 102, 359 |
| `apps/native_poc/src/d3d_capture_readback.hpp` | 제품/공유 | 260 |
| `apps/native_poc/src/directory_client.cpp` | 제품/공유 | 473, 479 |
| `apps/native_poc/src/directory_client.hpp` | 제품/공유 | 311 |
| `apps/native_poc/src/directory_http_contract_test.cpp` | 테스트 | 65, 75, 142 |
| `apps/native_poc/src/directory_retry_test.cpp` | 테스트 | 80, 81, 109, 110, 120, 121, 277, 278, 336, 389 |
| `apps/native_poc/src/gdi_capture_process.cpp` | 제품/공유 | 54, 160, 291 |
| `apps/native_poc/src/host_app_main.cpp` | 제품/공유 | 237, 272, 557, 558, 577, 651, 1215, 1269, 1346, 1363 |
| `apps/native_poc/src/host_encoded_sender.cpp` | 제품/공유 | 101 |
| `apps/native_poc/src/host_encoded_sender.hpp` | 제품/공유 | 172 |
| `apps/native_poc/src/host_main_loop_mailbox_test.cpp` | 테스트 | 124, 144 |
| `apps/native_poc/src/host_sender_epoch_test.cpp` | 테스트 | 102 |
| `apps/native_poc/src/host_session.hpp` | 제품/공유 | 84, 86, 87 |
| `apps/native_poc/src/host_shutdown.cpp` | 제품/공유 | 133, 139, 140, 148 |
| `apps/native_poc/src/host_startup.hpp` | 제품/공유 | 31, 51 |
| `apps/native_poc/src/host_startup_capture.cpp` | 제품/공유 | 104, 108, 114, 372 |
| `apps/native_poc/src/host_startup_control.cpp` | 제품/공유 | 127, 228, 389 |
| `apps/native_poc/src/host_unlock_relay.cpp` | 제품/공유 | 70, 78 |
| `apps/native_poc/src/host_unlock_relay.hpp` | 제품/공유 | 54 |
| `apps/native_poc/src/host_watchdog.hpp` | 제품/공유 | 102, 122 |
| `apps/native_poc/src/log_upload.cpp` | 제품/공유 | 127, 526, 583, 592 |
| `apps/native_poc/src/log_upload.hpp` | 제품/공유 | 129 |
| `apps/native_poc/src/log_upload_shutdown_test.cpp` | 테스트 | 5, 82, 93, 124, 233, 247, 256 |
| `apps/native_poc/src/log_upload_test.cpp` | 테스트 | 96, 105, 193 |
| `apps/native_poc/src/mf_h264_codec.cpp` | 제품/공유 | 289, 323 |
| `apps/native_poc/src/native_video_client_session.cpp` | 제품/공유 | 101, 262, 634, 637 |
| `apps/native_poc/src/native_video_client_session.hpp` | 제품/공유 | 168, 169 |
| `apps/native_poc/src/native_video_client_shared_core_test.cpp` | 테스트 | 116, 117, 126, 127, 219, 220 |
| `apps/native_poc/src/native_video_host_main.cpp` | 제품/공유 | 204 |
| `apps/native_poc/src/punch_any_test.cpp` | 테스트 | 54, 73, 84, 120, 138, 148 |
| `apps/native_poc/src/secure_input_service_main.cpp` | 제품/공유 | 806, 848 |
| `apps/native_poc/src/udp_control_channel_test.cpp` | 테스트 | 44, 63, 87, 188, 226 |
| `apps/native_poc/src/update_check.cpp` | 제품/공유 | 82, 85 |
| `apps/native_poc/src/update_credential_channel_test.cpp` | 테스트 | 105, 112, 149, 154 |
| `apps/native_poc/src/update_effects_test.cpp` | 테스트 | 306, 312 |
| `apps/native_poc/src/update_http_test.cpp` | 테스트 | 65, 83, 91 |
| `apps/native_poc/src/update_registration_com_test.cpp` | 테스트 | 86, 91, 103, 106, 121, 133 |
| `apps/native_poc/src/viewer_context.hpp` | 제품/공유 | 47, 48 |
| `apps/native_poc/src/viewer_control_client.hpp` | 제품/공유 | 16 |
| `apps/native_poc/src/viewer_liveness_test.cpp` | 테스트 | 169, 178 |
| `apps/native_poc/src/viewer_startup.cpp` | 제품/공유 | 661, 695 |
| `apps/native_poc/src/viewer_thread_join.hpp` | 제품/공유 | 23, 28 |
| `apps/native_poc/src/viewer_udp_recovery_test.cpp` | 테스트 | 144, 150, 155, 567, 627, 628, 734, 735, 743, 744 |
| `apps/native_poc/src/viewer_video_receiver.hpp` | 제품/공유 | 14 |
| `apps/native_poc/tools/winlogon_capture_probe.cpp` | 도구 | 69, 75, 394, 430 |
| `libs/capture/src/capture_backend_dxgi.cpp` | 제품/공유 | 81, 557, 573 |

## 전체 파일

| 파일 | 구분 / 조사 | 줄 | 패턴 수 T/W/S/R/E | SHA-256 |
|---|---|---:|---|---|
| `apps/directory/package.json` | 빌드 / 검색 | 11 | 0/0/0/0/0 | `ceafbdf2a1d14a4fab07701427cb760aee7e9f1cdc2e373211f741fe0da40680` |
| `apps/directory/server.js` | 제품/공유 / 분기 추적 | 1540 | 9/0/0/7/18 | `e32510c3c6a7d76a6b5cb36f59a06737c6f0e7b1a5290287a595a48d8b1f7ca5` |
| `apps/directory/test/directory_test.js` | 테스트 / 검색 | 210 | 0/0/0/0/1 | `4f984550f1cb305e98ab87137550740974b2801b47d1e389876a4e3cde608d48` |
| `apps/directory/test/logs_test.js` | 테스트 / 검색 | 156 | 3/0/0/1/1 | `dc4780e1425b8f838207fa86c7e49f0e5b6ad84b0f441e8d8898586baee5b770` |
| `apps/directory/test/nat_diag_test.js` | 테스트 / 검색 | 130 | 0/0/0/0/1 | `54695f59d2bb8b5fbdeb8b6e47a54b9bb548a7a70171b7e4bf864d5b159543c9` |
| `apps/directory/test/observe_contract_test.js` | 테스트 / 검색 | 182 | 5/0/0/0/1 | `8ca78cfc81d6e8107aa1dee44bc6baf9e2ab5b7411b4414777d9fdf36446e4bf` |
| `apps/directory/test/observe_correction_test.js` | 테스트 / 검색 | 87 | 0/0/0/0/2 | `df61465e2c7601785a55b18f78eca03cd5e16d12244e7c1883e59cb37fbb7dfc` |
| `apps/directory/test/proxy_contamination_test.js` | 테스트 / 검색 | 257 | 3/0/0/1/1 | `e1b293866e5af4774460d2789641a3c5506c8ab79f1c6e8e4039dd1b52f020d8` |
| `apps/directory/test/relay_test.js` | 테스트 / 검색 | 336 | 0/0/0/3/2 | `3084a00fa92331e1b23eab1c76aa606a46869133700fb1d758a66dd7782a4c58` |
| `apps/directory/test/restart_test.js` | 테스트 / 검색 | 125 | 0/0/0/14/1 | `64e22d4cce4fe42ff546adc14ea2097f25437512ea70ae9e7f1f3ca26b5e3284` |
| `apps/directory/test/run.js` | 테스트 / 검색 | 273 | 14/0/0/5/0 | `6f8ee9f96a46109d453db26e7e15abeadf32b63891a1f9c157aa886b8b8d8bdd` |
| `apps/directory/test/same_lan_test.js` | 테스트 / 검색 | 240 | 0/0/0/0/1 | `f347a765f71f1455676652f3f7b14fa4dd46142ba3f9d6df2a001efa6a137e3b` |
| `apps/directory/test/update_manifest_test.js` | 테스트 / 검색 | 236 | 4/0/0/0/1 | `fb93733680ddc1027a0550ff5a3ec62d3b8971ea8ce5fcc253f4498d9827178f` |
| `apps/directory/test/update_publish_contract_test.js` | 테스트 / 검색 | 193 | 3/0/0/0/0 | `71d59a5dd92d70d59244fdf58370bc3890df7cacf05cabfdef6d1748bc7770fb` |
| `apps/directory/test/update_route_test.js` | 테스트 / 검색 | 135 | 0/0/0/0/0 | `ba88a0f9ad83450730d20b419a71d695fc8a662be4a1a5e301247e446ef380dc` |
| `apps/directory/test/version_compare_test.js` | 테스트 / 검색 | 73 | 1/0/0/0/1 | `88d49a04b2d96d1efbcd3ef5cfac8ad28f7686fbd4da6df5fa644efacc53b508` |
| `apps/directory/update_manifest.js` | 제품/공유 / 검색 | 228 | 1/0/0/0/5 | `a11254aa3fb007b6c00f184d6ad7aaa36f1a1e181ae083ad238e2eb658b641bf` |
| `apps/directory/version_compare.js` | 제품/공유 / 검색 | 61 | 0/0/0/0/0 | `5117b0a4a825c85c5da07ab3b09ef96a591d3cf62e3a9ba70423e775f54139d3` |
| `apps/native_poc/CMakeLists.txt` | 빌드 / 검색 | 1639 | 0/0/0/9/0 | `be468d6e2a705116ffffe7cc6eafaf063da3155132214930d886195db71fef5a` |
| `apps/native_poc/installer/installer_ids.h` | 제품/공유 / 검색 | 31 | 0/0/0/0/0 | `16c79ca55f5d9a0e1a3020dcd00b0dab48c475ec553a556ad92cea75208ea97c` |
| `apps/native_poc/installer/installer_main.cpp` | 제품/공유 / 검색 | 703 | 0/3/2/2/16 | `d298d74a676a461a474075a439d69146f8dd85a1fcfc5c5f8665ceb073e26c14` |
| `apps/native_poc/src/backend_request_match.hpp` | 제품/공유 / 검색 | 102 | 0/0/0/0/2 | `3acc2f695536ff419b9beea1efa2271b0e637dae7db68fde803f461e837ad29f` |
| `apps/native_poc/src/bind_port_candidates.hpp` | 제품/공유 / 검색 | 58 | 0/0/0/0/0 | `9e3a11723bf1a4b07bc150948f09963f324f132eb136f8a11cb7ea719ce9a373` |
| `apps/native_poc/src/bind_port_candidates_test.cpp` | 테스트 / 검색 | 110 | 0/0/0/0/0 | `75c0dce1221d4cf1f5c4d552734163e778bd82f7d12c932e813b2c08b6d598ee` |
| `apps/native_poc/src/capture_cadence_gate.hpp` | 제품/공유 / 검색 | 130 | 0/0/0/4/2 | `cb90bc0937000fd6137fc90b6e42636e2de4f14adf811e6a524f324995e919d4` |
| `apps/native_poc/src/capture_cadence_gate_test.cpp` | 테스트 / 검색 | 211 | 0/0/0/0/0 | `5488c176065831d6dc824077cb7f7f1255c1cf28bf10bb13ff33a1a37c71f6aa` |
| `apps/native_poc/src/capture_readback_test.cpp` | 테스트 / 검색 | 122 | 0/0/0/0/0 | `009b367cb0f7100cb46e0fff682eec91e92aa0088df79684affcb7cdece8b644` |
| `apps/native_poc/src/client_macro_window.cpp` | 제품/공유 / 검색 | 383 | 0/0/0/0/3 | `6cefb8a3b4f97bcccaca037b484a42babe4222a68286ae6d8ebc7ecaff6cecc3` |
| `apps/native_poc/src/client_macro_window.hpp` | 제품/공유 / 검색 | 32 | 0/0/0/0/0 | `9cd17d8c23b64d3763be96a2be6d32c060bb368b358111a39857be32b501ee5c` |
| `apps/native_poc/src/client_session_toolbar.cpp` | 제품/공유 / 검색 | 515 | 0/0/3/1/3 | `8d022006a62cbf6845fc568df52da51262bd5c75d8396b067972022dd3e4bfdf` |
| `apps/native_poc/src/client_session_toolbar.hpp` | 제품/공유 / 검색 | 94 | 0/0/0/0/0 | `efc57c1905a7aae06c24659958cd070ee4caf57efeae92f8f3787c954e0b7f0d` |
| `apps/native_poc/src/client_shell_bridge.cpp` | 제품/공유 / 분기 추적 | 162 | 0/0/0/0/3 | `bfc40f08643111abe39239b4616629c1d5c8226a151301482ccb9138765afa94` |
| `apps/native_poc/src/client_shell_bridge.hpp` | 제품/공유 / 검색 | 127 | 0/0/0/0/0 | `b8e6108a809b3ec2c87e0eecf07976c0de178c58388fdc7ad7ec7dc0af13ec02` |
| `apps/native_poc/src/client_shell_bridge_test.cpp` | 테스트 / 검색 | 159 | 0/0/0/0/0 | `f0ae986c899ddd03a087d85c7d9d95e9fbc5efddb282210780467c8b7b9b1c86` |
| `apps/native_poc/src/client_shell_main.cpp` | 제품/공유 / 분기 추적 | 1289 | 10/3/24/1/5 | `8bf560af09169017b5d0baf5ba315f2b9cc0831317deee5cc3d4d373ca9c68d5` |
| `apps/native_poc/src/client_update_flow_test.cpp` | 테스트 / 검색 | 377 | 3/1/4/0/5 | `789c4c0ec61b9be4135d257f13b3261ab29840a6735424235591cf05ee82a027` |
| `apps/native_poc/src/client_update_gate.cpp` | 제품/공유 / 분기 추적 | 25 | 0/0/0/0/0 | `0a7eb4129a9484034a8ee6806d7368728deb3f83dcb0aaf77f3ac7f9b3f82d1b` |
| `apps/native_poc/src/client_update_gate.hpp` | 제품/공유 / 분기 추적 | 57 | 0/0/0/0/0 | `f960fd43e73ea8309c59019143482a2e11dc463249afd95c8e55ad83833c268b` |
| `apps/native_poc/src/client_update_ui_test.cpp` | 테스트 / 검색 | 734 | 0/0/5/0/9 | `93677273c4ce0bd93eb3828012b442485442349fc80f0c2cbac7e2ef4f490190` |
| `apps/native_poc/src/connect_candidates.hpp` | 제품/공유 / 검색 | 110 | 0/0/0/0/6 | `d05aad505cc8fc297ed13dd01b08bb861825e82cbf11ede4f3eb71b4a86dd36a` |
| `apps/native_poc/src/connect_candidates_test.cpp` | 테스트 / 검색 | 153 | 0/0/0/0/0 | `a9a312696cc6c0ff50f5270ffa10d78247472d943b80e7d4fb8ed0eed9fa5cb1` |
| `apps/native_poc/src/d3d_capture_readback.cpp` | 제품/공유 / 분기 추적 | 871 | 2/1/20/1/35 | `6c04a079b16e12e6b0c05fd70e9defe70c9988a01258dc6039f41bdb5b2b9514` |
| `apps/native_poc/src/d3d_capture_readback.hpp` | 제품/공유 / 검색 | 270 | 1/0/8/3/0 | `69b6712b6dfeec67db395af5beb5dc1de33497a01462e44443fcbc5f83da2c6d` |
| `apps/native_poc/src/directory_client.cpp` | 제품/공유 / 분기 추적 | 1286 | 2/0/7/3/75 | `0acb3f7ccc70ec7295eefc055ddd1398d8dd4916f156d4a4c6ab4bb625c4205f` |
| `apps/native_poc/src/directory_client.hpp` | 제품/공유 / 분기 추적 | 329 | 1/0/2/1/0 | `b66f60a842f060c3658fb07b9ee7a3d5fd9b97613f1966f05dd509c49f9ffbff` |
| `apps/native_poc/src/directory_http_contract_test.cpp` | 테스트 / 검색 | 293 | 3/0/3/1/4 | `1c9c4b41a42f9d9f2bfba027e95dd85c7d4fa297829c7f0a501d1ec2052df043` |
| `apps/native_poc/src/directory_observe.hpp` | 제품/공유 / 검색 | 91 | 0/0/0/0/0 | `73b5d5eb38a184bbe3c32c3858d6c42c68cc46f96daadbcd70eed6c3f28c31c0` |
| `apps/native_poc/src/directory_rendezvous.cpp` | 제품/공유 / 검색 | 253 | 0/0/0/0/20 | `b33c8233db8a8691e7363a2ebc942188e8cb799e40c6c5bf0f556b5b52b71193` |
| `apps/native_poc/src/directory_rendezvous.hpp` | 제품/공유 / 검색 | 76 | 0/0/0/0/0 | `2cb1355e444e83606e13c354855b0f227dca1ee0b060a89784f8d1abc563362e` |
| `apps/native_poc/src/directory_retry_test.cpp` | 테스트 / 검색 | 785 | 10/0/8/6/8 | `ee335d4dcd787e5e59a80b339c9bd3b238a088ec4e4f028f6595e2854909ef40` |
| `apps/native_poc/src/directory_session_bootstrap.cpp` | 제품/공유 / 검색 | 156 | 0/0/0/1/10 | `00d276fa8da17959557b1de0187f3cf2b176ffa967606879192ffad05d552864` |
| `apps/native_poc/src/directory_session_bootstrap.hpp` | 제품/공유 / 검색 | 72 | 0/0/0/0/0 | `57203b53577c486aad6d1f9511a3928b9a818c5bef22ee1a3ad639d6243b20d7` |
| `apps/native_poc/src/directory_session_client.cpp` | 제품/공유 / 검색 | 364 | 0/0/0/0/19 | `eba8f5ac1bacbbc632cc421693c639ea2b140de7b638d27788c1367c61b15aac` |
| `apps/native_poc/src/directory_session_client.hpp` | 제품/공유 / 검색 | 89 | 0/0/0/2/0 | `c739774721fbc6f009be30296c1b5a61b5862cd9fb5f8dea72518a7b86bc83e1` |
| `apps/native_poc/src/directory_session_client_test.cpp` | 테스트 / 검색 | 683 | 0/0/0/0/0 | `7737ea69754c58be8a5a9ce438f0c198aacdf4714c464e05bf2a91e79cba4c81` |
| `apps/native_poc/src/encode_resolution_ladder.hpp` | 제품/공유 / 검색 | 106 | 0/0/0/2/0 | `594a2ad5c6447ed761dba4f10572c2c44cc1c3cafa5a081f0de408d84704f0c7` |
| `apps/native_poc/src/encode_resolution_ladder_test.cpp` | 테스트 / 검색 | 129 | 0/0/0/1/0 | `036beb293d6bf9eed8b5d0ae47066ec9d5be2cae94d2b6f275585b0c37b31907` |
| `apps/native_poc/src/env_util.hpp` | 제품/공유 / 검색 | 54 | 0/0/0/0/4 | `83eaf96b813c63ebf1fbd7420c6a9ce30efc21896f31bba495f190685a2c2640` |
| `apps/native_poc/src/gdi_capture_process.cpp` | 제품/공유 / 검색 | 307 | 3/2/3/0/11 | `0ad7e8d43ab057c9400717a2fcc1e20d46da809b6900c47ecd2037f1db553abc` |
| `apps/native_poc/src/gdi_capture_process.hpp` | 제품/공유 / 검색 | 55 | 0/0/0/0/0 | `232109ddef3a41cf13d0f992d06af366299e020e3a931880063307c42d77a4c5` |
| `apps/native_poc/src/gdi_capture_process_test.cpp` | 테스트 / 검색 | 126 | 0/0/4/0/0 | `a82b21bbe2429c77facbb7e6f5d4f1833a34b32e6dd3f390e4bafe83227be0d1` |
| `apps/native_poc/src/gdi_capture_protocol.hpp` | 제품/공유 / 검색 | 53 | 0/0/0/0/0 | `2926370a2569aebc3d3bd139ef10198b92fa9113598a76885b1ca8fea1bba8dc` |
| `apps/native_poc/src/gdi_capture_worker_main.cpp` | 제품/공유 / 검색 | 201 | 0/1/0/3/2 | `9c4551df7cb86690fe9f980cf101157dd586f3ac92d2c185323cc1def013005c` |
| `apps/native_poc/src/handoff_fixture_main.cpp` | 제품/공유 / 검색 | 183 | 0/2/0/0/2 | `6e088ef746573490ecf5f9e61bd705768812a72d61527536ec0c557f4e9c3b1d` |
| `apps/native_poc/src/host_abr.hpp` | 제품/공유 / 검색 | 406 | 0/0/0/0/0 | `3f8af5de12c4b2a24c82fddae150abd8525d644d1275f9db52fe8454f2629c0f` |
| `apps/native_poc/src/host_abr_test.cpp` | 테스트 / 검색 | 457 | 0/0/0/1/0 | `9fcd3199106866ff57b421b6cd5636069ce07066df822be706e18e0c72fb58c6` |
| `apps/native_poc/src/host_app_main.cpp` | 제품/공유 / 분기 추적 | 2057 | 10/4/37/28/4 | `bd56adf5eea086e0ef631f488277d3a276fd8868f05c040154d439aec61d275a` |
| `apps/native_poc/src/host_args.cpp` | 제품/공유 / 검색 | 180 | 0/0/0/0/0 | `28aec7f7d5a10080cc32e7d2743241683d357e6cb10def3a4481f3eedc18b229` |
| `apps/native_poc/src/host_args.hpp` | 제품/공유 / 검색 | 79 | 0/0/0/0/0 | `342223a413dd843ae6e24cdb3b882dda807ad59b3b4cfc8fcc873bed7a04df3c` |
| `apps/native_poc/src/host_backend_policy.hpp` | 제품/공유 / 검색 | 142 | 0/0/6/21/2 | `24ce583215f1202aa7b5f9d2fcf31985c0448bf446b36cf63d232c597df63e4a` |
| `apps/native_poc/src/host_backend_policy_test.cpp` | 테스트 / 검색 | 135 | 0/0/0/23/0 | `88a2863cc4f5fd24c07a1c71a162db896fd1793b62c729e3bf1c50287a21b097` |
| `apps/native_poc/src/host_bgra_scale.cpp` | 제품/공유 / 검색 | 360 | 0/0/1/0/13 | `05ebcbdfb14291fbd85c6e8fb834552208940d9415acef7651acc2b03f6220fb` |
| `apps/native_poc/src/host_bgra_scale.hpp` | 제품/공유 / 검색 | 55 | 0/0/0/0/0 | `2a2bef7425c0115eb405e244d9e36aa200c8493785d8ca74c9ae72fffb59455f` |
| `apps/native_poc/src/host_bgra_scale_test.cpp` | 테스트 / 검색 | 111 | 0/0/0/0/0 | `edcab614da8cbfffea901ba8027df7a95cceb2c8e12c4d00d016063f8a991863` |
| `apps/native_poc/src/host_bottleneck.hpp` | 제품/공유 / 검색 | 79 | 0/0/0/0/0 | `214a1fd8ac92cd6257609a8e132fa7a2e17aa53d28807ce9208865c8ce1e6c01` |
| `apps/native_poc/src/host_capture_device.cpp` | 제품/공유 / 분기 추적 | 416 | 0/0/0/0/17 | `f29355e19b16bf564c1635d0b1cc753b8860d77db2aee4bb5f14d3b3678164a8` |
| `apps/native_poc/src/host_capture_device.hpp` | 제품/공유 / 검색 | 95 | 0/0/0/1/0 | `0f24c72827fc4d5dbe5ab88ed1ba3818759f5d09ae661d955b4556766b44989c` |
| `apps/native_poc/src/host_capture_session.cpp` | 제품/공유 / 분기 추적 | 971 | 0/0/15/14/38 | `e0cc2350275ed537d6a111a296bd39a097d8667e4666d41b16f7596c5fcae188` |
| `apps/native_poc/src/host_capture_session.hpp` | 제품/공유 / 분기 추적 | 334 | 0/0/41/8/0 | `21049a5d20865a6c3c7936cbd087d6eb44ec91ac376a91a2499b65221ff5e655` |
| `apps/native_poc/src/host_client_metrics.hpp` | 제품/공유 / 검색 | 74 | 0/0/5/0/0 | `5c7abdfc5d1b18abf8a8ef4be832e173646f53490a875e81077f09b7e262cca8` |
| `apps/native_poc/src/host_command_line.hpp` | 제품/공유 / 검색 | 69 | 0/0/0/0/0 | `59eea916be43562d7aee317589c728544bc4c0291a77ea434b7ecfff315885bf` |
| `apps/native_poc/src/host_command_line_test.cpp` | 테스트 / 검색 | 93 | 0/0/0/0/0 | `1c2038543dec1a967730e4ce43a2f6bce473dd02a75e5a178664aace6a79f5a7` |
| `apps/native_poc/src/host_control_session.cpp` | 제품/공유 / 분기 추적 | 953 | 0/1/5/7/2 | `0b9e845ab43c807d68dfb5ca560a35fa0e7645476dc25231bbfc4da079b98c9c` |
| `apps/native_poc/src/host_control_session.hpp` | 제품/공유 / 검색 | 78 | 0/0/2/0/0 | `e08621772b28a05a893b78c92336cfed5e71f13bb44e3433643e111afacac88a` |
| `apps/native_poc/src/host_encode_epoch_test.cpp` | 테스트 / 검색 | 861 | 0/0/1/6/11 | `f2435b066f66b9c2b109896990e72b736067418eba683203fd2630bb4418dd96` |
| `apps/native_poc/src/host_encoded_sender.cpp` | 제품/공유 / 분기 추적 | 348 | 1/1/6/5/2 | `ef8eb1c48cf0b591822e27a5dbd0625e30b620decd4a310128d324aeab093bc9` |
| `apps/native_poc/src/host_encoded_sender.hpp` | 제품/공유 / 검색 | 199 | 1/0/29/0/0 | `12a13f45e51eb13520badb871de74adcc7830ce2c52a5ff851d0c3a2e5ef5a20` |
| `apps/native_poc/src/host_encoder_manager.cpp` | 제품/공유 / 분기 추적 | 186 | 0/0/0/3/3 | `80596ef19d5f8b6df1b0e9b52049ccc2e531be9f34c2cae4074d8caa830410f7` |
| `apps/native_poc/src/host_encoder_manager.hpp` | 제품/공유 / 분기 추적 | 303 | 0/0/2/13/4 | `9865ab639b5f039408ff6031b3a7c6c9da9bf5612616cec9b725a411e16d92fb` |
| `apps/native_poc/src/host_epoch_gate.hpp` | 제품/공유 / 검색 | 215 | 0/0/0/3/1 | `f2b6145f729a4b8f603285eca58255c048d2a14ad5273c8e6be8b8ae7af7ecf7` |
| `apps/native_poc/src/host_frame_gate.hpp` | 제품/공유 / 검색 | 104 | 0/0/0/1/0 | `f9df5cf9c7ad752063a6c780dbf9c3902d98563ee00ebbac144e070b57a6bc3d` |
| `apps/native_poc/src/host_frame_gate_test.cpp` | 테스트 / 검색 | 143 | 0/0/0/1/0 | `063ccafe04061ca3973c27269d9c4d5b6c3ce83be07902606c5ad13e5e6e5c23` |
| `apps/native_poc/src/host_frame_state.hpp` | 제품/공유 / 검색 | 58 | 0/0/0/0/0 | `b7dd0120f70a5303af8f2d659663c7e60316c80c26ce2547515aca6ca8a7eddc` |
| `apps/native_poc/src/host_gpu_scaler.hpp` | 제품/공유 / 검색 | 235 | 0/0/2/0/22 | `eb1938f4c7dfdacb2c3f1f9f0841ba57df3b5e582a0bc2ed8e9e4e214392ea44` |
| `apps/native_poc/src/host_input_inject.cpp` | 제품/공유 / 검색 | 786 | 0/0/24/0/13 | `18bd1974c0b81ab5e6ab481537ecfe47ab2dfa88aad97d997dde0449c7559896` |
| `apps/native_poc/src/host_input_inject.hpp` | 제품/공유 / 검색 | 128 | 0/0/5/0/0 | `11ef8e5e91b79dde4c7ae99080863c995e3cf3b688c803c561c951ea4861f6f6` |
| `apps/native_poc/src/host_input_router.hpp` | 제품/공유 / 검색 | 89 | 0/0/24/2/0 | `df0b8b22cd83c8c9975cb5338ef9d3d90d2320b6ebb3cf270af7a4b2d0e6d404` |
| `apps/native_poc/src/host_input_target_rect.hpp` | 제품/공유 / 검색 | 81 | 0/0/0/2/0 | `1f07ca0d6ce7da9956010ee208cb4340cdd79dda3bb9e8ee83d7ca9aa345e3d6` |
| `apps/native_poc/src/host_input_target_rect_test.cpp` | 테스트 / 검색 | 228 | 0/0/0/10/1 | `748f2e3564cb3557a4f1e9d755944d689fa64698e061fb55f9e322aa3445a52c` |
| `apps/native_poc/src/host_kick.hpp` | 제품/공유 / 검색 | 104 | 0/0/0/1/0 | `afd9e7f9406c22a14e7c0ae21ffaf9440edd1209112eba2e9e4d19afbe1a90bd` |
| `apps/native_poc/src/host_kick_test.cpp` | 테스트 / 검색 | 130 | 0/0/0/0/0 | `b5ca64f8871af527fdce79ca8c8bf35cfd20aacb38e0434058d6743e06f1d338` |
| `apps/native_poc/src/host_log.hpp` | 제품/공유 / 검색 | 138 | 0/0/3/1/0 | `c1902a903dc25a7d6597aa2dbd7d0c4852c80c680ed0aa74dae4351036c2f016` |
| `apps/native_poc/src/host_loop_helpers.cpp` | 제품/공유 / 분기 추적 | 537 | 0/0/10/19/13 | `75028ab547cc16bd3a4e108e712edbaa7e82ea7d1f1b2b03bfb26adf2df963ba` |
| `apps/native_poc/src/host_main_loop.hpp` | 제품/공유 / 분기 추적 | 233 | 0/0/1/19/1 | `9eccd15c1a8322b7acb6ea7c336eb9b9f98917ed2fe2970f6368f72fa928b924` |
| `apps/native_poc/src/host_main_loop_mailbox.hpp` | 제품/공유 / 검색 | 203 | 0/0/11/0/0 | `a18adfd5e5c4bf1f526e51ba04ba3d01e89ae201f6a1502199cd44dc82d535d0` |
| `apps/native_poc/src/host_main_loop_mailbox_test.cpp` | 테스트 / 검색 | 197 | 2/0/0/0/0 | `3ef0ec043502106d9acf0edeb330b9f7312e10a5706ec671b0028c8d6b120956` |
| `apps/native_poc/src/host_net_io.cpp` | 제품/공유 / 검색 | 279 | 0/1/2/0/2 | `850c38b77f06e16eccb3ad949c2b3ea9f08bf98e3921a3bce416fa76d6fdf6d5` |
| `apps/native_poc/src/host_net_io.hpp` | 제품/공유 / 검색 | 115 | 0/0/2/0/0 | `92c74759546f96eb9360eea23e1efa1635ab7db410781ccb505fdcc98060e788` |
| `apps/native_poc/src/host_secure_target_rect.hpp` | 제품/공유 / 검색 | 91 | 0/0/0/0/0 | `3c26e0bf13f37a2ce83be6fca22eeac5e8d561bde4aba1465433bab8cf4f2434` |
| `apps/native_poc/src/host_sender_epoch_test.cpp` | 테스트 / 검색 | 354 | 1/2/12/0/4 | `bf0b64860359515dea75407bcb010daf96895f4d709aa269643911f4f27ad086` |
| `apps/native_poc/src/host_sender_queue_policy.hpp` | 제품/공유 / 검색 | 49 | 0/0/0/0/0 | `26ab717ab2748cc82ae4eacac37d0ef70d0b6cba20ee3e00f24c2bd438d28be1` |
| `apps/native_poc/src/host_sender_queue_policy_test.cpp` | 테스트 / 검색 | 185 | 0/0/0/0/0 | `39a6213756a4c8ed89c67ba522615ef6bd98b4364235f5670ea6aca3a6f7945f` |
| `apps/native_poc/src/host_session.hpp` | 제품/공유 / 분기 추적 | 148 | 3/1/7/1/0 | `95281bef5e54cd38f7f48b3b093a9b3c5322bd2b03a9dce14f1fd903cf35b84c` |
| `apps/native_poc/src/host_shutdown.cpp` | 제품/공유 / 분기 추적 | 163 | 4/0/1/1/0 | `580ccdd562ae289e2acb29a06e4abebbb7370f3fdfc7bf3eabb90eb4b89cb4d3` |
| `apps/native_poc/src/host_stage_backend.cpp` | 제품/공유 / 분기 추적 | 285 | 0/0/0/24/0 | `74d850d3e9944b4292d75d2b1c2630c52837bd455b3432e7c281e97ee9dcce34` |
| `apps/native_poc/src/host_stage_encode_send.cpp` | 제품/공유 / 검색 | 297 | 0/0/0/2/0 | `a066e668d6638eca229f92df09075e9d69c04d01f24138f4abe149a720ad9a6b` |
| `apps/native_poc/src/host_stage_encode_send_h264.cpp` | 제품/공유 / 분기 추적 | 554 | 0/0/2/21/0 | `8c8f768398bac4516abe3ae880552912d885d55d0d63e4587fb8380fa2b244e5` |
| `apps/native_poc/src/host_stage_encode_send_h264.hpp` | 제품/공유 / 검색 | 60 | 0/0/0/1/0 | `1bf7778f612205356cc1d9d449fdcb2eaf4e28dd9296e80674ec6ef0cde71f71` |
| `apps/native_poc/src/host_stage_encode_send_h264_au.cpp` | 제품/공유 / 분기 추적 | 746 | 0/0/1/9/0 | `a0ddc7cc38e3b74420332f1014e7cf1c89b152deb5c5bcc553b2dd84255d0b6f` |
| `apps/native_poc/src/host_stage_gate_static.cpp` | 제품/공유 / 검색 | 178 | 0/0/0/1/0 | `d2ad55c696635b1bcb2844e6b60237ffbcbec0669a4d747bc639186a88a9fcab` |
| `apps/native_poc/src/host_stage_geometry.cpp` | 제품/공유 / 분기 추적 | 172 | 0/0/2/13/0 | `e6c3bddd3ff9a0e3468f2d1f90b73b51b6be231fd46215a7643c18035e0722c2` |
| `apps/native_poc/src/host_stage_pace.cpp` | 제품/공유 / 검색 | 111 | 0/0/0/1/0 | `def6218d582b1753f7814b01764de5e9e0f9670ffc01152a2567d148c8cb7c88` |
| `apps/native_poc/src/host_stage_pop_frame.cpp` | 제품/공유 / 분기 추적 | 363 | 0/1/4/2/0 | `51c65a8971dcc54f9148c7d5f2fa6babe4a9b3b32541956e30d102a4b13a3c37` |
| `apps/native_poc/src/host_stage_runtime_tune.cpp` | 제품/공유 / 검색 | 245 | 0/0/2/5/0 | `5f7daac5fc49fa0339f24d60cf412e7a4cb045f053d3ba2c49b716ab4a68299d` |
| `apps/native_poc/src/host_stage_selection.cpp` | 제품/공유 / 분기 추적 | 327 | 0/0/5/19/0 | `4dcaa8842173edb8d3abe603d236e14cb46ddc2f0387992c1e6c488135efcdc4` |
| `apps/native_poc/src/host_stage_stats.cpp` | 제품/공유 / 분기 추적 | 472 | 0/0/1/79/1 | `7a67bddb4bce5a09aee66f144fde8ae575e683521ca3a4bbfd00938fc701cb7d` |
| `apps/native_poc/src/host_stage_stats_h264.cpp` | 제품/공유 / 검색 | 444 | 0/0/1/13/0 | `611dacaa320f2612ad5c0dbd8487adfdaa95fd3c9eac49630478eadbcfa6d493` |
| `apps/native_poc/src/host_stage_stream_active.cpp` | 제품/공유 / 분기 추적 | 180 | 0/0/0/13/0 | `9116e5349b955e07eebacbb3c084a80a3e7f7e9083b3a20f3975b96e4013a5e2` |
| `apps/native_poc/src/host_stage_time_limit.cpp` | 제품/공유 / 분기 추적 | 154 | 0/0/0/3/0 | `f47c07ffe12c00d1795a484c1b4ad55f3b5e20ce86ef3a575bb10322a2dd1343` |
| `apps/native_poc/src/host_stage_watchdogs.cpp` | 제품/공유 / 분기 추적 | 231 | 0/0/0/62/1 | `e96b5d8535145cf64632aa84c4b41a3694f017344723264c0cf6fa962a72963f` |
| `apps/native_poc/src/host_startup.hpp` | 제품/공유 / 분기 추적 | 57 | 2/0/2/9/0 | `1f48920ac1346fc8bc7684ce336c104e71cd75dd156f43c5667fec928d40964a` |
| `apps/native_poc/src/host_startup_capture.cpp` | 제품/공유 / 분기 추적 | 409 | 4/2/1/46/4 | `e70202c3f59d3300d91024ff38cf9a60efee2fff89c7c2ddb0f87da031206010` |
| `apps/native_poc/src/host_startup_config.cpp` | 제품/공유 / 검색 | 416 | 0/0/0/10/0 | `98a240e5eca762f106796d7669c5d5a84765e005644ec25c4defec2227f74243` |
| `apps/native_poc/src/host_startup_connect.cpp` | 제품/공유 / 분기 추적 | 399 | 0/0/1/1/0 | `2551eb92fd100d46dfd1da9379c9fcc9fa7c1dd8684c4796d4fc18c560c70c5e` |
| `apps/native_poc/src/host_startup_control.cpp` | 제품/공유 / 분기 추적 | 434 | 3/2/5/5/1 | `3e6900ff30af743b33964678d2c0c8562f7d4cd2ce4124c3e6926ebeecaa2e6e` |
| `apps/native_poc/src/host_startup_graphics.cpp` | 제품/공유 / 분기 추적 | 430 | 0/0/1/16/2 | `257815a3d38c2a7b32ff27be4b5cf5208afc1e720f549374f032fd7b03faf6dc` |
| `apps/native_poc/src/host_stats.hpp` | 제품/공유 / 검색 | 103 | 0/0/5/2/0 | `6ab9b7d58bee3d35ff816d68ddc37ff6df32d0c7eae2ed3a9b7edf0882eace06` |
| `apps/native_poc/src/host_string_util.hpp` | 제품/공유 / 검색 | 7 | 0/0/0/0/0 | `676223cdd09e8fe6acf53f240f314b548adfc618e8beeb73f6295b4b0fd8cc54` |
| `apps/native_poc/src/host_unlock_relay.cpp` | 제품/공유 / 검색 | 253 | 2/6/8/0/4 | `8fbc59b051767250a296699db6c422e2797d023030376d848d28e7c8224f5c2d` |
| `apps/native_poc/src/host_unlock_relay.hpp` | 제품/공유 / 검색 | 67 | 1/0/2/0/0 | `ce4a53e0b0a5d22799d61b87d2031d1701f99a04e33b81dfb2b92f3057b1c03a` |
| `apps/native_poc/src/host_watchdog.hpp` | 제품/공유 / 분기 추적 | 126 | 2/1/7/29/0 | `61c40a62dfc948b38a853fc70d6042aea26442a13277dc44ce985fd7662883cb` |
| `apps/native_poc/src/host_window_enum.cpp` | 제품/공유 / 검색 | 340 | 0/0/0/1/28 | `295d11950d45d975df8aab3b82cbde3cf483391bccffc5e64b195183ab3568e8` |
| `apps/native_poc/src/host_window_enum.hpp` | 제품/공유 / 검색 | 91 | 0/0/0/0/0 | `f26b84a97d259ebe67b5415284925b3cde1a80e51000eeb15a52398ec9630a19` |
| `apps/native_poc/src/input_macro.cpp` | 제품/공유 / 검색 | 311 | 0/0/18/0/13 | `d6935a8994a912c4a233af62aaad02812c72f6f92b7bc013c94afed57223ca0d` |
| `apps/native_poc/src/input_macro.hpp` | 제품/공유 / 검색 | 149 | 0/0/0/0/0 | `8f0a0d4d4317c29fc17ac1143a30631ea837bfae54002ea14c62444f03103191` |
| `apps/native_poc/src/input_macro_test.cpp` | 테스트 / 검색 | 318 | 0/0/0/0/0 | `1c5dc40d5181a322dcc23b62679ab01f348149e09e235bdba9fbc19b9173768c` |
| `apps/native_poc/src/install_registration.cpp` | 제품/공유 / 검색 | 223 | 0/0/0/0/3 | `b8a7df75d3e44af2cd01c60514b0bf702b23ccca0cef4811f914431207bd35e4` |
| `apps/native_poc/src/install_registration.hpp` | 제품/공유 / 검색 | 142 | 0/0/0/0/0 | `305a5c0329321fb378964b0895ce2452c3d679b1d37cff180b531ed81cd4cbf2` |
| `apps/native_poc/src/install_registration_test.cpp` | 테스트 / 검색 | 291 | 0/0/0/0/2 | `39dd3ae2748af092a76bf0832bbab6190e312e600b87abf7f2a20ab92cd6a673` |
| `apps/native_poc/src/json_profile.hpp` | 제품/공유 / 검색 | 211 | 0/0/0/0/8 | `b8c245e3631f48c0bfe592ab5b5062159af10846810c8616b2c27563733b7c5b` |
| `apps/native_poc/src/log_upload.cpp` | 제품/공유 / 분기 추적 | 669 | 4/1/14/6/7 | `d92114f1fc694501c2c9bd72ff485a9eee034147d43c9300536ba47a969be635` |
| `apps/native_poc/src/log_upload.hpp` | 제품/공유 / 분기 추적 | 155 | 1/0/0/10/0 | `d1716792eecfcb0b605640ab913e932df6b130228148a0e9e219ec515a2623fd` |
| `apps/native_poc/src/log_upload_shutdown_test.cpp` | 테스트 / 검색 | 401 | 7/1/3/2/6 | `da1f4c4917d7d585c9a00dfa362d617db98ecd293730072e7a6bff8ef66833d5` |
| `apps/native_poc/src/log_upload_test.cpp` | 테스트 / 검색 | 660 | 3/0/8/10/3 | `bb724b6c0e1a8e4bd11996971b7595d5ca434b0f2c164bb2f41e08451bcee877` |
| `apps/native_poc/src/macro_shell_bridge.cpp` | 제품/공유 / 검색 | 162 | 0/0/0/0/12 | `64c90b63d02b360e4ed2c36b940eef8e0442efa519b7fb58f49216602f4bccfe` |
| `apps/native_poc/src/macro_shell_bridge.hpp` | 제품/공유 / 검색 | 63 | 0/0/0/0/0 | `1480efd4d98c72b2e8999e1969e61736d3aa66a71c70ba244456030c690c64cd` |
| `apps/native_poc/src/macro_shell_bridge_test.cpp` | 테스트 / 검색 | 121 | 0/0/0/0/0 | `da670ac6238f1ef0922526c5fd647b20aa14b78c45a3c16abdf0702e29adbd2e` |
| `apps/native_poc/src/mf_h264_codec.cpp` | 제품/공유 / 검색 | 2511 | 2/0/0/7/181 | `b3b2432d4247b2d6204234e6061c742bc5827c00f9fa0643c92fd76eedf4810d` |
| `apps/native_poc/src/mf_h264_codec.hpp` | 제품/공유 / 검색 | 301 | 0/0/0/2/1 | `1bb8cf2a54a3f458875f139835069e54eadb2d5be8510606cc7e1ed1610ecf06` |
| `apps/native_poc/src/mf_h264_codec_test.cpp` | 테스트 / 검색 | 92 | 0/0/0/0/2 | `8b944b3b82c64fef0f0d3eec6d1412019967e06ecd0f7ff9cb65be858b5cc4de` |
| `apps/native_poc/src/native_client_main.cpp` | 제품/공유 / 검색 | 175 | 0/0/0/0/2 | `25c31701a8bc9f0fc075984aa3a7ac23c9f69a548f883e194881c526e363f508` |
| `apps/native_poc/src/native_host_main.cpp` | 제품/공유 / 검색 | 241 | 0/0/0/0/3 | `b1fde30071b208c59ca97f59373eacf9b98a606ba37833e906cf8febd164a669` |
| `apps/native_poc/src/native_socket.hpp` | 제품/공유 / 검색 | 192 | 0/3/0/1/6 | `376e116edad7a4dfba2b247c75507147049daa117d7c32ba0683641cae1b031f` |
| `apps/native_poc/src/native_video_client_main.cpp` | 제품/공유 / 검색 | 75 | 0/0/0/2/0 | `a53f46b8eb899b9c3dcbe0ad702af17c341f969af09e3f27f0f46660a6ea991d` |
| `apps/native_poc/src/native_video_client_session.cpp` | 제품/공유 / 검색 | 857 | 4/0/38/9/34 | `e2bff88598496ee2f816f1827744de55dbe0568c0e8c49a6be02d5491bffb25d` |
| `apps/native_poc/src/native_video_client_session.hpp` | 제품/공유 / 검색 | 189 | 2/0/6/1/2 | `f5fc0b5d880f9736cdfc2c646c368c393ec1af709c3f4ea0a7d72c886c2de54d` |
| `apps/native_poc/src/native_video_client_shared_core.cpp` | 제품/공유 / 분기 추적 | 1370 | 0/0/23/0/30 | `19d01e83eaf8f48750e58ad9d7211c6af59d6e4eb26096eae28fb6338a2c9a62` |
| `apps/native_poc/src/native_video_client_shared_core.hpp` | 제품/공유 / 분기 추적 | 572 | 0/0/29/2/0 | `26068d8181f25f5fca42fb426b133774a178a2ff128937f97e7e6c5c05b8b0e8` |
| `apps/native_poc/src/native_video_client_shared_core_test.cpp` | 테스트 / 검색 | 1412 | 6/5/2/4/203 | `2d2f6dcecc86960e90d33ec0551c33a598fad929a9f4cdbc38ced97910311fde` |
| `apps/native_poc/src/native_video_client_tcp_control.cpp` | 제품/공유 / 검색 | 206 | 0/0/1/4/11 | `1fe9703f7bb006440a47072354ea3237650222ad5f684c83e40b751931cd8239` |
| `apps/native_poc/src/native_video_client_tcp_control.hpp` | 제품/공유 / 검색 | 81 | 0/0/1/1/0 | `88a1af8bfdcf6d0eaff871f7634ec6fc5a5384ffb4fb5e3c2609671077526cf3` |
| `apps/native_poc/src/native_video_host_main.cpp` | 제품/공유 / 분기 추적 | 283 | 1/0/2/21/1 | `5945b3ba78d20b2811e3ce7aec9bc2d46c4544bf4800c02529c8d5d580611b05` |
| `apps/native_poc/src/native_video_transport.hpp` | 제품/공유 / 검색 | 35 | 0/0/0/0/2 | `92172970793ce13f032ea28052ff37fac200369632ea2045d73348003e97d302` |
| `apps/native_poc/src/observe_vectors_test.cpp` | 테스트 / 검색 | 118 | 0/0/0/0/0 | `c52feb54c12f948e0accc0837fe1c31629f9433671b0bc3803f2aa57b19db522` |
| `apps/native_poc/src/payload_name.cpp` | 제품/공유 / 검색 | 190 | 0/0/0/0/2 | `3d81ef1c25fd44f1e217db8651988ecb7371ed15e412af38cde93cf5b09cedfe` |
| `apps/native_poc/src/payload_name.hpp` | 제품/공유 / 검색 | 87 | 0/0/0/0/0 | `2e047bc9412a08aad8a66ff667ab98beb978d93b9ad4f0f97e5b8361d5cd22a4` |
| `apps/native_poc/src/payload_name_test.cpp` | 테스트 / 검색 | 170 | 0/0/0/0/0 | `bdc22c5ac49b029afdaa25d6fcc1d3087e28227f2a45ff59e70fe6d23192fb24` |
| `apps/native_poc/src/picker_empty_state_test.cpp` | 테스트 / 검색 | 154 | 0/0/0/1/0 | `10358f13b5c0170f690a5eb9cfd22b4c527ac50d3ef750c3a6bfbed4f02fc2ba` |
| `apps/native_poc/src/picker_open_chain_test.cpp` | 테스트 / 검색 | 274 | 0/0/6/0/1 | `b2d4b0b3f18932938f8626161b2fa0b7b6ea7ae16a23718b83fc2c230a5acdaa` |
| `apps/native_poc/src/poc_protocol.hpp` | 제품/공유 / 검색 | 712 | 0/0/0/1/0 | `c691102a1610ff57eae9f6da3ce85a6e1ee2c182ffb06b402567ca71a3dee8f9` |
| `apps/native_poc/src/product_version.hpp` | 제품/공유 / 검색 | 14 | 0/0/0/0/0 | `d4289cb7df6eb0923476a67986c926a72a46566d363144f4d61e4d8a83413936` |
| `apps/native_poc/src/punch_any_test.cpp` | 테스트 / 검색 | 270 | 6/0/2/0/4 | `44b0c0180fb750d3c3ebaed8df596c580f8465caebff56b2f9cac71bbbe79056` |
| `apps/native_poc/src/scn_dummy_main.cpp` | 제품/공유 / 검색 | 76 | 0/1/0/0/0 | `41ed7e140a7dab519c6cc5786444930af16a6ddc61e9cdac18ca579216148966` |
| `apps/native_poc/src/sealed_unlock.cpp` | 제품/공유 / 검색 | 428 | 0/0/0/0/28 | `a7b69bebef50eec3b1ce67dd1f877dfb6a86b1bfca316dcfd03e259ba493202b` |
| `apps/native_poc/src/sealed_unlock.hpp` | 제품/공유 / 검색 | 175 | 0/0/0/0/0 | `cce195ed23720893683b7043b94603772703703509baf50402c39eb5f748ba97` |
| `apps/native_poc/src/sealed_unlock_test.cpp` | 테스트 / 검색 | 254 | 0/0/0/0/0 | `525ed29c5f3bee9af3340b01825eb142eeee171f7f10ed1e533ef982ae963b52` |
| `apps/native_poc/src/secure_input_broker.cpp` | 제품/공유 / 분기 추적 | 268 | 0/2/9/1/13 | `9c830e0ad64b7131d0aeda4b10d562cff0ed695b96ecebb83ccae95b93315cb3` |
| `apps/native_poc/src/secure_input_broker.hpp` | 제품/공유 / 검색 | 62 | 0/0/0/0/0 | `a7750004cdb007f766e7fd47d246698c200c2c0c97f5feea656678e65328a27b` |
| `apps/native_poc/src/secure_input_diag_budget.hpp` | 제품/공유 / 검색 | 48 | 0/0/0/0/1 | `a197af8c94348e836eb0792e9f3038d01a6a8d037cf630202e5461c2f1e8c0a2` |
| `apps/native_poc/src/secure_input_mapping.hpp` | 제품/공유 / 검색 | 62 | 0/0/0/0/0 | `3f391e55aa880269b8565f8f7fe818d8eef5d7ca1c0a41dfd87ac54b364374b0` |
| `apps/native_poc/src/secure_input_mapping_test.cpp` | 테스트 / 검색 | 164 | 0/0/0/0/0 | `7df27ac66d23ea4cbfaae38360192b0b102efb43aa57815489d50c429f1f58c6` |
| `apps/native_poc/src/secure_input_protocol.hpp` | 제품/공유 / 검색 | 44 | 0/0/0/0/0 | `c076c29cf5765ad42a114bcb8e4f54a3b65061bc08c820f712e398a002045b73` |
| `apps/native_poc/src/secure_input_service_main.cpp` | 제품/공유 / 검색 | 1228 | 2/7/9/9/19 | `17874fd4f71bbee21f79cdeb315e2b1c57113a600e6326c593d9b4789c39c39e` |
| `apps/native_poc/src/secure_input_session.hpp` | 제품/공유 / 검색 | 84 | 0/0/0/0/0 | `ca00a58df5f6d356409979314aab73c62f82029d8cf7249ebd2dd3010b0ea9b5` |
| `apps/native_poc/src/secure_input_session_test.cpp` | 테스트 / 검색 | 110 | 0/0/0/1/0 | `ea2487bb0d837db99f04458cb9315aae5c68ccfe152bca9298b0e97a326e18f5` |
| `apps/native_poc/src/secure_unlock_ipc.hpp` | 제품/공유 / 검색 | 99 | 0/0/0/0/0 | `990ddb8066ce36cdba9dc3c4c668d482a876279cc727583ca469890af1506a9f` |
| `apps/native_poc/src/session_toolbar_click_test.cpp` | 테스트 / 검색 | 346 | 0/0/14/0/1 | `d25d37756707ccff9ef5e4675767f764da831bf44898a5beefb0485eca4cced5` |
| `apps/native_poc/src/string_util.hpp` | 제품/공유 / 검색 | 101 | 0/0/0/0/0 | `af6790b1dd1e5734592cfeb70388aa8e9cf3bb6c2227cdc0a760fa58b0310850` |
| `apps/native_poc/src/time_utils.hpp` | 제품/공유 / 검색 | 24 | 0/0/0/0/0 | `974890ac7efe507c16a03f392ae801f07be1a3160fcf3c0872a17f81fe6c042b` |
| `apps/native_poc/src/udp_control_channel.cpp` | 제품/공유 / 분기 추적 | 363 | 0/2/7/0/18 | `a563db47f050f25abe0059f667faecbc4e28b8e90c6bc6faa0cce5785d87bed1` |
| `apps/native_poc/src/udp_control_channel.hpp` | 제품/공유 / 검색 | 181 | 0/0/2/0/0 | `307a9f3536120bb13907ca6a880d1ba03328a7682f476a27e592d391c823cccf` |
| `apps/native_poc/src/udp_control_channel_test.cpp` | 테스트 / 검색 | 248 | 5/0/7/0/0 | `4abdef598d972795a9e09f6b5d62f2b017c6dfb5b7a016ba99239be2ac5a2be8` |
| `apps/native_poc/src/udp_control_e2e_test.cpp` | 테스트 / 검색 | 191 | 0/0/4/0/0 | `3b890933698e2b38208f7aa888f082e28190dec6232c8064c2b04ac3526abfa9` |
| `apps/native_poc/src/udp_fec_interleave_test.cpp` | 테스트 / 검색 | 202 | 0/0/0/0/0 | `88edd957d864f7c96f8eb151260ff4809d19023d86dd68a3a4b37e32a83b65fc` |
| `apps/native_poc/src/udp_video_nack.hpp` | 제품/공유 / 검색 | 163 | 0/0/0/2/6 | `6a22f47f3bed04eb1f9e17ef2aec2c81269863a890dd6d33d6b88f28ff61356f` |
| `apps/native_poc/src/unlock_protocol_test.cpp` | 테스트 / 검색 | 142 | 0/0/0/0/0 | `a07242397eab49e3d8133120c79675cb02d2ad99d7b3fd168a28f98d917f23e1` |
| `apps/native_poc/src/unlock_wire.hpp` | 제품/공유 / 검색 | 61 | 0/0/0/0/0 | `6f32a134b8c527fd0af4e00278bbbe411844cb1dd0f86a50dac2f901616e6722` |
| `apps/native_poc/src/update_check.cpp` | 제품/공유 / 분기 추적 | 165 | 2/0/0/0/3 | `6b80edd6f7db93e46db19f654d00ccc5b76a2d6d079652a4555e7641dd8fc086` |
| `apps/native_poc/src/update_check.hpp` | 제품/공유 / 검색 | 113 | 0/0/0/0/0 | `ecbad4707f80ced01a81ab98f8fadf27d064822b9ca0367a9fc1b8d3af88991e` |
| `apps/native_poc/src/update_check_test.cpp` | 테스트 / 검색 | 242 | 0/2/4/0/2 | `04c37772ac0cab7a1a1c6680b065be853fcfc5569e0fad0361d1519416aa371a` |
| `apps/native_poc/src/update_credential_channel.cpp` | 제품/공유 / 분기 추적 | 376 | 0/6/0/0/29 | `8426d6a86e7b839dc329ab720a1982855c40c539d22bf8cc37e5ef5730ec6145` |
| `apps/native_poc/src/update_credential_channel.hpp` | 제품/공유 / 검색 | 106 | 0/0/0/0/0 | `6f347db8939b1af24908e4ef9cc0c7fb070f1474a4193c807ea9b25fc0d44411` |
| `apps/native_poc/src/update_credential_channel_test.cpp` | 테스트 / 검색 | 208 | 4/2/2/0/0 | `24c719125be7414ee507749a70b45ec5e9075169928d054fedc071987aa043ff` |
| `apps/native_poc/src/update_credential_hop_helper.cpp` | 제품/공유 / 검색 | 223 | 0/1/0/0/1 | `fdb81b4a3ed17450afb25e2598d421121a7df9c63b496f68e77c52780937fd21` |
| `apps/native_poc/src/update_credential_hops_test.cpp` | 테스트 / 검색 | 436 | 0/1/0/0/0 | `6e1f23e5b7942a198252672db162557df229d474bf6457f010cb6658283106fd` |
| `apps/native_poc/src/update_effects.cpp` | 제품/공유 / 분기 추적 | 937 | 0/5/0/1/38 | `42840af26f4502b45749ed3a93cc737a65fdc29c2cd3cb27a38e60e9d5dde277` |
| `apps/native_poc/src/update_effects.hpp` | 제품/공유 / 검색 | 361 | 0/0/0/0/0 | `24d4a719f5ea1c10790b6b1274586aa0b08026dec2a0c9267aa3b525fe0101f5` |
| `apps/native_poc/src/update_effects_test.cpp` | 테스트 / 검색 | 1586 | 2/7/0/0/14 | `6769c22813288d22326e400fbba42c2c5fa72f9631fa738b6ecd588b30f609f3` |
| `apps/native_poc/src/update_endpoint.cpp` | 제품/공유 / 검색 | 147 | 0/0/0/0/11 | `f63f5b50fd94e13631844291047c62fb5e74faa8486369c56d959a20b8f0e6e9` |
| `apps/native_poc/src/update_endpoint.hpp` | 제품/공유 / 검색 | 113 | 0/0/0/0/0 | `9625ceff1c2f515b1f46ad070b1ad6825cbea0bb4b641c08601f4dceaf118797` |
| `apps/native_poc/src/update_handoff.cpp` | 제품/공유 / 검색 | 193 | 0/0/0/0/2 | `55959da7b1217960f660987b48dbb42a909714d89e8ae712d06a1146286070fc` |
| `apps/native_poc/src/update_handoff.hpp` | 제품/공유 / 검색 | 262 | 0/0/0/1/0 | `60fe436107674d27bdd797635add5d774210c371778e845a2f1dbd2b266775dd` |
| `apps/native_poc/src/update_handoff_process_test.cpp` | 테스트 / 검색 | 454 | 0/1/0/0/0 | `e750a7af5f12c7f2b759fa15a23ccb10914d52161b893da5beb16ad6243d9064` |
| `apps/native_poc/src/update_handoff_test.cpp` | 테스트 / 검색 | 318 | 0/0/0/0/1 | `764c1e37bc6c1fc27960ad44a92f67c72a563a96998bf40ab71fe8b36e67d00e` |
| `apps/native_poc/src/update_handoff_wait.cpp` | 제품/공유 / 검색 | 165 | 0/0/0/1/0 | `6985e8c1292eda23b586fe225b417294417d3fb7d59f155cd234774b092c9216` |
| `apps/native_poc/src/update_health.cpp` | 제품/공유 / 검색 | 137 | 0/0/0/0/1 | `bf0c992f30ef1dbae9e56d5067bf72228de494581ea8142c0e5d890d17a5552a` |
| `apps/native_poc/src/update_health.hpp` | 제품/공유 / 검색 | 88 | 0/0/0/0/0 | `6ef76e78a4dd24598ebba0cc32c7ed62a24ece92d80ed6ab3dd73623ce64339d` |
| `apps/native_poc/src/update_health_log.cpp` | 제품/공유 / 검색 | 44 | 0/1/0/0/0 | `5afd8406918f1ed7c06e15bde6a136ccb355064be246fc11a5642d6ca0b74d45` |
| `apps/native_poc/src/update_health_log.hpp` | 제품/공유 / 검색 | 40 | 0/0/0/0/0 | `0eac9a3102e6fb85467987b1bbf5e3569f76655dc4c2a219a004c185d0000bf0` |
| `apps/native_poc/src/update_http.cpp` | 제품/공유 / 검색 | 308 | 0/1/0/0/11 | `dddf2a04493d74b405fa3732925baefc717be8050a59e94a6a66a6ab4fb8e106` |
| `apps/native_poc/src/update_http.hpp` | 제품/공유 / 검색 | 99 | 0/0/0/0/0 | `fc2da8366b46a71f54c4af2cf4339e2bcc82be11b7d7a97702321e4256bab683` |
| `apps/native_poc/src/update_http_test.cpp` | 테스트 / 검색 | 213 | 3/0/2/0/4 | `cae60f9e4642e15ea6625daf104251fea2611b1d25a2b29274f1b38f7ec550b2` |
| `apps/native_poc/src/update_job_guard.cpp` | 제품/공유 / 검색 | 167 | 0/0/0/0/2 | `34dd1dacb46aad6aea6e51bed2184b930df2a581f8984913a2ee38f63a1076b2` |
| `apps/native_poc/src/update_job_guard.hpp` | 제품/공유 / 검색 | 68 | 0/0/0/0/0 | `ef6fb99efd82e7f6593b461c005858abffc79762421e038c41dcf5a579a5cdd6` |
| `apps/native_poc/src/update_job_guard_test.cpp` | 테스트 / 검색 | 222 | 0/0/0/0/2 | `94345c436d5787e9677dcf3b15260fabaac08b750cf9d219e47ca7c4d3de482c` |
| `apps/native_poc/src/update_manifest.cpp` | 제품/공유 / 검색 | 323 | 0/0/0/0/14 | `b976209ceb14055a1dba836648d5ef4ea1c89063521e49df59c15b2ea610f73d` |
| `apps/native_poc/src/update_manifest.hpp` | 제품/공유 / 검색 | 169 | 0/0/0/0/0 | `64c1cf0a8e64fc2f4d5a40680c4ea461d392dfdb5def8bf2a0c1b33d41e11005` |
| `apps/native_poc/src/update_manifest_test.cpp` | 테스트 / 검색 | 415 | 0/0/0/0/2 | `2d28e70d6ae8c3b8751751bcdef6527d3fb2edc7b2bc1e76aa095055a4d81f5a` |
| `apps/native_poc/src/update_process_identity.cpp` | 제품/공유 / 검색 | 65 | 0/0/0/0/6 | `12a00e129384c497621bc7332c55b958e74d6bce86fc8ee455eba468888b72f2` |
| `apps/native_poc/src/update_process_targets.cpp` | 제품/공유 / 검색 | 205 | 0/0/1/0/5 | `e89ec0d05bda815c84af54d3cdf8db0fb2af0dfaf95d713a6dbf12fc5009e079` |
| `apps/native_poc/src/update_process_targets.hpp` | 제품/공유 / 검색 | 67 | 0/0/0/0/0 | `ffdcd5b344ac356d422233bdfa771531ec344e44d5b50c921ecf35c82db30ed1` |
| `apps/native_poc/src/update_registration_com_test.cpp` | 테스트 / 검색 | 160 | 6/0/0/0/3 | `9dd8b214cfd93dd898c8d977b052f2fdc1f24df6aad6a8036cc1f36fec062663` |
| `apps/native_poc/src/update_registration_wiring.cpp` | 제품/공유 / 검색 | 197 | 0/1/0/0/3 | `d653b45bdcf9275eef72c728fb6eed3bae3e418108273cbd0a71e937e19daf67` |
| `apps/native_poc/src/update_registration_wiring.hpp` | 제품/공유 / 검색 | 60 | 0/0/0/0/0 | `22afb3f902aa665609317793c7f7a6e98f793aa65292903983fda63267e86bff` |
| `apps/native_poc/src/update_relaunch.cpp` | 제품/공유 / 검색 | 658 | 0/3/0/0/30 | `5f03c5cb636d46da54376f1f7870b43f1c203fa26610b99d51e579866efe9ddf` |
| `apps/native_poc/src/update_relaunch.hpp` | 제품/공유 / 검색 | 295 | 0/0/0/1/1 | `82bd8fab8b60cee1e6cd02bbafb7d9a5db76bb6e411a9e5c25659efa866fe34e` |
| `apps/native_poc/src/update_relaunch_plan.cpp` | 제품/공유 / 검색 | 106 | 0/0/0/0/0 | `53b6797cbc494fb8a530472bee49bc3b4fa73d4d1098bebdae1e53377f2ffd7c` |
| `apps/native_poc/src/update_relaunch_plan.hpp` | 제품/공유 / 검색 | 117 | 0/0/0/1/0 | `ce50fb26c1c27b2b64f9c98e796ab2e6c54509da06809bb326a03f9568a87b44` |
| `apps/native_poc/src/update_relaunch_test.cpp` | 테스트 / 검색 | 1006 | 0/1/0/1/7 | `0b5f9c4d3b93b821fff1e23851dc33898c899548c5e14aabc4f6d37e9a8077bb` |
| `apps/native_poc/src/update_release_test.cpp` | 테스트 / 검색 | 909 | 0/2/0/0/6 | `cb9ff22442643ddd70931f5e3453100baa4e0c82a6fe3142e6e321dcc25f10ba` |
| `apps/native_poc/src/update_signature.cpp` | 제품/공유 / 검색 | 98 | 0/0/0/0/9 | `f9344131855de39ea5d8d3f6a5f4b05664ffff9bf5bbeecdf1723238c867f635` |
| `apps/native_poc/src/update_signature.hpp` | 제품/공유 / 검색 | 44 | 0/0/0/0/0 | `d869c1878afef2d8852a4977248ed5971af1b15674a0d660392eb3035db09c2c` |
| `apps/native_poc/src/update_state_machine.cpp` | 제품/공유 / 분기 추적 | 273 | 0/0/0/1/0 | `5d009cd19f94dc48ea269428aedade9d3f6b90e20390a9a75fb341f92273ffc5` |
| `apps/native_poc/src/update_state_machine.hpp` | 제품/공유 / 검색 | 266 | 0/0/0/1/0 | `3f7c5dc27b16efb890f35f744384df47615949ba8ebc6a2f385bdaf0d80b5595` |
| `apps/native_poc/src/update_state_machine_test.cpp` | 테스트 / 검색 | 679 | 0/0/0/1/3 | `6d8c041a94aa39bdc1db6572401a1df450692f9e46262592588bd1b78a90b10c` |
| `apps/native_poc/src/update_stop_process_test.cpp` | 테스트 / 검색 | 518 | 0/6/0/1/5 | `139c4aeee63c146a8dbd17343359a871cad484a65c5d87c023e5c611a20c7498` |
| `apps/native_poc/src/updater_assembly_test.cpp` | 테스트 / 검색 | 586 | 0/0/0/0/7 | `a2890e7395f35e7c7f369ab2580329b21c32f37a3ead2d20ae641fd0c55e2135` |
| `apps/native_poc/src/updater_effects.cpp` | 제품/공유 / 검색 | 629 | 0/1/0/0/13 | `77344d56c7e603e50351ad0696394b2e83b674139217bc575a5c6ed29f6f72d3` |
| `apps/native_poc/src/updater_effects.hpp` | 제품/공유 / 검색 | 233 | 0/0/0/0/0 | `4ca8fd98347e98d8240fd022b5d88cfe6ec08d3c99a228f21c8fdeebae8b93ac` |
| `apps/native_poc/src/updater_main.cpp` | 제품/공유 / 검색 | 363 | 0/0/0/0/7 | `b04810b15aba26bd33b8c815627c0a0ef23571c5e140cd41ecc2561f235ea11d` |
| `apps/native_poc/src/updater_options.cpp` | 제품/공유 / 검색 | 223 | 0/0/0/0/10 | `38fcc824aee88125d1a84aa1d80dfbf5bebb4d85d7f307d782fa369385edf8d4` |
| `apps/native_poc/src/updater_options.hpp` | 제품/공유 / 검색 | 145 | 0/0/0/1/0 | `044986ff48e815e6bf2567b5995967a0ba1691fc8449aedac979ba05246b13e0` |
| `apps/native_poc/src/updater_options_test.cpp` | 테스트 / 검색 | 285 | 0/0/0/0/1 | `48366bc17b2701295c21714d9aea07d3e7395eb48ed829b5d54290c63bc219bd` |
| `apps/native_poc/src/updater_scenarios_test.cpp` | 테스트 / 검색 | 1411 | 0/7/0/6/9 | `c22b34533c21c497612f34b9113847a6bcf6f221c3660c4f03cb99149d02068f` |
| `apps/native_poc/src/url_origin.cpp` | 제품/공유 / 검색 | 70 | 0/0/0/0/0 | `6d848d1e174c8a6e319febb6d5dc6ed5f3ac2b0c0cb5105463b6cc99d65c8341` |
| `apps/native_poc/src/url_origin.hpp` | 제품/공유 / 검색 | 31 | 0/0/0/0/0 | `4cbe53b8d5b010d68dc257fac822e4b8473f1fbef8c2fe0b4d26668339e0c348` |
| `apps/native_poc/src/version_compare.hpp` | 제품/공유 / 검색 | 83 | 0/0/0/0/0 | `510bcaa0ab220e3317441f19b690af7321cac08cf4810232d6b8b76f1c9f1d77` |
| `apps/native_poc/src/version_compare_test.cpp` | 테스트 / 검색 | 104 | 0/0/0/0/0 | `d3730cf1c88704ae4c9037865fce29a7d04a200641e56f3a895ba760d6e1900b` |
| `apps/native_poc/src/video_playout_clock.hpp` | 제품/공유 / 검색 | 201 | 0/0/0/1/0 | `5d54643b5babcb0e7c6708007426ff1f8eb2538e4cdf79924939cd231fc2d6a1` |
| `apps/native_poc/src/video_playout_clock_test.cpp` | 테스트 / 검색 | 281 | 0/0/0/0/2 | `d3544292a0c2496cf3828d9476c92a8659f65e1393fede44f48b40f2bf2cecc9` |
| `apps/native_poc/src/viewer_args.cpp` | 제품/공유 / 검색 | 147 | 0/0/0/0/0 | `09bf8c1550fb3c602c94f535c7893e9f446fe28849a41f2dfe1508ebeaa3e6c7` |
| `apps/native_poc/src/viewer_args.hpp` | 제품/공유 / 검색 | 63 | 0/0/0/0/0 | `3556b7716112b7f673dfe1ffa1bf58ed84faca0b64c64934c22be64652ec7c94` |
| `apps/native_poc/src/viewer_client_metrics.hpp` | 제품/공유 / 검색 | 63 | 0/0/2/0/0 | `881616b70346fef5b6fa8777f9f7bc67693b1f5a31978b286b74d69259a81c56` |
| `apps/native_poc/src/viewer_common.hpp` | 제품/공유 / 검색 | 122 | 0/0/0/0/0 | `fa439dc2bee5a331836750e4c0e8b6ff0d9d95a817fbc0f258b95b8b1f2b2d04` |
| `apps/native_poc/src/viewer_constants.hpp` | 제품/공유 / 분기 추적 | 105 | 0/0/0/4/0 | `d2e0037d3cbdd61a4885f0e5f5392da870c38f86e9bcc20ce74f615065a44dae` |
| `apps/native_poc/src/viewer_context.hpp` | 제품/공유 / 검색 | 51 | 2/0/0/0/0 | `d3a8e68a70b76abe7da1f073258c1496b454e8d5b6beab13697249eebf7ffef7` |
| `apps/native_poc/src/viewer_control_client.cpp` | 제품/공유 / 분기 추적 | 444 | 0/0/5/7/0 | `603c94069c1f82399a47bed9166c887336092ed8a856c24c23f3dd623ed4c8cb` |
| `apps/native_poc/src/viewer_control_client.hpp` | 제품/공유 / 검색 | 55 | 1/0/0/0/0 | `0cf9b16a3e689648a2fcba1bf1791096d2326c60bd635fdbbf2b824de5702946` |
| `apps/native_poc/src/viewer_control_state.hpp` | 제품/공유 / 검색 | 64 | 0/0/4/0/0 | `3adc0e4ffe3c9bbd270c1d69f1cdbb4fa2e0ad052ca04ccabb61a3c3b93effb4` |
| `apps/native_poc/src/viewer_cursor_overlay.cpp` | 제품/공유 / 검색 | 138 | 0/0/0/0/0 | `dc36c4c70cd32dbd31c438147e947cef97e4e1bc0747ab59339a517f2117bff0` |
| `apps/native_poc/src/viewer_cursor_overlay.hpp` | 제품/공유 / 검색 | 30 | 0/0/0/0/0 | `60dd46101832b80e4103445647b9d519d342a627b0479493ef6b89e9f90c51a4` |
| `apps/native_poc/src/viewer_decoder_backend.hpp` | 제품/공유 / 검색 | 23 | 0/0/0/0/0 | `59b0f98648bb408c1deda493b0de9cdb622961b31f197a93d7bcdc944ab2c347` |
| `apps/native_poc/src/viewer_decoder_state.hpp` | 제품/공유 / 검색 | 40 | 0/0/0/0/0 | `608065b5f2670e55ac98e4312d0676970f63add265551e79ab978053cac70d41` |
| `apps/native_poc/src/viewer_env_util.hpp` | 제품/공유 / 검색 | 39 | 0/0/0/0/0 | `b60bb75bca6411a479c1d46250dee5340a9d946ac607b6a35f58c7749e77f106` |
| `apps/native_poc/src/viewer_frame_buffer.hpp` | 제품/공유 / 검색 | 111 | 0/0/13/0/0 | `ffb66e1c8ba2cc7dea45fa76a9f12b83a9bede44538facc8ff183bdbc43ccaff` |
| `apps/native_poc/src/viewer_frame_gate.cpp` | 제품/공유 / 분기 추적 | 599 | 0/0/0/20/0 | `4eeecb55550e19c52aa9804939cc5b00c3a28284cabf5f1381a74f070b3cd51d` |
| `apps/native_poc/src/viewer_frame_gate.hpp` | 제품/공유 / 검색 | 117 | 0/0/0/0/0 | `4ba9b2b6043b638814862ae150e10455a8f1e8b16f79c60c31d3ae82ea7aa9af` |
| `apps/native_poc/src/viewer_frame_gate_state.hpp` | 제품/공유 / 검색 | 157 | 0/0/0/11/0 | `bd00e3a218a3b9bf68338e4c65ce1ddd44dc864d4f48c7ca4efd1ed5075152ff` |
| `apps/native_poc/src/viewer_frame_gate_test.cpp` | 테스트 / 검색 | 1119 | 0/0/0/12/2 | `7f248c0d5226c1418865424b8a011ad916146751ceda995ce5fb3509a02dda47` |
| `apps/native_poc/src/viewer_gdi_util.cpp` | 제품/공유 / 검색 | 125 | 0/0/0/0/0 | `5bf8524fea928497a72f484f3781349478d7e21e4825634d126cbcf1f5746e31` |
| `apps/native_poc/src/viewer_gdi_util.hpp` | 제품/공유 / 검색 | 37 | 0/0/0/0/0 | `c89be7703ae2b42933241cc13427cc0acdd273427ea0d2451d52f672abdd40b9` |
| `apps/native_poc/src/viewer_input_forward.cpp` | 제품/공유 / 검색 | 186 | 0/0/0/0/6 | `8e2a777b03d8d7e2d7789d47ce5412c31b1cc4f9c81720535d6aca760d8d7051` |
| `apps/native_poc/src/viewer_input_forward.hpp` | 제품/공유 / 검색 | 75 | 0/0/0/0/0 | `2a0f54621d0131b6a53e72367624f5eee5add96632689eb0dab8eaef27acd0ec` |
| `apps/native_poc/src/viewer_input_state.hpp` | 제품/공유 / 검색 | 48 | 0/0/10/0/0 | `9dc3bae9f7d762eeadbf87705c18c93bad8fa9fd330ca42e311ecee78db77283` |
| `apps/native_poc/src/viewer_layout.cpp` | 제품/공유 / 분기 추적 | 116 | 0/0/1/0/5 | `5c8338405c57a6501a99a3fba2fdcf9ffd7b8b5b0ef31711f5b4cdfdd3b4ce60` |
| `apps/native_poc/src/viewer_layout.hpp` | 제품/공유 / 검색 | 43 | 0/0/0/0/0 | `9c817db2882201edcc5bd2531cd7d247d73ba5e07314a5e51be0b45ff8001eeb` |
| `apps/native_poc/src/viewer_layout_math.hpp` | 제품/공유 / 검색 | 264 | 0/0/0/0/4 | `3ce6c6d4fb894af8a74861ee290423585ac199bac90764e6f58c132d130db0bc` |
| `apps/native_poc/src/viewer_layout_test.cpp` | 테스트 / 검색 | 197 | 0/0/0/0/0 | `bf4a0e13dfe88230126eb39cb411577ff364c5910bb6abd84753e3b8dc14d8be` |
| `apps/native_poc/src/viewer_liveness_test.cpp` | 테스트 / 분기 추적 | 188 | 2/0/0/2/0 | `44408ce7b9b346927399621cd3754a6dd856bbb38bd2258304bdeff71904ace2` |
| `apps/native_poc/src/viewer_log.cpp` | 제품/공유 / 검색 | 26 | 0/0/1/0/0 | `a0e74824c7acb0aaca3c4d1afedd670441692e47010c6fd2beace8b3d7671c80` |
| `apps/native_poc/src/viewer_log.hpp` | 제품/공유 / 검색 | 25 | 0/0/0/0/0 | `4971db1520ae159dd27a78089425bcca36eee44247f3b2e347c10775f105c525` |
| `apps/native_poc/src/viewer_nv12_renderer.hpp` | 제품/공유 / 분기 추적 | 609 | 0/0/0/0/51 | `97319e3748fbf924669204edbc163f08bdb9a08a51bf906e9a7b45957cddddff` |
| `apps/native_poc/src/viewer_overlay_draw.cpp` | 제품/공유 / 검색 | 316 | 0/0/1/1/0 | `05aa546d4327f9cf51447bd0e7a7cb03199cc8976b912c0f1f4b000407033c07` |
| `apps/native_poc/src/viewer_overlay_draw.hpp` | 제품/공유 / 검색 | 29 | 0/0/0/0/0 | `2f7c904844af4307f945fc6a32f5bc275ac3e25291561c4c2e4877b46ad8ed59` |
| `apps/native_poc/src/viewer_picker.cpp` | 제품/공유 / 검색 | 247 | 0/0/2/2/8 | `e430adbb8d3d5b7bc392c51afd5315c7ed131b49e1077736d13412d0219676ba` |
| `apps/native_poc/src/viewer_picker.hpp` | 제품/공유 / 검색 | 87 | 0/0/0/1/0 | `bec8ce6425dea35a38ac39915e7ce16e5a7355c76156f2003dacaa22059b4f00` |
| `apps/native_poc/src/viewer_picker_empty_line.hpp` | 제품/공유 / 검색 | 51 | 0/0/0/0/0 | `3a1cc5a2d32ae6688ecc8f56ba5abbba0eac5f8c42284c053d54d3369e4077e3` |
| `apps/native_poc/src/viewer_picker_gesture_test.cpp` | 테스트 / 검색 | 118 | 0/0/0/0/0 | `c200556ed2056c4221ce379b56935ecc09ef45120f658bc4f1e702bbf93159b4` |
| `apps/native_poc/src/viewer_picker_state.cpp` | 제품/공유 / 검색 | 43 | 0/0/0/0/0 | `3cc2641955831d33d1502c2630a4dcee28176a31ee6dbe1ca667c0983f908cb3` |
| `apps/native_poc/src/viewer_picker_state.hpp` | 제품/공유 / 검색 | 62 | 0/0/7/0/0 | `1f5c9494a0597a4b9306f1e07060b48ab5add1a0e7639da88883e18a6dd58cd9` |
| `apps/native_poc/src/viewer_present.cpp` | 제품/공유 / 분기 추적 | 498 | 0/0/4/0/6 | `6ffbb7502baee9dd8105d0d42b7c9cff7cfa4a5fec91ec2420aa9916a6ca7875` |
| `apps/native_poc/src/viewer_present.hpp` | 제품/공유 / 검색 | 37 | 0/0/0/0/0 | `3ff3512d216237aa3f4e158a7f52b040b46f8054659770baa178121a65bf9726` |
| `apps/native_poc/src/viewer_present_stats.hpp` | 제품/공유 / 검색 | 59 | 0/0/11/0/0 | `c22795c6514f7961870533041715ae8f7b68f119662bd42a641811b2e125a3b0` |
| `apps/native_poc/src/viewer_recv_liveness.hpp` | 제품/공유 / 분기 추적 | 144 | 0/0/9/9/0 | `7ccbdbdb3065d52c74a81bceafbc5e1dd1367dc372fb5c898aa04a814491992e` |
| `apps/native_poc/src/viewer_recv_stats.hpp` | 제품/공유 / 검색 | 67 | 0/0/0/0/0 | `ab482f66ddbea4ccfbc4f017d5d4ec9ee22b73354ac5a68c76471a1386c296ad` |
| `apps/native_poc/src/viewer_remote_cursor.hpp` | 제품/공유 / 검색 | 53 | 0/0/2/0/0 | `d5ef2d8d4f4e243cef1a0bc9e131c4c341b03afd2fdae15ff961c4619eaa9e28` |
| `apps/native_poc/src/viewer_selection_gate.cpp` | 제품/공유 / 검색 | 106 | 0/0/1/0/1 | `0864972aef92377bd1dfe51087dcce072d5853438622b343b6105dbc6ab4fb87` |
| `apps/native_poc/src/viewer_selection_gate.hpp` | 제품/공유 / 검색 | 82 | 0/0/8/0/0 | `073e9c6d6083f0896fc0106a5dfbb4573286da20109ba8e74c885a4bced5463f` |
| `apps/native_poc/src/viewer_selection_gate_test.cpp` | 테스트 / 검색 | 149 | 0/0/0/0/0 | `0ad6795258635ad9952bacaa7b76e17a98f11de2a741d95e7a77a8bf7e9cc061` |
| `apps/native_poc/src/viewer_session_state.hpp` | 제품/공유 / 분기 추적 | 88 | 0/0/14/2/0 | `c7e47564be98cefafaad73475655523c525167b95efb5df432d55eeecda8cb9c` |
| `apps/native_poc/src/viewer_session_watchdog.cpp` | 제품/공유 / 분기 추적 | 100 | 0/0/1/5/0 | `de9ae770549c4ca1cd74948cd64a01fdef9c5eab034f5e54a12ded499ca3932d` |
| `apps/native_poc/src/viewer_session_watchdog.hpp` | 제품/공유 / 검색 | 23 | 0/0/0/1/0 | `9dc93a91c0f09626c76750e63e86daa31361743b1ce850729a58ea853990200d` |
| `apps/native_poc/src/viewer_shutdown.cpp` | 제품/공유 / 분기 추적 | 85 | 0/0/1/2/1 | `58e32c56b0511da0d7b81669840cf79891f736a7220b4311b5787a3bc310a291` |
| `apps/native_poc/src/viewer_shutdown.hpp` | 제품/공유 / 검색 | 12 | 0/0/0/0/0 | `bd0cd24ed2a0f5cc0fd3e3a5e928870d494131b2507f3a75a07c766d61edc183` |
| `apps/native_poc/src/viewer_startup.cpp` | 제품/공유 / 분기 추적 | 738 | 2/0/0/19/2 | `6e5dcb27c0bfe8fe8b54c83ffbb3c1f455809eab33e6502b216a52e6722b6cf4` |
| `apps/native_poc/src/viewer_startup.hpp` | 제품/공유 / 검색 | 41 | 0/0/0/0/0 | `b9221364a7470488bd503e995774f6bcd6281bf3c941d1bd0cc7c320b04f518a` |
| `apps/native_poc/src/viewer_startup_failure_test.cpp` | 테스트 / 검색 | 141 | 0/4/1/0/3 | `9d2cd262ef26c57b2640ddfa74ec35655befbe501048980df07030e2bf1504cb` |
| `apps/native_poc/src/viewer_state.hpp` | 제품/공유 / 검색 | 48 | 0/0/0/1/0 | `bde59c3769d8949d575ae8da7623f12aa84db47347ca4c634e425c0091350562` |
| `apps/native_poc/src/viewer_thread_join.hpp` | 제품/공유 / 분기 추적 | 32 | 2/1/0/1/1 | `da50b41bbdb3bbc257daafbdc05ac730511d5d90a362b09aa1e63031b7bb2615` |
| `apps/native_poc/src/viewer_udp_recovery_test.cpp` | 테스트 / 검색 | 1887 | 10/2/60/8/18 | `bcd16d0ae5ef30a9c41050b82d5a68ad4393edd6ce8273017ca8c4db6d74994c` |
| `apps/native_poc/src/viewer_udp_session.hpp` | 제품/공유 / 검색 | 61 | 0/0/0/1/0 | `8dc818e552a9a58c6ed903d9a431fbc5e70eee0a0167b76ec227e075970e3b95` |
| `apps/native_poc/src/viewer_ui_resources.hpp` | 제품/공유 / 검색 | 45 | 0/0/0/0/0 | `a4989cafea4ee224e15ec56c5317032a60d3d8c233e55cfd23af4f22672615ab` |
| `apps/native_poc/src/viewer_unlock.cpp` | 제품/공유 / 검색 | 342 | 0/2/1/0/18 | `a114d0cfc3c9a4d1a9771b11aff6f5c781debd0aeb5836f41d100fcc2329f5ea` |
| `apps/native_poc/src/viewer_unlock.hpp` | 제품/공유 / 검색 | 46 | 0/0/0/0/0 | `299a292a8a66b35d8294a681ec958be63f5369a01712261baca1a88dc0f4cfcb` |
| `apps/native_poc/src/viewer_video_receiver.cpp` | 제품/공유 / 분기 추적 | 663 | 0/5/3/2/2 | `48b8e87f01939b4d602b6cf7c26a23285f14906a702ae24c380c781fa49f6e43` |
| `apps/native_poc/src/viewer_video_receiver.hpp` | 제품/공유 / 검색 | 97 | 1/0/0/0/0 | `8d442989b3e5bdca4320df26df78bc58f77510ee9adc70080fe085bb2c650cd7` |
| `apps/native_poc/src/viewer_video_receiver_frame.cpp` | 제품/공유 / 분기 추적 | 455 | 0/0/1/0/1 | `9afa77b7f661d305772bb3f7c5542119c3649dfe5e22a50082b66ddbd2dca12c` |
| `apps/native_poc/src/viewer_window_proc.cpp` | 제품/공유 / 분기 추적 | 702 | 0/0/0/4/4 | `4394c4fe151588358708b4116b7ce5ad3130aa0824a7d06015471b752169e2e7` |
| `apps/native_poc/src/viewer_window_proc.hpp` | 제품/공유 / 검색 | 31 | 0/0/0/0/0 | `91e562cd62e7400bbc000559edf4a148e1bdecbe4de01e977160f068b95210bf` |
| `apps/native_poc/src/viewer_window_proc_isolated_test.cpp` | 테스트 / 검색 | 536 | 0/0/13/1/10 | `5f19fd98abaa98724220385480e3a2db30c808cd77ae33ec89fede5d59ae20fc` |
| `apps/native_poc/src/winhttp_tls_posture_test.cpp` | 테스트 / 검색 | 273 | 0/2/0/0/6 | `e7e7ff8d38c7d0113cc12f9f81ceab146a2235f1767ec07cd77dee003a24bd56` |
| `apps/native_poc/src/winhttp_transport.cpp` | 제품/공유 / 분기 추적 | 167 | 0/0/0/0/9 | `70a774d9fac223251efbc0d1838f9d67e70d6fc1cd4bc35e722deacad53476f5` |
| `apps/native_poc/src/winhttp_transport.hpp` | 제품/공유 / 검색 | 72 | 0/0/0/0/0 | `39572f4f29ea12f20f9315e49f9b8427e90dca530cbd5402a407ef2b0328f0f1` |
| `apps/native_poc/tools/desktop_update_rate.cpp` | 도구 / 검색 | 266 | 0/0/0/0/10 | `f25ac2448f9a054ab6214cd1361392c9ce56125cd52a609379ecf79812e1a923` |
| `apps/native_poc/tools/directory_login_probe.cpp` | 도구 / 검색 | 62 | 0/0/0/0/0 | `dce0a14a259887d7820fb77c7a75867471b81e8673a2eb5643ec737eb5bc8423` |
| `apps/native_poc/tools/picker_preview.cpp` | 도구 / 검색 | 368 | 0/0/1/2/1 | `47f31e29b2f596d11e479dc9b66214f2103b2e5b0aef7521d73ec26854e4ff91` |
| `apps/native_poc/tools/ui_preview.cpp` | 도구 / 검색 | 377 | 0/0/5/0/9 | `0c7636fb94ec14969d11a84945520f5100dae364f496feb07bdd803480678ab2` |
| `apps/native_poc/tools/verify_release_manifest.cpp` | 도구 / 검색 | 117 | 0/0/0/0/1 | `b48e9f1ee3732b1a2ed03bed5028c83fc1fdd05efcf8bfdb9356abe274ae9987` |
| `apps/native_poc/tools/webview2_spike.cpp` | 도구 / 검색 | 175 | 0/0/2/0/4 | `7261ec66cc906ee3e3c92c05aa3b6b33aca5857485f0a145550e2f9952fca103` |
| `apps/native_poc/tools/winlogon_capture_probe.cpp` | 도구 / 검색 | 772 | 4/1/0/2/15 | `513baa483b0a9f82c9be641e670943028a0bbbf62c29d649f2d0a0bdeaa49446` |
| `apps/native_poc/ui/macro.html` | 제품/공유 / 검색 | 274 | 0/0/1/0/0 | `f589f5df16d24fc8932d230da35f1661f0e97c1c63a6f2d43ad7a8fcc095529b` |
| `apps/native_poc/ui/shell.html` | 제품/공유 / 분기 추적 | 463 | 0/0/1/0/0 | `3e59eb07d159bb4625eac713b8434d2e04d79c5100fed0afc24698e55677b5a6` |
| `libs/capture/CMakeLists.txt` | 빌드 / 검색 | 20 | 0/0/0/0/0 | `1f0a5786a81b5857cc35b1e4ca4e87a87bed0678b7e0335d2a3da4eea7f149c7` |
| `libs/capture/src/capture_backend_dxgi.cpp` | 제품/공유 / 분기 추적 | 603 | 3/0/10/7/29 | `deada5063b3c4e838435ff57315f93ca8cedd5c5ad03d24de66211533dceb7a5` |
| `libs/capture/src/capture_backend_dxgi.hpp` | 제품/공유 / 검색 | 109 | 0/0/0/4/0 | `15fe266157209687f8c886a082270ee886cd63cf63c6942ec54ea2625ae481ae` |
| `libs/capture/src/capture_runtime.cpp` | 제품/공유 / 검색 | 276 | 0/0/5/0/9 | `757505f944a298355e8535a081b144d420d4562f0f7b831e0cc3393913bc2cc0` |
| `libs/capture/src/capture_runtime.hpp` | 제품/공유 / 검색 | 27 | 0/0/0/0/0 | `aeef6113642871688b3c7f1933d530ca7519f66a42f068bafff71526470e8310` |
| `libs/capture/src/dxgi_output_selection.hpp` | 제품/공유 / 검색 | 137 | 0/0/0/0/0 | `ccd0c905b3533851b97cd8476c8b5f13c8254406cd8123c2c4b48bad5250bdfe` |
| `libs/capture/src/dxgi_output_selection_test.cpp` | 테스트 / 검색 | 191 | 0/0/0/0/0 | `003a5c9ebc475ede60ce93d27dfcafe34a6c874814aa1694d097bae8dfcd408c` |
| `libs/common/CMakeLists.txt` | 빌드 / 검색 | 17 | 0/0/0/0/0 | `bf234cbf263e2e8668a581b8de2cd5d582bd6d394d04f2de939269739dd95ec8` |
| `libs/common/include/common/input_protocol.hpp` | 제품/공유 / 검색 | 32 | 0/0/0/0/0 | `df1d649d9b1ccdfcd6da40246e84e81ab4a57784a0c1e0414d56d9c4de624744` |
| `libs/common/include/common/version.hpp` | 제품/공유 / 검색 | 5 | 0/0/0/0/0 | `74ab130afa0765751a8cb97db3215eb22f2756d44b54ad1028345c544b3c0716` |
| `libs/common/src/input_protocol.cpp` | 제품/공유 / 검색 | 89 | 0/0/0/0/0 | `d2f73c95c2c3f163ebfd83ac56915ea73bf8377327d38bbdb904c0d4a693d748` |
| `libs/common/src/version.cpp` | 제품/공유 / 검색 | 5 | 0/0/0/0/0 | `97b49fa0b46319023f77cb3022b8461d253a5d70c8a965d3bc8d1fdc578377c0` |

## 문서 작성 중 기준본과 달라진 파일

검사 시 HEAD: `4cad92e3870ef57273ddffec2cde65cda81cf6bb`. 아래는 **별도 작업의 변경**이며 조사자가 수정한 제품 파일이 아니다. 원장 줄 번호는 고정 사본 기준이다.

- `apps/native_poc/src/product_version.hpp`
- `apps/native_poc/src/viewer_window_proc_isolated_test.cpp`
