#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"
#include "headless_graphics.h"

typedef void Obj;
typedef const void Method;
static struct {
 void *(__cdecl *domain_get)(void);
 const void **(__cdecl *domain_get_assemblies)(void *,size_t *);
 const void *(__cdecl *assembly_get_image)(const void *);
 const char *(__cdecl *image_get_name)(const void *);
 const void *(__cdecl *get_corlib)(void);
 void *(__cdecl *class_from_name)(const void *,const char *,const char *);
 void *(__cdecl *class_get_type)(void *);
 const Method *(__cdecl *class_get_methods)(void *,void **);
 const Method *(__cdecl *class_get_method_from_name)(void *,const char *,int);
 const char *(__cdecl *method_get_name)(const Method *);
 uint32_t (__cdecl *method_get_param_count)(const Method *);
 const void *(__cdecl *method_get_param)(const Method *,uint32_t);
 char *(__cdecl *type_get_name)(const void *);
 void (__cdecl *free)(void *);
 Obj *(__cdecl *type_get_object)(const void *);
 Obj *(__cdecl *method_get_object)(const Method *,void *);
 const Method *(__cdecl *method_get_from_reflection)(Obj *);
 void *(__cdecl *object_get_class)(Obj *);
 Obj *(__cdecl *array_new)(void *,uintptr_t);
 uint32_t (__cdecl *array_object_header_size)(void);
 void (__cdecl *gc_wbarrier_set_field)(Obj *,void **,Obj *);
 Obj *(__cdecl *runtime_invoke)(const Method *,Obj *,void **,Obj **);
 void (__cdecl *format_exception)(Obj *,char *,int);
} g;
static void (*logger)(const char *);
static void *texture_class;
static const Method *frame_rate, *vsync;
static void (__cdecl *original_name)(Obj *,Obj *,const Method *);
static void (__cdecl *original_filter)(Obj *,int,const Method *);
static void (__cdecl *original_wrap)(Obj *,int,const Method *);

static void say(const char *text) {if(logger)logger(text);}
static void *code(const Method *m) {
 if(!m)return NULL;
 void *p=*(void *const *)m;MEMORY_BASIC_INFORMATION info;
 if(!p||!VirtualQuery(p,&info,sizeof(info))||info.State!=MEM_COMMIT)return NULL;
 DWORD flags=info.Protect&0xff;
 return !(info.Protect&PAGE_GUARD)&&(flags==PAGE_EXECUTE||flags==PAGE_EXECUTE_READ||flags==PAGE_EXECUTE_READWRITE||flags==PAGE_EXECUTE_WRITECOPY)?p:NULL;
}
static bool hook(const Method *m,void *replacement,void **original,const char *description) {
 void *p=code(m);if(!p){say(description);say("Headless graphics: executable method unavailable");return false;}
 MH_STATUS result=MH_CreateHook(p,replacement,original);
 if(result!=MH_OK){say(description);say(MH_StatusToString(result));return false;}
 result=MH_EnableHook(p);if(result!=MH_OK){say(MH_StatusToString(result));return false;}
 return true;
}
/* All callers use the Windows x64 ABI: unused extra register/stack arguments are safe. */
static void __cdecl no_gpu(Obj *self,const Method *unused) {(void)self;(void)unused;}
static bool is_progress_texture(Obj *o) {return o&&g.object_get_class(o)==texture_class;}
static void __cdecl set_name(Obj *self,Obj *value,const Method *info) {if(!is_progress_texture(self))original_name(self,value,info);}
static void __cdecl set_filter(Obj *self,int value,const Method *info) {if(!is_progress_texture(self))original_filter(self,value,info);}
static void __cdecl set_wrap(Obj *self,int value,const Method *info) {if(!is_progress_texture(self))original_wrap(self,value,info);}

static const void *image_named(const char *name) {
 size_t count=0;const void **assemblies=g.domain_get_assemblies(g.domain_get(),&count);
 for(size_t i=0;i<count;i++){const void *image=g.assembly_get_image(assemblies[i]);if(strcmp(g.image_get_name(image),name)==0)return image;}
 return NULL;
}
static bool parameter_is(const Method *m,uint32_t index,const char *expected) {
 char *name=g.type_get_name(g.method_get_param(m,index));
 bool match=name&&strcmp(name,expected)==0;g.free(name);return match;
}
static const Method *find_texture_ctor(void) {
 void *iterator=NULL;const Method *m;
 while((m=g.class_get_methods(texture_class,&iterator))) {
  if(strcmp(g.method_get_name(m),".ctor")==0&&g.method_get_param_count(m)==6&&parameter_is(m,3,"UnityEngine.TextureFormat")&&parameter_is(m,4,"System.Boolean")&&parameter_is(m,5,"System.Boolean"))return m;
 }
 return NULL;
}
static const Method *float_pixel_method(void) {
 void *iterator=NULL;const Method *definition=NULL,*m;
 while((m=g.class_get_methods(texture_class,&iterator))) {
  if(strcmp(g.method_get_name(m),"SetPixelData")!=0||g.method_get_param_count(m)!=4)continue;
  char *type=g.type_get_name(g.method_get_param(m,0));
  bool array=type&&strstr(type,"[]")!=NULL;g.free(type);
  if(array){definition=m;break;}
 }
 if(!definition){say("Headless graphics: SetPixelData<T>(T[],...) definition missing");return NULL;}
 const void *corlib=g.get_corlib();void *single=g.class_from_name(corlib,"System","Single"),*type=g.class_from_name(corlib,"System","Type");
 if(!single||!type)return NULL;
 Obj *types=g.array_new(type,1),*single_type=g.type_get_object(g.class_get_type(single));
 if(!types||!single_type)return NULL;
 uint32_t header=g.array_object_header_size();
 if(header<2*sizeof(void *)||header>128)return NULL;
 void **element=(void **)((unsigned char *)types+header);
 g.gc_wbarrier_set_field(types,element,single_type);
 Obj *reflection=g.method_get_object(definition,NULL);if(!reflection)return NULL;
 const Method *make=g.class_get_method_from_name(g.object_get_class(reflection),"MakeGenericMethod",1);
 if(!make)return NULL;
 void *args[]={types};Obj *exception=NULL;
 Obj *instantiated=g.runtime_invoke(make,reflection,args,&exception);
 if(exception){char text[768]={0};g.format_exception(exception,text,sizeof(text));say(text);return NULL;}
 return instantiated?g.method_get_from_reflection(instantiated):NULL;
}
bool moon_install_headless_graphics(HMODULE module,void (*log_message)(const char *)) {
 logger=log_message;
#define API(name) do{FARPROC address=GetProcAddress(module,"il2cpp_" #name);memcpy(&g.name,&address,sizeof(address));if(!g.name){say("Missing il2cpp_" #name);return false;}}while(0)
 API(domain_get);API(domain_get_assemblies);API(assembly_get_image);API(image_get_name);API(get_corlib);
 API(class_from_name);API(class_get_type);API(class_get_methods);API(class_get_method_from_name);
 API(method_get_name);API(method_get_param_count);API(method_get_param);API(type_get_name);API(free);
 API(type_get_object);API(method_get_object);API(method_get_from_reflection);API(object_get_class);
 API(array_new);API(array_object_header_size);API(gc_wbarrier_set_field);API(runtime_invoke);API(format_exception);
#undef API
 const void *core=image_named("UnityEngine.CoreModule.dll"),*game=image_named("Assembly-CSharp.dll");
 if(!core||!game)return false;
 texture_class=g.class_from_name(core,"UnityEngine","Texture2DArray");
 void *texture=g.class_from_name(core,"UnityEngine","Texture"),*object=g.class_from_name(core,"UnityEngine","Object"),*world=g.class_from_name(game,"MoonCleaners","MoonWorld");
 if(!texture_class||!texture||!object||!world)return false;
 void *app=g.class_from_name(core,"UnityEngine","Application"),*quality=g.class_from_name(core,"UnityEngine","QualitySettings");
 if(app)frame_rate=g.class_get_method_from_name(app,"set_targetFrameRate",1);
 if(quality)vsync=g.class_get_method_from_name(quality,"set_vSyncCount",1);
 const Method *pixels=float_pixel_method();
 if(!pixels){say("Headless graphics: float pixel upload unavailable");return false;}
 bool ok=hook(find_texture_ctor(),no_gpu,NULL,"Texture2DArray constructor")&&
  hook(g.class_get_method_from_name(object,"set_name",1),set_name,(void **)&original_name,"Object.set_name")&&
  hook(g.class_get_method_from_name(texture,"set_filterMode",1),set_filter,(void **)&original_filter,"Texture.set_filterMode")&&
  hook(g.class_get_method_from_name(texture,"set_wrapMode",1),set_wrap,(void **)&original_wrap,"Texture.set_wrapMode")&&
  hook(pixels,no_gpu,NULL,"Texture2DArray.SetPixelData<float>")&&
  hook(g.class_get_method_from_name(texture_class,"Apply",2),no_gpu,NULL,"Texture2DArray.Apply")&&
  hook(g.class_get_method_from_name(world,"RestoreRenderingBindings",0),no_gpu,NULL,"MoonWorld.RestoreRenderingBindings");
 if(ok)say("Headless progress texture hooks installed; CPU progress initialization preserved");
 return ok;
}
void moon_headless_frame_limit(void) {
 if(!frame_rate)return;
 int frames=30,zero=0;Obj *exception=NULL;void *args[]={&frames};
 g.runtime_invoke(frame_rate,NULL,args,&exception);
 if(vsync){args[0]=&zero;g.runtime_invoke(vsync,NULL,args,&exception);}
}
