from pathlib import Path
import subprocess,os
repo=Path('C:/work/gnlink-wt/apk-inbox-c')
root=Path(__file__).parent/'c_d504_review'
prefix=(root/'probe.cpp').read_text(encoding='utf-8').split('int main(){')[0]
main=r'''
int main(){
 AndroidVideoDecoderSink s;s.SetSurface(nullptr,reinterpret_cast<jobject>(0x101));
 auto waitq=[&](size_t n){for(int i=0;i<1000&&queued()<n;i++)pause(2);return queued()>=n;};
 auto lose=[&](unsigned seq){auto x=f(seq,false);x.payload.resize((4u<<20)+1);s.OnEncodedH264Frame(std::move(x));};
 s.OnEncodedH264Frame(f(1,true));if(!waitq(1))return 2;
 lose(2);DecoderKeyRequest oldReq{};if(!s.TakeDecoderKeyRequest(&oldReq))return 2;
 s.OnEncodedH264Frame(f(3,true));if(!waitq(2))return 2;pause(10);
 lose(4);DecoderKeyRequest current{};if(!s.TakeDecoderKeyRequest(&current))return 2;
 auto merged=current;merged.Merge(oldReq);
 auto good=s.DecoderKeyRequestValidCauses(current),bad=s.DecoderKeyRequestValidCauses(merged);
 printf("SAME EPOCH late old publish: epoch=%llu currentEpisode=%llu oldEpisode=%llu mergedEpisode=%llu validNew=%u validMerged=%u\n",(unsigned long long)current.sinkEpoch,(unsigned long long)current.episodeId,(unsigned long long)oldReq.episodeId,(unsigned long long)merged.episodeId,good,bad);
 bool a=(good&kKeyCauseRecovery)!=0&&bad==0;
 s.SetSurface(nullptr,reinterpret_cast<jobject>(0x202));
 s.OnEncodedH264Frame(f(5,true));if(!waitq(3))return 2;pause(10);
 lose(6);DecoderKeyRequest newer{};if(!s.TakeDecoderKeyRequest(&newer))return 2;
 merged=newer;merged.Merge(oldReq);
 good=s.DecoderKeyRequestValidCauses(newer);bad=s.DecoderKeyRequestValidCauses(merged);
 printf("NEW EPOCH late old publish: currentEpoch=%llu oldEpoch=%llu mergedEpoch=%llu validNew=%u validMerged=%u\n",(unsigned long long)newer.sinkEpoch,(unsigned long long)oldReq.sinkEpoch,(unsigned long long)merged.sinkEpoch,good,bad);
 bool b=(good&kKeyCauseRecovery)!=0&&bad==0;
 s.SetSurface(nullptr,nullptr);return a&&b?0:1;
}
'''
(root/'merge_probe.cpp').write_text(prefix+main,encoding='utf-8')
base=Path(__file__).with_name('c_review_probe.py').read_text(encoding='utf-8')
build='ms=Path'+base.split('ms=Path',1)[1]
for a,b in [('probe.cpp','merge_probe.cpp'),('probe.exe','merge_probe.exe'),('build.log','merge_build.log'),('probe.log','merge_probe.log')]:build=build.replace(a,b)
exec(compile(build,'merge-probe-build','exec'))
assert r.returncode==0
