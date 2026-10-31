
#include <cstdio>
#include <initializer_list>
#include "client_mouse_xbutton_fence.hpp"
namespace n=remote60::native_poc;
int main(){
 uint16_t bits=8;
 bool down=n::FenceMouseXButtonsForHost(n::HostTakesMouseXButtons(false,0),2,5,&bits);
 bits=0;
 bool up=n::FenceMouseXButtonsForHost(n::HostTakesMouseXButtons(true,n::kCaptureFlagMouseXButtonsV1),3,5,&bits);
 bits=8;
 bool move=n::FenceMouseXButtonsForHost(true,1,0,&bits);
 std::printf("REVIEW first_Pong_transition down_accepted=%d later_up_accepted=%d later_move_X_bits=%u\n",down,up,bits);
 int refused=0;
 for(uint16_t kind:{uint16_t(2),uint16_t(3)})for(uint32_t key:{5u,6u}){uint16_t b=31;refused+=!n::FenceMouseXButtonsForHost(false,kind,key,&b);}
 uint16_t legacy=31;bool legacyMove=n::FenceMouseXButtonsForHost(false,1,0,&legacy);
 struct E{uint16_t kind;uint32_t key;uint16_t buttons;};
 const E regular[]={{5,9,0},{6,9,0},{1,0,0},{2,1,1},{3,1,0}};
 int regularPass=0;for(const auto& e:regular){uint16_t b=e.buttons;regularPass+=n::FenceMouseXButtonsForHost(false,e.kind,e.key,&b)&&b==e.buttons;}
 std::printf("REVIEW steady_noX refused_edges=%d move_allowed=%d masked_buttons=%u regular_record_inputs_unchanged=%d\n",refused,legacyMove,legacy,regularPass);
 bits=8;bool heldDown=n::FenceMouseXButtonsForHost(true,2,5,&bits);bits=0;bool lostCapUp=n::FenceMouseXButtonsForHost(false,3,5,&bits);
 std::printf("REVIEW capability_drop earlier_down_accepted=%d later_up_accepted=%d\n",heldDown,lostCapUp);
 std::puts("REVIEW actual_Host_Viewer_AVD_JNI_network_input_power_clipboard_APIs=0");
 return !down&&up&&move&&refused==4&&legacyMove&&legacy==7&&regularPass==5&&heldDown&&!lostCapUp?0:1;
}
