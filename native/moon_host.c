#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "MinHook.h"
#include "proxy_exports.h"
#include "headless_graphics.h"
#include "server_network.h"

typedef const void Method;
typedef void Object;
FARPROC g_version_procs[17];
static HMODULE game_module;
static CRITICAL_SECTION log_lock;
static volatile LONG installed, installing, install_failed, tick_busy;
static int host_slot=1, privacy=2;
static bool headless=true, autostart=true;
static char log_path[4096], state_path[4096], command_path[4096], last_error[1024];

typedef struct {
 void *(__cdecl *domain_get)(void);
 const void **(__cdecl *domain_get_assemblies)(void *,size_t *);
 const void *(__cdecl *assembly_get_image)(const void *);
 const char *(__cdecl *image_get_name)(const void *);
 void *(__cdecl *class_from_name)(const void *,const char *,const char *);
 const Method *(__cdecl *class_get_method_from_name)(void *,const char *,int);
 void *(__cdecl *method_get_class)(const Method *);
 const char *(__cdecl *class_get_namespace)(void *);
 void *(__cdecl *class_get_field_from_name)(void *,const char *);
 Object *(__cdecl *field_get_value_object)(void *,Object *);
 void (__cdecl *field_get_value)(Object *,void *,void *);
 Object *(__cdecl *runtime_invoke)(const Method *,Object *,void **,Object **);
 void *(__cdecl *object_unbox)(Object *);
 void *(__cdecl *object_get_class)(Object *);
 int (__cdecl *string_length)(Object *);
 const wchar_t *(__cdecl *string_chars)(Object *);
 void (__cdecl *format_exception)(Object *,char *,int);
 void *(__cdecl *thread_current)(void);
 void *(__cdecl *thread_attach)(void *);
 void (__cdecl *thread_detach)(void *);
 uintptr_t (__cdecl *gchandle_new)(Object *,bool);
 Object *(__cdecl *gchandle_get_target)(uintptr_t);
 void (__cdecl *gchandle_free)(uintptr_t);
 uintptr_t (__cdecl *array_length)(Object *);
} Il2Cpp;
static Il2Cpp api;
static const void *game_image;
static struct {
 const Method *steam, *menu, *game, *loading, *lobby, *host, *busy, *shift;
 const Method *set_slot, *create, *set_ready, *start_shift, *load, *dismiss;
 const Method *crew_instance;
 const Method *save_progress,*quit_game;
 void *crew_bound_field, *game_scene_field;
 void *world_progress_field, *world_regions_field;
} methods;
static uintptr_t initialized_world;
static uintptr_t save_task;
static bool stop_after_save,quit_requested,save_failed;
static ULONGLONG save_deadline;
static void (__cdecl *original_world_initialize)(Object *,const Method *);
static void (__cdecl *original_update)(Object *,const Method *);
static bool (__cdecl *original_signed)(int,const Method *);
static bool (__cdecl *original_show)(Object *,Object *,const Method *);
static Object *(__cdecl *unity_runtime_invoke)(const Method *,Object *,void **,Object **);
static FARPROC (WINAPI *unity_get_proc_address)(HMODULE,LPCSTR);
static volatile LONG unity_import_patched;
static volatile LONG unity_runtime_patched;

static void note(const char *message) {
 SYSTEMTIME t; GetSystemTime(&t);
 EnterCriticalSection(&log_lock);
 FILE *f=fopen(log_path,"a");
 if(f) {fprintf(f,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ %s\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,message);fclose(f);}
 LeaveCriticalSection(&log_lock);
}
static void error(const char *message) {
 snprintf(last_error,sizeof(last_error),"%s",message); note(message);
}
static void json_string(FILE *f,const char *text) {
 fputc('"',f);
 for(const unsigned char *p=(const unsigned char *)text;*p;p++) {
  if(*p=='"'||*p=='\\') {fputc('\\',f);fputc(*p,f);}
  else if(*p<32)fprintf(f,"\\u%04x",*p);
  else fputc(*p,f);
 }
 fputc('"',f);
}
typedef struct {bool steam,menu,game,loading,lobby,host,busy,shift,bound;} State;
static const char *phase="initializing";
static void write_state(State q) {
 char temp[4160];snprintf(temp,sizeof(temp),"%s.tmp",state_path);
 FILE *f=fopen(temp,"w"); if(!f)return;
 FILETIME ft;GetSystemTimeAsFileTime(&ft);
 ULARGE_INTEGER time;time.LowPart=ft.dwLowDateTime;time.HighPart=ft.dwHighDateTime;
 unsigned long long unix_ms=(time.QuadPart-116444736000000000ULL)/10000;
 bool ready=q.steam&&q.game&&!q.loading&&q.lobby&&q.bound&&(MoonNet_IsClient()||(q.host&&q.shift))&&!MoonNet_IsStopped()&&!stop_after_save&&!save_failed;
 fprintf(f,"{\"schema\":1,\"heartbeat_unix_ms\":%llu,\"phase\":",unix_ms);json_string(f,phase);
 fprintf(f,",\"slot\":%d,\"steam\":%s,\"menu\":%s,\"in_game\":%s,\"loading\":%s,\"in_lobby\":%s,\"is_host\":%s,\"shift_started\":%s,\"world_bound\":%s,\"ready\":%s,\"error\":",host_slot,q.steam?"true":"false",q.menu?"true":"false",q.game?"true":"false",q.loading?"true":"false",q.lobby?"true":"false",q.host?"true":"false",q.shift?"true":"false",q.bound?"true":"false",ready?"true":"false");
 json_string(f,last_error);fprintf(f,",\"save_pending\":%s,\"save_failed\":%s,\"dedicated\":%s,\"client\":%s,\"server_steam_id\":\"%llu\",\"authenticated_players\":%u,\"join_transport\":\"steam\",\"network_phase\":",save_task?"true":"false",save_failed?"true":"false",MoonNet_IsDedicated()?"true":"false",MoonNet_IsClient()?"true":"false",(unsigned long long)MoonNet_ServerId(),MoonNet_AuthenticatedPeers());json_string(f,MoonNet_Phase());if(MoonNet_IsDedicated()){fprintf(f,",\"game_port\":%d,\"query_port\":%d,\"steam_account_type\":%u,\"steam_login_mode\":",MoonNet_GamePort(),MoonNet_QueryPort(),MoonNet_ServerAccountType());json_string(f,MoonNet_LoginMode());}fputs("}\n",f);fclose(f);
 MoveFileExA(temp,state_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
static void env_text(const char *name,char *target,size_t capacity) {
 DWORD n=GetEnvironmentVariableA(name,target,(DWORD)capacity);
 if(n==0||n>=capacity)target[0]=0;
}
static bool opted_in(void) {
 wchar_t executable[MAX_PATH];DWORD size=GetModuleFileNameW(NULL,executable,MAX_PATH);
 if(!size||size>=MAX_PATH)return false;
 const wchar_t *leaf=wcsrchr(executable,L'\\');leaf=leaf?leaf+1:executable;
 if(_wcsicmp(leaf,L"MoonSweepers.exe")!=0)return false;
 if(MoonNet_Requested())return true;
 char value[16];env_text("MOON_HOST",value,sizeof(value));
 if(strcmp(value,"1")==0)return true;
 int argc=0;LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 bool yes=false;
 if(argv){for(int i=1;i<argc;i++)if(wcscmp(argv[i],L"-moon-host")==0)yes=true;LocalFree(argv);}
 return yes;
}
static void text_from_wide(const wchar_t *input,char *output,size_t size) {
 if(!WideCharToMultiByte(CP_UTF8,0,input,-1,output,(int)size,NULL,NULL))output[0]=0;
}
static bool configure(void) {
 char value[2048];
 GetModuleFileNameA(NULL,value,sizeof(value));value[sizeof(value)-1]=0;char *slash=strrchr(value,'\\');
 if(slash)slash[1]=0;else value[0]=0;
 snprintf(log_path,sizeof(log_path),"%smoon-host.log",value);
 snprintf(state_path,sizeof(state_path),"%smoon-host-state.json",value);
 snprintf(command_path,sizeof(command_path),"%smoon-host-command.txt",value);
 env_text("MOON_HOST_LOG",value,sizeof(value));if(*value)snprintf(log_path,sizeof(log_path),"%s",value);
 env_text("MOON_HOST_STATE",value,sizeof(value));if(*value)snprintf(state_path,sizeof(state_path),"%s",value);
 env_text("MOON_HOST_COMMAND",value,sizeof(value));if(*value)snprintf(command_path,sizeof(command_path),"%s",value);
 env_text("MOON_SLOT",value,sizeof(value));if(*value)host_slot=atoi(value);
 env_text("MOON_HEADLESS",value,sizeof(value));if(*value)headless=strcmp(value,"0")!=0;
 env_text("MOON_AUTOSTART",value,sizeof(value));if(*value)autostart=strcmp(value,"0")!=0;
 env_text("MOON_PRIVACY",value,sizeof(value));
 if(*value){if(strcmp(value,"friends")==0)privacy=0;else if(strcmp(value,"invite")!=0){error("MOON_PRIVACY must be invite or friends");return false;}}
 int argc=0;LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 if(argv){
  for(int i=1;i<argc;i++) {
   if(wcscmp(argv[i],L"-moon-slot")==0&&i+1<argc)host_slot=_wtoi(argv[++i]);
   else if(wcscmp(argv[i],L"-moon-privacy")==0&&i+1<argc){i++;if(wcscmp(argv[i],L"invite")==0)privacy=2;else if(wcscmp(argv[i],L"friends")==0)privacy=0;else{LocalFree(argv);error("-moon-privacy must be invite or friends");return false;}}
   else if(wcscmp(argv[i],L"-moon-headless")==0)headless=true;
   else if(wcscmp(argv[i],L"-moon-graphics")==0)headless=false;
   else if(wcscmp(argv[i],L"-moon-no-autostart")==0)autostart=false;
   else if(wcscmp(argv[i],L"-moon-log")==0&&i+1<argc)text_from_wide(argv[++i],log_path,sizeof(log_path));
   else if(wcscmp(argv[i],L"-moon-state")==0&&i+1<argc)text_from_wide(argv[++i],state_path,sizeof(state_path));
  }
  LocalFree(argv);
 }
 if(host_slot<1||host_slot>5){error("Host slot must be between 1 and 5");return false;}
 if(MoonNet_IsClient()){headless=false;autostart=false;}
 char message[160];snprintf(message,sizeof(message),"Moon host enabled: slot=%d privacy=%s headless=%d autostart=%d",host_slot,privacy==2?"invite":"friends",headless,autostart);note(message);
 return true;
}
static bool resolve_api(void) {
#define API(name) do{FARPROC p=GetProcAddress(game_module,"il2cpp_" #name);memcpy(&api.name,&p,sizeof(p));if(!api.name){error("Missing il2cpp_" #name);return false;}}while(0)
 API(domain_get);API(domain_get_assemblies);API(assembly_get_image);API(image_get_name);
 API(class_from_name);API(class_get_method_from_name);API(method_get_class);API(class_get_namespace);API(class_get_field_from_name);
 API(field_get_value_object);API(field_get_value);API(runtime_invoke);API(object_unbox);API(object_get_class);
 API(string_length);API(string_chars);API(format_exception);API(thread_current);API(thread_attach);API(thread_detach);
 API(gchandle_new);API(gchandle_get_target);API(gchandle_free);API(array_length);
#undef API
 return true;
}
static void *klass(const char *name) {return api.class_from_name(game_image,"MoonCleaners",name);}
static const Method *method(const char *type,const char *name,int argc) {
 void *k=klass(type);return k?api.class_get_method_from_name(k,name,argc):NULL;
}
static void *method_code(const Method *m) {
 if(!m)return NULL;
 void *code=*(void *const *)m;
 MEMORY_BASIC_INFORMATION info;
 if(!code||VirtualQuery(code,&info,sizeof(info))!=sizeof(info))return NULL;
 DWORD protect=info.Protect&0xff;
 if((info.Protect&PAGE_GUARD)||info.State!=MEM_COMMIT)return NULL;
 if(protect!=PAGE_EXECUTE&&protect!=PAGE_EXECUTE_READ&&protect!=PAGE_EXECUTE_READWRITE&&protect!=PAGE_EXECUTE_WRITECOPY)return NULL;
 return code;
}
static Object *call(const Method *m,Object *self,void **args,bool *ok) {
 if(!m){*ok=false;error("Missing required managed method");return NULL;}
 Object *exception=NULL;Object *result=api.runtime_invoke(m,self,args,&exception);
 if(exception){char text[768]={0};api.format_exception(exception,text,sizeof(text));error(text);*ok=false;}
 return result;
}
static bool boolean(const Method *m,bool *ok) {
 Object *o=call(m,NULL,NULL,ok);return o&&*(uint8_t *)api.object_unbox(o)!=0;
}
static State read_state(bool *ok) {
 State q={0};q.steam=boolean(methods.steam,ok);q.menu=boolean(methods.menu,ok);
 q.game=boolean(methods.game,ok);q.loading=boolean(methods.loading,ok);
 q.lobby=boolean(methods.lobby,ok);q.host=boolean(methods.host,ok);
 q.busy=boolean(methods.busy,ok);q.shift=boolean(methods.shift,ok);
 if(q.game&&methods.crew_instance&&methods.crew_bound_field){Object *crew=call(methods.crew_instance,NULL,NULL,ok);Object *world=NULL;if(crew)api.field_get_value(crew,methods.crew_bound_field,&world);q.bound=world&&initialized_world&&api.gchandle_get_target(initialized_world)==world;}
 return q;
}
static void diagnostic_game_state(State q) {
 static State previous;static bool recorded;
 if(recorded&&previous.steam==q.steam&&previous.menu==q.menu&&previous.game==q.game&&previous.loading==q.loading&&previous.lobby==q.lobby&&previous.host==q.host&&previous.busy==q.busy&&previous.shift==q.shift&&previous.bound==q.bound)return;
 previous=q;recorded=true;
 char details[160];snprintf(details,sizeof(details),"steam=%d menu=%d game=%d loading=%d lobby=%d host=%d busy=%d shift=%d world_bound=%d",q.steam,q.menu,q.game,q.loading,q.lobby,q.host,q.busy,q.shift,q.bound);
 MoonNet_Diagnostic("game_state",details);
}
static void transition(const char *next) {if(strcmp(phase,next)!=0){phase=next;note(next);MoonNet_Diagnostic("host_phase",next);}}
static void request_quit(void) {
 if(quit_requested)return;
 quit_requested=true;transition(save_failed?"stopped-save-failed":"stopped");
 MoonNet_Shutdown();bool ok=true;call(methods.quit_game,NULL,NULL,&ok);
}
static bool task_flag(Object *task,const char *name,bool *ok) {
 const Method *m=api.class_get_method_from_name(api.object_get_class(task),name,0);
 Object *result=call(m,task,NULL,ok);return result&&*(uint8_t *)api.object_unbox(result)!=0;
}
static bool control_tick(State q,ULONGLONG now) {
 char command[32]={0};FILE *f=fopen(command_path,"r");
 if(f){bool read=fgets(command,sizeof(command),f)!=NULL;fclose(f);if(read&&DeleteFileA(command_path)){
  command[strcspn(command,"\r\n")]=0;
  if(strcmp(command,"stop")==0||strcmp(command,"save")==0){
   if(MoonNet_IsClient())note("Host save command ignored by player client");
   else {
    if(strcmp(command,"stop")==0)stop_after_save=true;
    if(!save_task&&!quit_requested){
     save_failed=false;
     if(q.bound&&initialized_world){
      bool ok=true;uint8_t complete=1;void *args[]={&complete};
      Object *task=call(methods.save_progress,api.gchandle_get_target(initialized_world),args,&ok);
      if(ok&&task){save_task=api.gchandle_new(task,false);save_deadline=now+90000;transition(stop_after_save?"stopping":"saving");note("Requested final CPU world save");}
      else {save_failed=true;error("World save did not return a completion task");}
     }else if(!stop_after_save){save_failed=true;error("Save refused: no initialized world is bound");}
    }
   }
  }else note("Unknown host command ignored");
 }}
 if(save_task){
  Object *task=api.gchandle_get_target(save_task);bool ok=true;
  bool done=task&&task_flag(task,"get_IsCompleted",&ok);
  if(done||!ok||now>=save_deadline){
   if(!done||!ok||task_flag(task,"get_IsFaulted",&ok)||task_flag(task,"get_IsCanceled",&ok)||!ok){save_failed=true;error("World save failed, was canceled, or exceeded 90 seconds");}
   else {last_error[0]=0;note("World save completed successfully");MoonNet_Diagnostic("world_save_completed","success=1");}
   api.gchandle_free(save_task);save_task=0;
  }
 }
 if(stop_after_save&&!save_task)request_quit();
 if(save_task||stop_after_save||save_failed){if(save_failed&&!stop_after_save)transition("save-failed");write_state(q);return true;}
 return false;
}
static bool client_world_timeout_tick(State q,ULONGLONG now) {
 static ULONGLONG deadline;
 static int stage;
 static bool timed_out;
 if(!MoonNet_IsClient()){deadline=0;stage=0;timed_out=false;return false;}
 if(q.bound&&!q.loading){deadline=0;stage=0;timed_out=false;last_error[0]=0;return false;}
 const char *network_phase=MoonNet_Phase();
 int current=strcmp(network_phase,"waiting-server-world")==0?1:strcmp(network_phase,"client-downloading-world")==0?2:0;
 if(timed_out)return true;
 if(!current){deadline=0;stage=0;return false;}
 if(current!=stage){
  stage=current;deadline=now+180000;
  MoonNet_Diagnostic("client_world_load_started",stage==1?"stage=server-world-wait":"stage=client-world-load");
 }
 if(now>=deadline){
  if(stage==1)error("Client waited 3 minutes for the server world. Check that server status reports ready=true and that its current server_steam_id matches this client.");
  else error("Client world load exceeded 3 minutes. Check the server log and this PC's moon-host.log; if the server restarted, update the saved server SteamID and reconnect.");
  transition("client-world-timeout");
  MoonNet_Diagnostic("client_world_load_timeout",stage==1?"stage=server-world-wait timeout_seconds=180":"stage=client-world-load timeout_seconds=180");
  timed_out=true;
  return true;
 }
 return false;
}
static void host_tick(void) {
 static ULONGLONG next_tick,deadline,retry_at,entered_start;
 static bool direct_load;
 ULONGLONG now=GetTickCount64();if(now<next_tick)return;next_tick=now+250;
 if(InterlockedCompareExchange(&tick_busy,1,0)!=0)return;
 if(headless)moon_headless_frame_limit();
 bool ok=true;State q=read_state(&ok);diagnostic_game_state(q);
 MoonNet_SetWorldReady(ok&&q.game&&!q.loading&&q.bound);
 if(control_tick(q,now)){InterlockedExchange(&tick_busy,0);return;}
 if(!ok){transition("managed-error");write_state(q);InterlockedExchange(&tick_busy,0);return;}
 if(MoonNet_IsStopped()){transition("disconnected");write_state(q);InterlockedExchange(&tick_busy,0);return;}
 if(client_world_timeout_tick(q,now)){write_state(q);InterlockedExchange(&tick_busy,0);return;}
 if(!autostart){transition(MoonNet_IsClient()?(q.bound&&!q.loading?"client-world-bound":MoonNet_Phase()):"manual");write_state(q);InterlockedExchange(&tick_busy,0);return;}
 if(q.game&&!q.loading&&q.bound&&q.lobby&&q.host&&q.shift){last_error[0]=0;transition("world-bound");}
 else if(q.game&&q.lobby&&q.host)transition(q.loading?"loading":"world-scene");
 else if(q.lobby&&!q.host){error("Refusing to control a lobby owned by another account");transition("foreign-lobby");}
 else if(!q.steam)transition("waiting-steam");
 else if(!q.lobby&&!q.busy&&!q.loading&&now>=retry_at&&(q.menu||q.game)) {
  if(q.menu){void *args[]={&host_slot};call(methods.set_slot,NULL,args,&ok);}
  int capacity=16;void *args[]={&privacy,&capacity};call(methods.create,NULL,args,&ok);
  transition(ok?"creating-lobby":"managed-error");deadline=now+45000;retry_at=now+30000;direct_load=false;
 }
 else if(q.lobby&&q.host&&!q.busy&&q.menu&&strcmp(phase,"starting-world")!=0) {
  void *slot_args[]={&host_slot};call(methods.set_slot,NULL,slot_args,&ok);
  uint8_t yes=1;void *args[]={&yes};call(methods.set_ready,NULL,args,&ok);call(methods.start_shift,NULL,NULL,&ok);
  entered_start=now;deadline=now+180000;direct_load=false;transition(ok?"starting-world":"managed-error");
 }
 else if(q.menu&&q.lobby&&q.host&&q.shift&&!q.loading&&strcmp(phase,"starting-world")==0&&!direct_load&&now-entered_start>=3000) {
  direct_load=true;
  Object *scene=methods.game_scene_field?api.field_get_value_object(methods.game_scene_field,NULL):NULL;
  if(scene&&api.string_length(scene)>0&&api.string_length(scene)<256){call(methods.dismiss,NULL,NULL,&ok);void *args[]={scene};call(methods.load,NULL,args,&ok);note("Requested direct load using MoonSession.GameScene");}
  else {error("GameScene constant unavailable; direct-load fallback refused");transition("managed-error");}
 }
 else if(deadline&&now>deadline&&(!q.game||q.loading)) {transition("startup-timeout");}
 write_state(q);InterlockedExchange(&tick_busy,0);
}
static void __cdecl update_hook(Object *self,const Method *info) {if(!MoonNet_IsDedicated())original_update(self,info);if(installed){MoonNet_Tick();host_tick();}}
static void __cdecl world_initialize_hook(Object *self,const Method *info) {
 original_world_initialize(self,info);
 Object *progress=NULL,*regions=NULL;
 api.field_get_value(self,methods.world_progress_field,&progress);api.field_get_value(self,methods.world_regions_field,&regions);
 if(progress&&regions&&api.array_length(regions)>0){
  if(!initialized_world||api.gchandle_get_target(initialized_world)!=self){if(initialized_world)api.gchandle_free(initialized_world);initialized_world=api.gchandle_new(self,false);note("CPU world initialization completed");char details[96];snprintf(details,sizeof(details),"progress_store=1 region_count=%llu",(unsigned long long)api.array_length(regions));MoonNet_Diagnostic("world_initialized",details);}
 }
}
static bool __cdecl signed_hook(int slot,const Method *info) {return slot==host_slot?true:original_signed(slot,info);}
static bool __cdecl show_hook(Object *scene,Object *prelude,const Method *info) {return headless&&!prelude?false:original_show(scene,prelude,info);}
static void __cdecl noop_instance(Object *self,const Method *info) {(void)self;(void)info;}
static void __cdecl noop_static(const Method *info) {(void)info;}
static bool add_hook(const char *type,const char *name,int count,void *replacement,void **original) {
 const Method *m=method(type,name,count);void *code=method_code(m);
 if(!code){char text[160];snprintf(text,sizeof(text),"Invalid/missing executable method: %s.%s",type,name);error(text);return false;}
 MH_STATUS result=MH_CreateHook(code,replacement,original);
 if(result!=MH_OK&&result!=MH_ERROR_ALREADY_CREATED){error(MH_StatusToString(result));return false;}
 result=MH_EnableHook(code);if(result!=MH_OK&&result!=MH_ERROR_ENABLED){error(MH_StatusToString(result));return false;}
 return true;
}
static bool install_game_hooks(void) {
 if(installed)return true;
 if(install_failed)return false;
 if(InterlockedCompareExchange(&installing,1,0)!=0)return false;
 size_t count=0;const void **assemblies=api.domain_get_assemblies(api.domain_get(),&count);
 for(size_t i=0;i<count;i++){const void *image=api.assembly_get_image(assemblies[i]);if(strcmp(api.image_get_name(image),"Assembly-CSharp.dll")==0){game_image=image;break;}}
 if(!game_image){InterlockedExchange(&installing,0);return false;}
#define METHOD(member,type,name,argc) do{methods.member=method(type,name,argc);if(!method_code(methods.member)){error("Missing method " type "." name);InterlockedExchange(&installing,0);return false;}}while(0)
 METHOD(steam,"MoonSteam","get_Initialized",0);METHOD(menu,"MoonSession","get_InMenu",0);METHOD(game,"MoonSession","get_InGame",0);METHOD(loading,"MoonSession","get_Loading",0);
 METHOD(lobby,"MoonSteamLobby","get_InLobby",0);METHOD(host,"MoonSteamLobby","get_IsHost",0);METHOD(busy,"MoonSteamLobby","get_Busy",0);METHOD(shift,"MoonSteamLobby","get_ShiftStarted",0);
 METHOD(set_slot,"MoonSaveSlots","set_Active",1);METHOD(create,"MoonSteamLobby","Create",2);METHOD(set_ready,"MoonSteamLobby","SetReady",1);METHOD(start_shift,"MoonSteamLobby","StartShift",0);
 METHOD(load,"MoonSession","Load",1);METHOD(dismiss,"MoonNewHire","Dismiss",0);METHOD(crew_instance,"MoonCrewSession","get_Instance",0);
 METHOD(save_progress,"MoonWorld","SaveProgress",1);METHOD(quit_game,"MoonSession","QuitGame",0);
#undef METHOD
 methods.crew_bound_field=api.class_get_field_from_name(klass("MoonCrewSession"),"bound");
 methods.game_scene_field=api.class_get_field_from_name(klass("MoonSession"),"GameScene");
 methods.world_progress_field=api.class_get_field_from_name(klass("MoonWorld"),"progressStore");
 methods.world_regions_field=api.class_get_field_from_name(klass("MoonWorld"),"regionStates");
 if(!methods.crew_bound_field){error("MoonCrewSession.bound unavailable; readiness cannot be verified");InterlockedExchange(&installing,0);return false;}
 bool success=methods.world_progress_field&&methods.world_regions_field&&MoonNet_Install(game_image,note);
 if(success&&headless)success=add_hook("MoonMainMenu","OnEnable",0,noop_instance,NULL)&&add_hook("MoonMainMenu","Update",0,noop_instance,NULL)&&add_hook("MoonStudioIntro","Launch",0,noop_static,NULL);
 if(success)success=(!headless||moon_install_headless_graphics(game_module,note))&&add_hook("MoonNewHire","SignedFor",1,signed_hook,(void **)&original_signed)&&add_hook("MoonLoadingScreen","Show",2,show_hook,(void **)&original_show);
 if(success)success=add_hook("MoonSteam","Update",0,update_hook,(void **)&original_update);
 if(success)success=add_hook("MoonWorld","Initialize",0,world_initialize_hook,(void **)&original_world_initialize);
 if(success){InterlockedExchange(&installed,1);transition("waiting-steam");note("Game hooks installed; ordinary Steam callbacks remain active");}
 else {InterlockedExchange(&install_failed,1);transition("hook-error");write_state((State){0});}
 InterlockedExchange(&installing,0);return success;
}
static Object *__cdecl unity_runtime_hook(const Method *m,Object *self,void **args,Object **exception) {
 if(!installed&&!install_failed&&m){
  void *k=api.method_get_class(m);const char *ns=k?api.class_get_namespace(k):NULL;
  if(ns&&strcmp(ns,"MoonCleaners")==0)install_game_hooks();
 }
 return unity_runtime_invoke(m,self,args,exception);
}
static FARPROC WINAPI unity_resolve_hook(HMODULE module,LPCSTR name) {
 FARPROC address=unity_get_proc_address(module,name);
 if(address&&(uintptr_t)name>65535&&strcmp(name,"il2cpp_runtime_invoke")==0) {
  wchar_t path[MAX_PATH];DWORD length=GetModuleFileNameW(module,path,MAX_PATH);
  const wchar_t *leaf=length?wcsrchr(path,L'\\'):NULL;
  if(leaf&&_wcsicmp(leaf+1,L"GameAssembly.dll")==0) {
   game_module=module;
   if(resolve_api()) {
    if(!unity_runtime_patched){
     MH_STATUS status=MH_CreateHook((void *)address,unity_runtime_hook,(void **)&unity_runtime_invoke);
     if(status==MH_OK&&MH_EnableHook((void *)address)==MH_OK){
      InterlockedExchange(&unity_runtime_patched,1);note("Watching the original managed invocation entrypoint");
     }else error("Could not install the early managed invocation hook");
    }
   }
  }
 }
 return address;
}
static bool patch_unity_resolver(void) {
 HMODULE unity=GetModuleHandleW(L"UnityPlayer.dll");if(!unity)return false;
 unsigned char *base=(unsigned char *)unity;
 IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER *)base;
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
 IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64 *)(base+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
 DWORD rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
 if(!rva)return false;
 IMAGE_IMPORT_DESCRIPTOR *imports=(IMAGE_IMPORT_DESCRIPTOR *)(base+rva);
 for(;imports->Name;imports++) {
  if(!imports->OriginalFirstThunk)continue;
  IMAGE_THUNK_DATA64 *names=(IMAGE_THUNK_DATA64 *)(base+imports->OriginalFirstThunk),*slots=(IMAGE_THUNK_DATA64 *)(base+imports->FirstThunk);
  for(;names->u1.AddressOfData;names++,slots++) {
   if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
   IMAGE_IMPORT_BY_NAME *entry=(IMAGE_IMPORT_BY_NAME *)(base+names->u1.AddressOfData);
   if(strcmp((const char *)entry->Name,"GetProcAddress")!=0)continue;
   void *original=(void *)(uintptr_t)slots->u1.Function;
   memcpy(&unity_get_proc_address,&original,sizeof(original));
   DWORD old;if(!VirtualProtect(&slots->u1.Function,sizeof(void *),PAGE_READWRITE,&old))return false;
   FARPROC (WINAPI *replacement)(HMODULE,LPCSTR)=unity_resolve_hook;void *value;
   memcpy(&value,&replacement,sizeof(value));
   InterlockedExchangePointer((void *volatile *)&slots->u1.Function,value);
   DWORD ignored;VirtualProtect(&slots->u1.Function,sizeof(void *),old,&ignored);
   InterlockedExchange(&unity_import_patched,1);note("Watching Unity's IL2CPP resolver before game startup");return true;
  }
 }
 return false;
}
static DWORD WINAPI host_worker(void *unused) {
 (void)unused;
 if(!configure()){transition("configuration-error");write_state((State){0});return 0;}
 write_state((State){0});
 MH_STATUS mh=MH_Initialize();if(mh!=MH_OK&&mh!=MH_ERROR_ALREADY_INITIALIZED){error(MH_StatusToString(mh));return 0;}
 if(!patch_unity_resolver()){error("Unity IL2CPP resolver unavailable; refusing unsafe early VM access");transition("hook-error");write_state((State){0});return 0;}
 for(int i=0;i<12000&&!game_module;i++){game_module=GetModuleHandleW(L"GameAssembly.dll");if(!game_module)Sleep(10);}
 if(!game_module||!resolve_api()){error("GameAssembly initialization unavailable");return 0;}
 /* Resolving an export does not mean the VM is initialized. With Unity's
    invocation entrypoint watched, the first game invocation installs hooks on
    its own initialized thread. The loader worker must not query the domain. */
 for(int i=0;i<600&&!installed&&!install_failed;i++)Sleep(200);
 if(!installed&&!install_failed){error("No initialized game invocation observed");transition("hook-timeout");write_state((State){0});}
 return 0;
}
static uintptr_t WINAPI missing_version_export(void) {SetLastError(ERROR_PROC_NOT_FOUND);return 0;}
static bool load_real_version(HMODULE self) {
 wchar_t path[MAX_PATH];UINT n=GetSystemDirectoryW(path,MAX_PATH);
 if(!n||n+13>=MAX_PATH)return false;
 wcscat(path,L"\\version.dll");HMODULE real=LoadLibraryExW(path,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
 if(!real||real==self)return false;
#define RESOLVE_VERSION(name,index) g_version_procs[index]=GetProcAddress(real,#name);if(!g_version_procs[index])g_version_procs[index]=(FARPROC)missing_version_export;
 VERSION_EXPORTS(RESOLVE_VERSION)
#undef RESOLVE_VERSION
 return true;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
 (void)reserved;
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(instance);
  if(!load_real_version(instance))return FALSE;
  InitializeCriticalSection(&log_lock);
  if(opted_in()){HANDLE thread=CreateThread(NULL,0,host_worker,NULL,0,NULL);if(thread)CloseHandle(thread);}
 }
 return TRUE;
}
