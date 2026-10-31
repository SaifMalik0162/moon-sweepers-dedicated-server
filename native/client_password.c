#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "client_password.h"

static volatile LONG password_state;
static char password[129];
static HWND password_edit,password_label;
static HINSTANCE password_instance;

static bool valid_password(const char *value) {
 size_t length=strlen(value);if(length<8||length>128)return false;
 for(size_t i=0;i<length;i++)if((unsigned char)value[i]<32||(unsigned char)value[i]>126)return false;
 return true;
}
static void control_font(HWND control) {SendMessageW(control,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);}
static LRESULT CALLBACK password_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
 switch(message) {
 case WM_CREATE:
  password_label=CreateWindowExW(0,L"STATIC",L"Enter the Moon Sweepers server password:",WS_CHILD|WS_VISIBLE,20,18,370,22,window,(HMENU)(uintptr_t)1003,password_instance,NULL);
  password_edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_PASSWORD|ES_AUTOHSCROLL,20,48,370,27,window,(HMENU)(uintptr_t)1001,password_instance,NULL);
  control_font(password_label);control_font(password_edit);SendMessageW(password_edit,EM_SETLIMITTEXT,128,0);
  control_font(CreateWindowExW(0,L"BUTTON",L"Join",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,202,91,90,28,window,(HMENU)(uintptr_t)IDOK,password_instance,NULL));
  control_font(CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,300,91,90,28,window,(HMENU)(uintptr_t)IDCANCEL,password_instance,NULL));
  return 0;
 case WM_COMMAND:
  if(LOWORD(wparam)==IDOK) {
   wchar_t input[129]={0};char encoded[129]={0};GetWindowTextW(password_edit,input,129);
   bool converted=WideCharToMultiByte(CP_UTF8,0,input,-1,encoded,sizeof(encoded),NULL,NULL)>0;
   SecureZeroMemory(input,sizeof(input));
   if(!converted||!valid_password(encoded)){SetWindowTextW(password_label,L"Use 8-128 characters, using ordinary ASCII text.");SecureZeroMemory(encoded,sizeof(encoded));SetFocus(password_edit);return 0;}
   memcpy(password,encoded,sizeof(password));SecureZeroMemory(encoded,sizeof(encoded));
   SetWindowTextW(password_edit,L"");InterlockedExchange(&password_state,2);DestroyWindow(window);return 0;
  }
  if(LOWORD(wparam)==IDCANCEL){InterlockedExchange(&password_state,-1);DestroyWindow(window);return 0;}
  break;
 case WM_CLOSE:InterlockedExchange(&password_state,-1);DestroyWindow(window);return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(window,message,wparam,lparam);
}
static DWORD WINAPI password_worker(void *unused) {
 (void)unused;
 HMODULE module=NULL;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)(uintptr_t)&MoonPassword_Read,&module);password_instance=module;
 WNDCLASSW wc={0};wc.lpfnWndProc=password_proc;wc.hInstance=module;wc.lpszClassName=L"MoonSweepersServerPassword";wc.hCursor=LoadCursorW(NULL,MAKEINTRESOURCEW(32512));wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
 if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){InterlockedExchange(&password_state,-1);return 0;}
 int x=(GetSystemMetrics(SM_CXSCREEN)-430)/2,y=(GetSystemMetrics(SM_CYSCREEN)-180)/2;
 HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_DLGMODALFRAME,wc.lpszClassName,L"Moon Sweepers - Server password",WS_CAPTION|WS_SYSMENU|WS_POPUP,x,y,430,180,NULL,NULL,module,NULL);
 if(!window){InterlockedExchange(&password_state,-1);return 0;}
 ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(password_edit);
 MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 if(password_state==1)InterlockedExchange(&password_state,-1);
 return 0;
}
int MoonPassword_Read(char *output,size_t capacity) {
 if(!output||capacity<129)return -1;
 if(InterlockedCompareExchange(&password_state,1,0)==0) {
  char value[129]={0};DWORD length=GetEnvironmentVariableA("MOON_JOIN_PASSWORD",value,sizeof(value));
  if(length){if(length<sizeof(value)&&valid_password(value)){memcpy(password,value,sizeof(password));InterlockedExchange(&password_state,2);}else InterlockedExchange(&password_state,-1);SecureZeroMemory(value,sizeof(value));}
  else {HANDLE thread=CreateThread(NULL,0,password_worker,NULL,0,NULL);if(thread)CloseHandle(thread);else InterlockedExchange(&password_state,-1);}
 }
 LONG state=InterlockedCompareExchange(&password_state,0,0);
 if(state==2){memcpy(output,password,sizeof(password));return 1;}
 return state<0?-1:0;
}
static DWORD WINAPI rejected_worker(void *unused) {
 (void)unused;
 MessageBoxW(NULL,L"The server password was incorrect. Close Moon Sweepers and launch the connection again to retry.",L"Moon Sweepers - Incorrect server password",MB_OK|MB_ICONWARNING|MB_TOPMOST);
 return 0;
}
void MoonPassword_NotifyRejected(void) {
 SecureZeroMemory(password,sizeof(password));
 HANDLE thread=CreateThread(NULL,0,rejected_worker,NULL,0,NULL);if(thread)CloseHandle(thread);
}
