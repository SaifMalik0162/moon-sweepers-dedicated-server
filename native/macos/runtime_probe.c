#define _DARWIN_C_SOURCE
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <errno.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "client_network.h"
#include "mono_game_bridge.h"

#if !defined(__APPLE__) || !defined(__arm64__)
#error "The verified Moon Sweepers macOS build requires Apple Silicon arm64."
#endif

#define EXPECTED_EXE_SHA256 "aec13ede1ca057b175de1ff00037153a264141846815682d86284bf935f50355"
#define EXPECTED_UNITY_SHA256 "f38328449c849c25b0e78aa489d1f1fff0f61e0d618fdd611208db93b1472e3b"
#define EXPECTED_GAME_SHA256 "75c93a9a74eea70d06c48d6c512b72a1fe6fc4f26f8fd2e1e3030ee48e27c0ae"
#define EXPECTED_STEAM_SHA256 "a11755aeb0d5bb020dac630d21103d318a2be55957e38ea9fd8112d7e689859e"

typedef struct _MonoImage MonoImage;
typedef struct _MonoClass MonoClass;
typedef struct _MonoMethod MonoMethod;
typedef struct _MonoMethodSignature MonoSignature;
typedef struct _MonoObject MonoObject;
typedef struct { int32_t user; int32_t id; void *data; int32_t size; } SteamCallbackMessage;

typedef struct {
    const char *(*image_get_name)(MonoImage *image);
    MonoImage *(*class_get_image)(MonoClass *klass);
    MonoClass *(*class_from_name)(MonoImage *image, const char *name_space, const char *name);
    MonoMethod *(*class_get_method_from_name)(MonoClass *klass, const char *name, int parameter_count);
    MonoClass *(*method_get_class)(MonoMethod *method);
    const char *(*method_get_name)(MonoMethod *method);
    const char *(*class_get_name)(MonoClass *klass);
    const char *(*class_get_namespace)(MonoClass *klass);
    MonoSignature *(*method_signature)(MonoMethod *method);
    uint32_t (*signature_get_param_count)(MonoSignature *signature);
} MonoApi;

static char executable_path[4096];
static char app_root[4096];
static char log_path[4096];
static bool compatible_build;
static MonoApi mono;
static void log_line(const char *format, ...);
static void client_log(const char *message);
static bool resolve_mono_api(void);
static void inspect_game_assembly(MonoImage *image);
static void inspect_steam_api_handle(void *library);
static bool patch_loader_image(const struct mach_header_64 *header, intptr_t slide, const char *image_name);
static bool patch_loaded_image(const char *leaf);
static bool observe_steam_callback(int32_t pipe, SteamCallbackMessage *message);
static void *(*real_dlsym)(void *handle, const char *symbol);
static void *(*real_dlopen)(const char *path, int mode);
static bool (*real_steam_next_callback)(int32_t pipe, SteamCallbackMessage *message);
static MonoObject *(*real_runtime_invoke)(MonoMethod *method, void *instance, void **parameters, MonoObject **exception);
static volatile int observed_game_invocation;
static volatile int steam_api_reported;

static MonoObject *observe_runtime_invoke(MonoMethod *method, void *instance, void **parameters, MonoObject **exception) {
    MonoObject *result = real_runtime_invoke(method, instance, parameters, exception);
    if (!__atomic_load_n(&observed_game_invocation, __ATOMIC_RELAXED) && mono.method_get_class &&
        mono.class_get_namespace && mono.class_get_name && mono.class_get_image && mono.image_get_name) {
        MonoClass *klass = mono.method_get_class(method);
        const char *name_space = klass ? mono.class_get_namespace(klass) : NULL;
        if (name_space && strcmp(name_space, "MoonCleaners") == 0 &&
            __atomic_exchange_n(&observed_game_invocation, 1, __ATOMIC_RELAXED) == 0) {
            log_line("runtime_hook observed=mono_runtime_invoke first_game_method=%s.%s.%s",
                     name_space, mono.class_get_name(klass), mono.method_get_name(method));
            MonoImage *image = mono.class_get_image(klass);
            inspect_game_assembly(image);
            if (!MoonMac_GameBridgeInitialize(image, client_log))
                log_line("mono_bridge initialized=no");
        }
    }
    if (MoonMac_IsMoonSteamUpdate(method)) MoonMac_GameBridgeUpdate();
    return result;
}

static void *observe_dlsym(void *handle, const char *symbol) {
    if (!real_dlsym) return NULL;
    void *result = real_dlsym(handle, symbol);
    if (compatible_build && symbol && strcmp(symbol, "mono_runtime_invoke") == 0 && result) {
        memcpy(&real_runtime_invoke, &result, sizeof(result));
        bool mono_ready = resolve_mono_api();
        void *replacement = (void *)&observe_runtime_invoke;
        log_line("runtime_hook installed=mono_runtime_invoke source=UnityPlayer.dlsym mono_api=%s",
                 mono_ready ? "ready" : "partial");
        return replacement;
    }
    if (compatible_build && symbol && strcmp(symbol, "SteamAPI_ManualDispatch_GetNextCallback") == 0 && result) {
        memcpy(&real_steam_next_callback, &result, sizeof(result));
        log_line("runtime_hook installed=SteamAPI_ManualDispatch_GetNextCallback");
        return (void *)&observe_steam_callback;
    }
    return result;
}

static bool observe_steam_callback(int32_t pipe, SteamCallbackMessage *message) {
    if (!real_steam_next_callback) return false;
    bool available = real_steam_next_callback(pipe, message);
    if (available && message) MoonMac_OnSteamCallback(message->id, message->size, message->data);
    if (available && message && message->id == 163 && message->size >= 8) {
        int32_t response = 0;
        memcpy(&response, (const uint8_t *)message->data + sizeof(uint32_t), sizeof(response));
        log_line("steam_callback id=163 bytes=%d response=%d", message->size, response);
    }
    return available;
}

static void *observe_dlopen(const char *path, int mode) {
    if (!real_dlopen) return NULL;
    void *library = real_dlopen(path, mode);
    if (path && strstr(path, "libmonobdwgc-2.0.dylib") && library) {
        (void)patch_loaded_image("libmonobdwgc-2.0.dylib");
    }
    if (path && (strstr(path, "steam_api.bundle") || strstr(path, "libsteam_api"))) {
        if (library) {
            log_line("steam_api load=success");
            inspect_steam_api_handle(library);
            MoonMac_OnSteamLibraryLoaded(library);
        }
        else log_line("steam_api load=failed");
    }
    return library;
}

static bool patch_loader_image(const struct mach_header_64 *header, intptr_t slide, const char *image_name) {
    if (!header || header->magic != MH_MAGIC_64) return false;
    const struct symtab_command *symbols_command = NULL;
    const struct dysymtab_command *dynamic_symbols_command = NULL;
    const struct segment_command_64 *linkedit = NULL;
    const struct load_command *command = (const struct load_command *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (command->cmd == LC_SYMTAB) symbols_command = (const struct symtab_command *)command;
        else if (command->cmd == LC_DYSYMTAB) dynamic_symbols_command = (const struct dysymtab_command *)command;
        else if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (const struct segment_command_64 *)command;
            if (strncmp(segment->segname, "__LINKEDIT", sizeof(segment->segname)) == 0) linkedit = segment;
        }
        command = (const struct load_command *)((const uint8_t *)command + command->cmdsize);
    }
    if (!symbols_command || !dynamic_symbols_command || !linkedit) return false;
    uintptr_t linkedit_base = (uintptr_t)slide + (uintptr_t)linkedit->vmaddr - (uintptr_t)linkedit->fileoff;
    const struct nlist_64 *symbols = (const struct nlist_64 *)(linkedit_base + symbols_command->symoff);
    const char *strings = (const char *)(linkedit_base + symbols_command->stroff);
    const uint32_t *indirect = (const uint32_t *)(linkedit_base + dynamic_symbols_command->indirectsymoff);
    const struct load_command *section_command = (const struct load_command *)(header + 1);
    bool patched = false;
    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (section_command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (const struct segment_command_64 *)section_command;
            const struct section_64 *sections = (const struct section_64 *)(segment + 1);
            for (uint32_t s = 0; s < segment->nsects; s++) {
                uint32_t type = sections[s].flags & SECTION_TYPE;
                if (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS) continue;
                size_t pointer_count = (size_t)(sections[s].size / sizeof(uintptr_t));
                for (size_t p = 0; p < pointer_count; p++) {
                    size_t indirect_index = (size_t)sections[s].reserved1 + p;
                    if (indirect_index >= dynamic_symbols_command->nindirectsyms) continue;
                    uint32_t symbol_index = indirect[indirect_index];
                    if (symbol_index == INDIRECT_SYMBOL_ABS || symbol_index == INDIRECT_SYMBOL_LOCAL ||
                        symbol_index >= symbols_command->nsyms) continue;
                    uint32_t string_index = symbols[symbol_index].n_un.n_strx;
                    if (string_index >= symbols_command->strsize) continue;
                    const char *name = strings + string_index;
                    bool hook_dlsym = strcmp(name, "_dlsym") == 0;
                    bool hook_dlopen = strcmp(name, "_dlopen") == 0;
                    if (!hook_dlsym && !hook_dlopen) continue;
                    void **slot = (void **)((uintptr_t)slide + sections[s].addr + p * sizeof(uintptr_t));
                    if (hook_dlsym && !real_dlsym) memcpy(&real_dlsym, slot, sizeof(real_dlsym));
                    if (hook_dlopen && !real_dlopen) memcpy(&real_dlopen, slot, sizeof(real_dlopen));
                    if ((hook_dlsym && !real_dlsym) || (hook_dlopen && !real_dlopen)) continue;
                    vm_address_t page = (vm_address_t)((uintptr_t)slot & ~((uintptr_t)vm_page_size - 1));
                    if (vm_protect(mach_task_self(), page, vm_page_size, false,
                                   VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) continue;
                    *slot = hook_dlsym ? (void *)&observe_dlsym : (void *)&observe_dlopen;
                    vm_prot_t restore = strncmp(sections[s].segname, "__DATA_CONST", sizeof(sections[s].segname)) == 0
                        ? VM_PROT_READ : VM_PROT_READ | VM_PROT_WRITE;
                    (void)vm_protect(mach_task_self(), page, vm_page_size, false, restore);
                    patched = true;
                }
            }
        }
        section_command = (const struct load_command *)((const uint8_t *)section_command + section_command->cmdsize);
    }
    if (patched) log_line("runtime_hook loader_imports=dlopen,dlsym image=%s", image_name);
    return patched;
}

static bool patch_loaded_image(const char *leaf) {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *path = _dyld_get_image_name(i);
        const char *image_leaf = path ? strrchr(path, '/') : NULL;
        image_leaf = image_leaf ? image_leaf + 1 : path;
        if (!path || !image_leaf || strcmp(image_leaf, leaf) != 0 ||
            strncmp(path, app_root, strlen(app_root)) != 0) continue;
        return patch_loader_image((const struct mach_header_64 *)_dyld_get_image_header(i),
                                  _dyld_get_image_vmaddr_slide(i), image_leaf);
    }
    return false;
}

static bool patch_unity_resolver(void) {
    uint32_t count = _dyld_image_count();
    bool unity_hooked = false;
    bool mono_hooked = false;
    for (uint32_t i = 0; i < count; i++) {
        const char *path = _dyld_get_image_name(i);
        if (!path || strncmp(path, app_root, strlen(app_root)) != 0) continue;
        const char *leaf = strrchr(path, '/');
        leaf = leaf ? leaf + 1 : path;
        bool is_unity = strcmp(leaf, "UnityPlayer.dylib") == 0;
        bool is_mono = strcmp(leaf, "libmonobdwgc-2.0.dylib") == 0;
        if (!is_unity && !is_mono) continue;
        bool ok = patch_loader_image((const struct mach_header_64 *)_dyld_get_image_header(i),
                                     _dyld_get_image_vmaddr_slide(i), leaf);
        if (is_unity) unity_hooked = ok;
        if (is_mono) mono_hooked = ok;
    }
    log_line("runtime_hook resolver unity=%s mono=%s",
             unity_hooked ? "installed" : "unavailable", mono_hooked ? "installed" : "unavailable");
    return unity_hooked;
}

static void ensure_log_directory(void) {
    const char *home = getenv("HOME");
    if (!home || !*home) return;
    char directory[4096];
    int n = snprintf(directory, sizeof(directory), "%s/Library/Logs/DragDropGames/MoonSweepers", home);
    if (n <= 0 || (size_t)n >= sizeof(directory)) return;
    char *cursor = directory + strlen(home) + 1;
    for (; *cursor; cursor++) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        (void)mkdir(directory, 0755);
        *cursor = '/';
    }
    (void)mkdir(directory, 0755);
    (void)snprintf(log_path, sizeof(log_path), "%s/moon-macos-adapter.log", directory);
}

static void log_line(const char *format, ...) {
    if (!log_path[0]) ensure_log_directory();
    if (!log_path[0]) return;
    FILE *file = fopen(log_path, "a");
    if (!file) return;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm utc;
    gmtime_r(&now.tv_sec, &utc);
    fprintf(file, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ pid=%d ",
            utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
            utc.tm_hour, utc.tm_min, utc.tm_sec, now.tv_nsec / 1000000L, getpid());
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
}

static void client_log(const char *message) {
    if (message) log_line("%s", message);
}

static bool file_sha256(const char *path, char hex[CC_SHA256_DIGEST_LENGTH * 2 + 1]) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    unsigned char buffer[65536];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) CC_SHA256_Update(&context, buffer, (CC_LONG)count);
    bool ok = !ferror(file);
    fclose(file);
    if (!ok) return false;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    for (size_t i = 0; i < sizeof(digest); i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[sizeof(digest) * 2] = '\0';
    return true;
}

static bool hash_matches(const char *path, const char *expected, const char *label) {
    char actual[CC_SHA256_DIGEST_LENGTH * 2 + 1];
    if (!file_sha256(path, actual)) {
        log_line("fingerprint unavailable file=%s errno=%d", label, errno);
        return false;
    }
    bool match = strcmp(actual, expected) == 0;
    log_line("fingerprint file=%s sha256=%s match=%s", label, actual, match ? "yes" : "no");
    return match;
}

static bool locate_app(void) {
    uint32_t size = sizeof(executable_path);
    if (_NSGetExecutablePath(executable_path, &size) != 0) return false;
    char *marker = strstr(executable_path, ".app/Contents/MacOS/");
    if (!marker) return false;
    size_t root_length = (size_t)(marker - executable_path) + strlen(".app");
    if (root_length >= sizeof(app_root)) return false;
    memcpy(app_root, executable_path, root_length);
    app_root[root_length] = '\0';
    return true;
}

static bool verify_supported_app(void) {
    char path[4096];
    bool ok = true;
    ok &= hash_matches(executable_path, EXPECTED_EXE_SHA256, "MoonSweepers");
    snprintf(path, sizeof(path), "%s/Contents/Frameworks/UnityPlayer.dylib", app_root);
    ok &= hash_matches(path, EXPECTED_UNITY_SHA256, "UnityPlayer.dylib");
    snprintf(path, sizeof(path), "%s/Contents/Resources/Data/Managed/Assembly-CSharp.dll", app_root);
    ok &= hash_matches(path, EXPECTED_GAME_SHA256, "Assembly-CSharp.dll");
    snprintf(path, sizeof(path), "%s/Contents/PlugIns/steam_api.bundle/Contents/MacOS/libsteam_api.dylib", app_root);
    ok &= hash_matches(path, EXPECTED_STEAM_SHA256, "libsteam_api.dylib");
    return ok;
}

static bool resolve_mono_api(void) {
#define RESOLVE(member, symbol) do { \
    void *address = dlsym(RTLD_DEFAULT, symbol); \
    memcpy(&mono.member, &address, sizeof(address)); \
    if (!mono.member) return false; \
} while (0)
    RESOLVE(image_get_name, "mono_image_get_name");
    RESOLVE(class_get_image, "mono_class_get_image");
    RESOLVE(class_from_name, "mono_class_from_name");
    RESOLVE(class_get_method_from_name, "mono_class_get_method_from_name");
    RESOLVE(method_get_class, "mono_method_get_class");
    RESOLVE(method_get_name, "mono_method_get_name");
    RESOLVE(class_get_name, "mono_class_get_name");
    RESOLVE(class_get_namespace, "mono_class_get_namespace");
    RESOLVE(method_signature, "mono_method_signature");
    RESOLVE(signature_get_param_count, "mono_signature_get_param_count");
#undef RESOLVE
    return true;
}

static void describe_method(MonoClass *klass, const char *type, const char *method_name, int parameter_count) {
    MonoMethod *method = mono.class_get_method_from_name(klass, method_name, parameter_count);
    if (!method) {
        log_line("managed_method type=%s method=%s/%d present=no", type, method_name, parameter_count);
        return;
    }
    MonoSignature *signature = mono.method_signature(method);
    uint32_t actual_count = signature ? mono.signature_get_param_count(signature) : UINT32_MAX;
    log_line("managed_method type=%s method=%s/%d present=yes", type, mono.method_get_name(method), actual_count);
}

static void inspect_steam_api_handle(void *library) {
    if (!library || __atomic_exchange_n(&steam_api_reported, 1, __ATOMIC_RELAXED)) return;
    static const char *symbols[] = {
        "SteamAPI_Init",
        "SteamAPI_RunCallbacks",
        "SteamAPI_SteamUser_v023",
        "SteamAPI_ISteamUser_GetSteamID",
        "SteamAPI_ISteamUser_GetAuthSessionTicket",
        "SteamAPI_ISteamUser_CancelAuthTicket",
        "SteamAPI_ManualDispatch_GetNextCallback",
        "SteamAPI_SteamNetworkingMessages_SteamAPI_v002",
        "SteamAPI_ISteamNetworkingMessages_SendMessageToUser",
        "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel",
        "SteamAPI_SteamNetworkingMessage_t_Release"
    };
    log_line("steam_api loaded=yes");
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
        log_line("steam_api_symbol name=%s present=%s", symbols[i], dlsym(library, symbols[i]) ? "yes" : "no");
    }
}

static void inspect_game_assembly(MonoImage *image) {
    const char *image_name = image ? mono.image_get_name(image) : NULL;
    if (!image) {
        log_line("runtime_probe game_image=unavailable");
        return;
    }
    log_line("managed_assembly name=%s", image_name ? image_name : "unknown");
    struct { const char *name; const char *method; int parameters; } targets[] = {
        {"MoonSteam", "Update", 0},
        {"MoonSteam", "get_Initialized", 0},
        {"MoonSteamLobby", "get_InLobby", 0},
        {"MoonSteamLobby", "get_IsHost", 0},
        {"MoonCrewSession", "BeginJoin", 1},
        {"MoonCrewSession", "SendBytes", 5},
        {"MoonCrewSession", "ReceiveTransfer", 2},
        {"MoonCrewSession", "ReceiveWorld", 2}
    };
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        MonoClass *klass = mono.class_from_name(image, "MoonCleaners", targets[i].name);
        if (!klass) {
            log_line("managed_type name=%s present=no", targets[i].name);
            continue;
        }
        describe_method(klass, targets[i].name, targets[i].method, targets[i].parameters);
    }
    log_line("runtime_probe complete build=1.0.5 steam_build=26317497 runtime=mono");
}

__attribute__((constructor)) static void moon_macos_adapter_load(void) {
    ensure_log_directory();
    if (!locate_app()) {
        log_line("adapter loaded executable=%s target=no", executable_path[0] ? executable_path : "unknown");
        return;
    }
    log_line("adapter loaded executable=%s app=%s", executable_path, app_root);
    compatible_build = verify_supported_app();
    if (!compatible_build) {
        log_line("adapter disabled reason=unsupported-or-modified-build");
        return;
    }
    MoonMac_SetLogger(client_log);
    MoonMac_Configure(MoonMac_ServerIdFromArgs());
    (void)patch_unity_resolver();
}

__attribute__((destructor)) static void moon_macos_adapter_unload(void) {
    MoonMac_Shutdown();
}
