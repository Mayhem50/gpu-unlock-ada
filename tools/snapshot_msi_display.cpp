// Read-only active Windows display routes, using public User32 APIs.
#define UNICODE
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#pragma comment(lib,"user32.lib")
static std::string quote(const wchar_t *s){
 int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);
 std::string utf(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s,-1,utf.data(),n,nullptr,nullptr);if(n)utf.pop_back();
 std::string r="\"";char b[8];for(unsigned char c:utf){if(c=='"'||c=='\\'){r+='\\';r+=c;}else if(c<32){sprintf_s(b,"\\u%04x",c);r+=b;}else r+=c;}return r+'"';
}
int main(){
 UINT32 np=0,nm=0;std::vector<DISPLAYCONFIG_PATH_INFO> paths;std::vector<DISPLAYCONFIG_MODE_INFO> modes;LONG s=0;
 for(unsigned attempt=0;attempt<3;attempt++){
  s=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&np,&nm);if(s)return 2;
  paths.resize(np);modes.resize(nm);s=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&np,paths.data(),&nm,modes.data(),nullptr);
  if(s!=ERROR_INSUFFICIENT_BUFFER)break;
 }
 if(s){fprintf(stderr,"QueryDisplayConfig: %ld\n",s);return 3;}
 printf("{\"active_paths\":%u,\"paths\":[",np);
 for(UINT32 i=0;i<np;i++){
  const auto &p=paths[i];DISPLAYCONFIG_ADAPTER_NAME a{};a.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;a.header.size=sizeof(a);a.header.adapterId=p.sourceInfo.adapterId;
  DISPLAYCONFIG_TARGET_DEVICE_NAME t{};t.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;t.header.size=sizeof(t);t.header.adapterId=p.targetInfo.adapterId;t.header.id=p.targetInfo.id;
  if(DisplayConfigGetDeviceInfo(&a.header)||DisplayConfigGetDeviceInfo(&t.header))return 4;
  if(i)putchar(',');printf("{\"adapter\":%s,\"monitor\":%s,\"monitor_path\":%s,\"technology\":%u,\"source_id\":%u,\"target_id\":%u}",quote(a.adapterDevicePath).c_str(),quote(t.monitorFriendlyDeviceName).c_str(),quote(t.monitorDevicePath).c_str(),(unsigned)p.targetInfo.outputTechnology,p.sourceInfo.id,p.targetInfo.id);
 }
 puts("]}");return 0;
}
