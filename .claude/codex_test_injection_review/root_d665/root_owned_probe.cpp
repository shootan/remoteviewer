
#define main guard_runtime_original_main
#include "e2e_guard_runtime_test.cpp"
#undef main
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;bool ok=true;std::string why;std::wstring root;
 const wchar_t* invalid[]={nullptr,L"C:\\codex-root-must-not-be-created",L"D:\\remote\\remote\\.claude\\test-tmp",L"D:\\remote\\remote\\.claude\\test-tmp\\..\\must-not-create"};
 int refused=0;
 for(const auto* p:invalid){SetEnvironmentVariableW(L"REMOTE60_TEST_TMP",p);why.clear();bool accepted=e::e2e_test_root(false,&root,&why);refused+=!accepted;ok&=!accepted;}
 std::printf("REVIEW read_only_invalid_roots_refused=%d_of_4\n",refused);
 SetEnvironmentVariableW(L"REMOTE60_TEST_TMP",argv[1]);
 ok&=e::e2e_test_root(true,&root,&why);ok&=e::e2e_new_run_dir(root,kRunPrefix,&gRunDir,&why);
 ok&=e::e2e_run_dir_valid(gRunDir,kRunPrefix,&why);
 SetEnvironmentVariableW(L"REMOTE60_E2E_RUN_DIR",gRunDir.c_str());ok&=child_run_dir(&why);
 const std::wstring f=run_file("review_owned","review-fixture");
 const std::wstring collision=run_file("review_owned","must-not-overwrite");
 auto snapshot=e::e2e_final_file(f);bool preserved=!f.empty()&&collision.empty()&&snapshot.complete&&snapshot.text=="review-fixture";ok&=preserved;
 delete_run_file(f);
 bool gone=GetFileAttributesW(f.c_str())==INVALID_FILE_ATTRIBUTES;
 ok&=gone;
 std::printf("REVIEW new_run_valid=%d collision_refused_and_original_preserved=%d own_file_removed=%d\n",e::e2e_run_dir_valid(gRunDir,kRunPrefix,&why),preserved,gone);
 std::puts("REVIEW Host_Viewer_GPU_network_input_power_clipboard=0; files confined to verified repository root");
 return ok?0:1;
}
