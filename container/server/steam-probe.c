/* Independent official Steam GameServer API feasibility test; contains no game code. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

typedef struct { int user, id; void *data; int size; } CallbackMsg;
typedef struct { int availability; char debug[256]; } AuthStatus;
typedef struct { int kind, size; unsigned char value[128]; } NetIdentity;
_Static_assert(sizeof(CallbackMsg)==24,"Win64 callback ABI");
_Static_assert(sizeof(AuthStatus)==260,"Steam auth ABI");
_Static_assert(sizeof(NetIdentity)==136,"Steam identity ABI");
typedef bool (__cdecl *InitFn)(uint32_t,uint16_t,uint16_t,uint16_t,int,const char*);
typedef void (__cdecl *VoidFn)(void);
typedef void* (__cdecl *GetterFn)(void);
typedef void (__cdecl *StringFn)(void*,const char*);
typedef void (__cdecl *BoolSetterFn)(void*,bool);
typedef void (__cdecl *SelfVoidFn)(void*);
typedef bool (__cdecl *SelfBoolFn)(void*);
typedef uint64_t (__cdecl *IDFn)(void*);
typedef int (__cdecl *PipeFn)(void);
typedef void (__cdecl *FrameFn)(int);
typedef bool (__cdecl *NextFn)(int,CallbackMsg*);
typedef int (__cdecl *InitAuthFn)(void*);
typedef int (__cdecl *AuthFn)(void*,AuthStatus*);
typedef bool (__cdecl *IdentityFn)(void*,NetIdentity*);
typedef uint32_t (__cdecl *ListenFn)(void*,int,int,void*);
typedef bool (__cdecl *CloseFn)(void*,uint32_t);

#define GET(name,type) type name=(type)GetProcAddress(api,#name); if(!name){fprintf(stderr,"Missing official SDK export: %s\n",#name);return 3;}
int main(int argc,char **argv) {
    if(argc!=3){fprintf(stderr,"steam-probe.exe STEAM_API_DLL REDIST_DIRECTORY\n");return 2;}
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetEnvironmentVariableA("SteamAppId","5335510");
    SetEnvironmentVariableA("SteamGameId","5335510");
    SetDllDirectoryA(argv[2]);
    HMODULE api=LoadLibraryA(argv[1]);
    if(!api){fprintf(stderr,"Steam API DLL load failed: Windows error %lu\n",GetLastError());return 3;}
    GET(SteamInternal_GameServer_Init,InitFn);
    GET(SteamGameServer_Shutdown,VoidFn);
    GET(SteamAPI_SteamGameServer_v015,GetterFn);
    GET(SteamAPI_ISteamGameServer_SetProduct,StringFn);
    GET(SteamAPI_ISteamGameServer_SetGameDescription,StringFn);
    GET(SteamAPI_ISteamGameServer_SetModDir,StringFn);
    GET(SteamAPI_ISteamGameServer_SetDedicatedServer,BoolSetterFn);
    GET(SteamAPI_ISteamGameServer_SetAdvertiseServerActive,BoolSetterFn);
    GET(SteamAPI_ISteamGameServer_LogOnAnonymous,SelfVoidFn);
    GET(SteamAPI_ISteamGameServer_LogOff,SelfVoidFn);
    GET(SteamAPI_ISteamGameServer_BLoggedOn,SelfBoolFn);
    GET(SteamAPI_ISteamGameServer_GetSteamID,IDFn);
    GET(SteamGameServer_GetHSteamPipe,PipeFn);
    GET(SteamAPI_ManualDispatch_Init,VoidFn);
    GET(SteamAPI_ManualDispatch_RunFrame,FrameFn);
    GET(SteamAPI_ManualDispatch_GetNextCallback,NextFn);
    GET(SteamAPI_ManualDispatch_FreeLastCallback,FrameFn);
    GET(SteamAPI_SteamGameServerNetworkingSockets_SteamAPI_v012,GetterFn);
    GET(SteamAPI_ISteamNetworkingSockets_InitAuthentication,InitAuthFn);
    GET(SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus,AuthFn);
    GET(SteamAPI_ISteamNetworkingSockets_GetIdentity,IdentityFn);
    GET(SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P,ListenFn);
    GET(SteamAPI_ISteamNetworkingSockets_CloseListenSocket,CloseFn);
    bool initialized=SteamInternal_GameServer_Init(0,0,27035,27036,2,"1.0.5");
    printf("{\"initialized\":%s,\"appid\":5335510}\n",initialized?"true":"false");fflush(stdout);
    if(!initialized)return 4;
    const char *libraries[]={"steamclient64.dll","tier0_s64.dll","vstdlib_s64.dll","crashhandler64.dll"};
    for(size_t i=0;i<4;i++){HMODULE h=GetModuleHandleA(libraries[i]);char path[4096];if(h&&GetModuleFileNameA(h,path,sizeof(path)))printf("server_library=%s\n",path);}
    void *server=SteamAPI_SteamGameServer_v015();
    if(!server){SteamGameServer_Shutdown();return 4;}
    SteamAPI_ISteamGameServer_SetProduct(server,"5335510");
    SteamAPI_ISteamGameServer_SetGameDescription(server,"Moon Sweepers");
    SteamAPI_ISteamGameServer_SetModDir(server,"Moon Sweepers");
    SteamAPI_ISteamGameServer_SetDedicatedServer(server,true);
    SteamAPI_ISteamGameServer_SetAdvertiseServerActive(server,false);
    SteamAPI_ManualDispatch_Init();
    int pipe=SteamGameServer_GetHSteamPipe();
    SteamAPI_ISteamGameServer_LogOnAnonymous(server);
    void *sockets=SteamAPI_SteamGameServerNetworkingSockets_SteamAPI_v012();
    DWORD started=GetTickCount();bool auth_started=false,success=false,relay_ready=false;int previous=-999;uint32_t listener=0;
    while((DWORD)(GetTickCount()-started)<45000){
        SteamAPI_ManualDispatch_RunFrame(pipe);
        CallbackMsg cb;
        while(SteamAPI_ManualDispatch_GetNextCallback(pipe,&cb)){
            if(cb.id==101||cb.id==102||cb.id==103){printf("{\"callback\":%d}",cb.id);if(cb.size>=4&&cb.id!=101){int result;memcpy(&result,cb.data,4);printf(" result=%d",result);}puts("");}
            if(cb.id==1281&&cb.size>=16){
                const unsigned char *data=(const unsigned char*)cb.data;
                int availability,config,relays;
                memcpy(&availability,data,4);memcpy(&config,data+8,4);memcpy(&relays,data+12,4);
                relay_ready=availability==100&&config==100&&relays==100&&!data[4];
                printf("relay_status=%d config=%d relays=%d measuring=%d\n",availability,config,relays,data[4]?1:0);
            }
            SteamAPI_ManualDispatch_FreeLastCallback(pipe);
        }
        if(SteamAPI_ISteamGameServer_BLoggedOn(server)){
            if(!auth_started){SteamAPI_ISteamNetworkingSockets_InitAuthentication(sockets);auth_started=true;}
            AuthStatus status;memset(&status,0,sizeof(status));
            int availability=SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus(sockets,&status);
            if(availability!=previous){printf("network_authentication=%d %s\n",availability,status.debug);previous=availability;}
            if(!listener)listener=SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P(sockets,79,0,NULL);
            NetIdentity identity;memset(&identity,0,sizeof(identity));
            bool identity_valid=SteamAPI_ISteamNetworkingSockets_GetIdentity(sockets,&identity);
            uint64_t id=SteamAPI_ISteamGameServer_GetSteamID(server);
            if(!success&&availability==100&&listener&&identity_valid&&((id>>52)&15)==4){
                printf("{\"anonymous_logged_on\":true,\"server_steam_id\":\"%" PRIu64 "\",\"account_type\":4,\"authentication\":100,\"listen_socket_valid\":true}\n",id);
                success=true;
            }
            if(success&&relay_ready)break;
        }
        fflush(stdout);Sleep(50);
    }
    success=success&&relay_ready;
    if(listener)SteamAPI_ISteamNetworkingSockets_CloseListenSocket(sockets,listener);
    SteamAPI_ISteamGameServer_LogOff(server);
    for(int i=0;i<10;i++){SteamAPI_ManualDispatch_RunFrame(pipe);Sleep(50);}
    SteamGameServer_Shutdown();
    puts(success?"PASS: official anonymous GameServer networking under Wine":"FAIL: anonymous server networking did not become ready");
    return success?0:5;
}
