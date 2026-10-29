// Offline ETL reader only: Windows SDK ProcessTrace + TDH; no GPU access.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <evntrace.h>
#include <tdh.h>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstring>
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"tdh.lib")
static FILE *output;
static unsigned long long total=0,selected=0,dxgTotal=0;
static std::map<unsigned,unsigned long long> counts;
static const GUID dxg={0x802ec45a,0x1e99,0x4b83,{0x99,0x20,0x87,0xc9,0x82,0x77,0xba,0x9d}};
static const GUID registry={0x70eb4f03,0xc1de,0x4f73,{0xa0,0x51,0x33,0xd1,0x3d,0x54,0x13,0xbd}};
static const std::set<unsigned> wanted={24,25,26,75,110,128,129,138,139,154,155,203,204,205,206,207,208,209,214,220,221,222,223,228,229,232,233,234,235,236,237,242,243,253,254,255,256,257,258,269,270,283,354,368,442,449,457,459,460,480,519,549,1094,1095};
static std::string utf8(const wchar_t *s){
 if(!s)return "";int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);
 if(n<=0)return "";std::string r(n,'\0');WideCharToMultiByte(CP_UTF8,0,s,-1,r.data(),n,nullptr,nullptr);r.pop_back();return r;
}
static std::string quote(const std::string &s){
 std::string r="\"";char b[8];for(unsigned char c:s){if(c=='"'||c=='\\'){r+='\\';r+=c;}else if(c<32){sprintf_s(b,"\\u%04x",c);r+=b;}else r+=c;}return r+'"';
}
static std::string hexbytes(const std::vector<unsigned char>&b){
 std::string r;char h[3];for(auto c:b){sprintf_s(h,"%02x",c);r+=h;}return r;
}
static std::string value(USHORT type,const std::vector<unsigned char>&b){
 if(type==TDH_INTYPE_UNICODESTRING&&b.size()>=2){auto copy=b;copy.push_back(0);copy.push_back(0);return utf8((wchar_t*)copy.data());}
 if(type==TDH_INTYPE_ANSISTRING){return std::string((char*)b.data(),strnlen((char*)b.data(),b.size()));}
 if(b.size()==1||b.size()==2||b.size()==4||b.size()==8){unsigned long long v=0;memcpy(&v,b.data(),b.size());char h[32];sprintf_s(h,"0x%llx",v);return h;}
 return hexbytes(b);
}
static bool contains(EVENT_RECORD *e,const wchar_t *text){
 const size_t n=wcslen(text)*sizeof(wchar_t);if(e->UserDataLength<n)return false;
 for(size_t i=0;i+n<=e->UserDataLength;i++)if(memcmp((unsigned char*)e->UserData+i,text,n)==0)return true;
 return false;
}
static void WINAPI record(EVENT_RECORD *e){
 total++;const bool isDxg=IsEqualGUID(e->EventHeader.ProviderId,dxg)!=0;
 const bool isRegistry=IsEqualGUID(e->EventHeader.ProviderId,registry)!=0;
 unsigned id=e->EventHeader.EventDescriptor.Id;
 if(isDxg){dxgTotal++;counts[id]++;if(!wanted.count(id))return;}
 else if(isRegistry){if(!contains(e,L"EnableRuntimePowerManagement")&&!contains(e,L"4d36e968-e325-11ce-bfc1-08002be10318")&&!contains(e,L"4D36E968-E325-11CE-BFC1-08002BE10318"))return;}
 else return;
 selected++;
 SYSTEMTIME t{};FILETIME ft;ULARGE_INTEGER stamp;stamp.QuadPart=e->EventHeader.TimeStamp.QuadPart;ft.dwLowDateTime=stamp.LowPart;ft.dwHighDateTime=stamp.HighPart;FileTimeToSystemTime(&ft,&t);
 char date[64];sprintf_s(date,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
 ULONG bytes=0;auto status=TdhGetEventInformation(e,0,nullptr,nullptr,&bytes);
 std::vector<unsigned char> infoBytes(bytes);TRACE_EVENT_INFO *info=bytes?(TRACE_EVENT_INFO*)infoBytes.data():nullptr;
 if(status==ERROR_INSUFFICIENT_BUFFER)status=TdhGetEventInformation(e,0,nullptr,info,&bytes);
 std::string task=status==ERROR_SUCCESS&&info->TaskNameOffset?utf8((wchar_t*)(infoBytes.data()+info->TaskNameOffset)):"";
 fprintf(output,"{\"provider\":\"%s\",\"utc\":%s,\"id\":%u,\"version\":%u,\"task\":%s,\"opcode\":%u,\"pid\":%lu,\"tid\":%lu,\"tdh_status\":%lu,\"data\":{",isDxg?"Microsoft-Windows-DxgKrnl":"Microsoft-Windows-Kernel-Registry",quote(date).c_str(),id,e->EventHeader.EventDescriptor.Version,quote(task).c_str(),e->EventHeader.EventDescriptor.Opcode,e->EventHeader.ProcessId,e->EventHeader.ThreadId,status);
 bool first=true;
 if(status==ERROR_SUCCESS){for(ULONG i=0;i<info->TopLevelPropertyCount;i++){
  const auto &p=info->EventPropertyInfoArray[i];if(p.Flags&PropertyStruct)continue;
  auto name=(wchar_t*)(infoBytes.data()+p.NameOffset);
  PROPERTY_DATA_DESCRIPTOR d{};d.PropertyName=(ULONGLONG)name;d.ArrayIndex=ULONG_MAX;
  ULONG n=0;auto ps=TdhGetPropertySize(e,0,nullptr,1,&d,&n);if(ps||n>1024*1024)continue;
  std::vector<unsigned char> data(n);ps=TdhGetProperty(e,0,nullptr,1,&d,n,data.data());if(ps)continue;
  if(!first)fputc(',',output);first=false;
  fprintf(output,"%s:%s",quote(utf8(name)).c_str(),quote(value(p.nonStructType.InType,data)).c_str());
 }}
 fprintf(output,"}}\n");
}
int wmain(int argc,wchar_t **argv){
 if(argc!=3){fwprintf(stderr,L"Usage: read_msi_etl trace.etl output.jsonl\n");return 2;}
 if(_wfopen_s(&output,argv[2],L"wb"))return 3;
 EVENT_TRACE_LOGFILEW log{};log.LogFileName=argv[1];log.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD;log.EventRecordCallback=record;
 TRACEHANDLE trace=OpenTraceW(&log);if(trace==INVALID_PROCESSTRACE_HANDLE){fprintf(stderr,"OpenTrace error %lu\n",GetLastError());fclose(output);return 4;}
 auto status=ProcessTrace(&trace,1,nullptr,nullptr);CloseTrace(trace);fclose(output);
 printf("{\"process_trace_status\":%lu,\"total_events\":%llu,\"dxg_events\":%llu,\"selected_events\":%llu,\"events_lost\":%lu,\"dxg_id_counts\":{",status,total,dxgTotal,selected,log.LogfileHeader.EventsLost);
 bool first=true;for(auto pair:counts){if(!first)putchar(',');first=false;printf("\"%u\":%llu",pair.first,pair.second);}puts("}}");
 return status==ERROR_SUCCESS?0:5;
}
