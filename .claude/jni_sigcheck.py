import re,sys
root=sys.argv[1]
kt=open(root+'/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt',encoding='utf-8').read()
cpp=open(root+'/apps/android_direct_client/app/src/main/cpp/native_bridge.cpp',encoding='utf-8').read()
T={'Long':'jlong','Int':'jint','Boolean':'jboolean','String':'jstring','String?':'jstring','Array<String>':'jobjectArray','Surface?':'jobject','LongArray':'jlongArray','ByteArray?':'jbyteArray','':'void'}
K={}
for m in re.finditer(r'external fun (\w+)\(([^)]*)\)\s*(?::\s*([\w<>?]+))?',kt):
    params=[x.strip() for x in m.group(2).split(',') if x.strip()]
    types=[T[p.split(':',1)[1].strip()] for p in params]
    K[m.group(1)]=(T[m.group(3) or ''],types)
C={}
for m in re.finditer(r'extern "C" JNIEXPORT (\w+) JNICALL\s*\nJava_com_remote60_androiddirect_NativeSessionBridge_(\w+)\(\s*([^)]*)\)',cpp):
    params=[x.strip() for x in m.group(3).replace('\n',' ').split(',')]
    types=[p.split()[0] for p in params[2:]]
    C[m.group(2)]=(m.group(1),types)
bad=0
for n in sorted(set(K)|set(C)):
    if K.get(n)!=C.get(n):
        bad+=1; print('MISMATCH',n,'kt',K.get(n),'cpp',C.get(n))
print('kotlin',len(K),'cpp',len(C),'mismatch',bad)
sys.exit(1 if bad else 0)
