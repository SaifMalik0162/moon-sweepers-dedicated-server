#include "mono_game_bridge.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct _MonoDomain MonoDomain;
typedef struct _MonoClass MonoClass;
typedef struct _MonoClassField MonoClassField;
typedef struct _MonoObject MonoObject;
typedef struct _MonoString MonoString;
typedef struct _MonoVTable MonoVTable;

typedef struct {
    MonoDomain *(*domain_get)(void);
    MonoDomain *(*thread_attach)(MonoDomain *domain);
    MonoClass *(*class_from_name)(MonoImage *image, const char *name_space, const char *name);
    MonoClass *(*class_get_nested_types)(MonoClass *klass, void **iterator);
    const char *(*class_get_name)(MonoClass *klass);
    MonoVTable *(*class_vtable)(MonoDomain *domain, MonoClass *klass);
    void (*runtime_class_init)(MonoVTable *vtable);
    MonoClassField *(*class_get_field_from_name)(MonoClass *klass, const char *name);
    MonoMethod *(*class_get_method_from_name)(MonoClass *klass, const char *name, int count);
    MonoClass *(*object_get_class)(MonoObject *object);
    MonoObject *(*object_new)(MonoDomain *domain, MonoClass *klass);
    MonoString *(*string_new)(MonoDomain *domain, const char *value);
    MonoObject *(*runtime_invoke)(MonoMethod *method, MonoObject *object, void **args, MonoObject **exception);
    void *(*object_unbox)(MonoObject *object);
    void (*field_static_get_value)(MonoVTable *vtable, MonoClassField *field, void *value);
    void (*field_static_set_value)(MonoVTable *vtable, MonoClassField *field, void *value);
    void (*field_set_value)(MonoObject *object, MonoClassField *field, void *value);
    MonoClass *(*method_get_class)(MonoMethod *method);
    const char *(*method_get_name)(MonoMethod *method);
    const char *(*class_get_namespace)(MonoClass *klass);
} MonoBridgeApi;

typedef struct {
    MonoClass *lobby;
    MonoClass *member;
    MonoClass *crew;
    MonoClass *join;
    MonoClass *session;
    MonoClass *new_hire;
    MonoVTable *lobby_vtable;
    MonoObject *member_list;
    MonoClassField *member_list_field;
    MonoClassField *lobby_id;
    MonoClassField *host_id;
    MonoClassField *host_flag;
    MonoClassField *crew_name;
    MonoClassField *shift_started;
    MonoClassField *busy;
    MonoClassField *privacy;
    MonoClassField *status;
    MonoClassField *status_error;
    MonoClassField *member_id;
    MonoClassField *member_name;
    MonoClassField *member_look;
    MonoClassField *member_ready;
    MonoClassField *member_host;
    MonoClassField *member_local;
    MonoMethod *list_clear;
    MonoMethod *list_add;
    MonoMethod *crew_instance;
    MonoMethod *crew_create;
    MonoMethod *join_ctor;
    MonoMethod *begin_join;
    MonoMethod *in_menu;
    MonoMethod *new_hire_active;
    MonoDomain *domain;
    uint64_t server_id;
    uint64_t local_id;
    char server_name[64];
    bool initialized;
    bool joined;
} GameBridge;

static MonoBridgeApi api;
static GameBridge game;
static MoonMacBridgeLog logger;

static void bridge_log(const char *format, ...) {
    if (!logger) return;
    char message[320];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    logger(message);
}

static bool resolve_api(void) {
#define RESOLVE(member, symbol) do { \
    void *address = dlsym(RTLD_DEFAULT, symbol); \
    memcpy(&api.member, &address, sizeof(address)); \
    if (!api.member) { bridge_log("mono_bridge missing_api=%s", symbol); return false; } \
} while (0)
    RESOLVE(domain_get, "mono_domain_get");
    RESOLVE(thread_attach, "mono_thread_attach");
    RESOLVE(class_from_name, "mono_class_from_name");
    RESOLVE(class_get_nested_types, "mono_class_get_nested_types");
    RESOLVE(class_get_name, "mono_class_get_name");
    RESOLVE(class_vtable, "mono_class_vtable");
    RESOLVE(runtime_class_init, "mono_runtime_class_init");
    RESOLVE(class_get_field_from_name, "mono_class_get_field_from_name");
    RESOLVE(class_get_method_from_name, "mono_class_get_method_from_name");
    RESOLVE(object_get_class, "mono_object_get_class");
    RESOLVE(object_new, "mono_object_new");
    RESOLVE(string_new, "mono_string_new");
    RESOLVE(runtime_invoke, "mono_runtime_invoke");
    RESOLVE(object_unbox, "mono_object_unbox");
    RESOLVE(field_static_get_value, "mono_field_static_get_value");
    RESOLVE(field_static_set_value, "mono_field_static_set_value");
    RESOLVE(field_set_value, "mono_field_set_value");
    RESOLVE(method_get_class, "mono_method_get_class");
    RESOLVE(method_get_name, "mono_method_get_name");
    RESOLVE(class_get_namespace, "mono_class_get_namespace");
#undef RESOLVE
    return true;
}

static MonoClass *nested_class(MonoClass *parent, const char *expected_name) {
    void *iterator = NULL;
    MonoClass *nested;
    while ((nested = api.class_get_nested_types(parent, &iterator)) != NULL) {
        const char *name = api.class_get_name(nested);
        if (name && strcmp(name, expected_name) == 0) return nested;
    }
    return NULL;
}

static MonoClassField *field(MonoClass *klass, const char *name) {
    MonoClassField *value = api.class_get_field_from_name(klass, name);
    if (!value) bridge_log("mono_bridge missing_field=%s.%s", api.class_get_name(klass), name);
    return value;
}

static MonoMethod *method(MonoClass *klass, const char *name, int count) {
    MonoMethod *value = api.class_get_method_from_name(klass, name, count);
    if (!value) bridge_log("mono_bridge missing_method=%s.%s/%d", api.class_get_name(klass), name, count);
    return value;
}

static bool invoke(MonoMethod *target, MonoObject *object, void **args, MonoObject **result) {
    if (!target) return false;
    MonoObject *exception = NULL;
    MonoObject *value = api.runtime_invoke(target, object, args, &exception);
    if (exception) {
        bridge_log("mono_bridge managed_call_failed method=%s", api.method_get_name(target));
        return false;
    }
    if (result) *result = value;
    return true;
}

static void set_static(MonoClassField *target, void *value) {
    if (target) api.field_static_set_value(game.lobby_vtable, target, value);
}

static bool static_bool(MonoMethod *target) {
    MonoObject *result = NULL;
    if (!invoke(target, NULL, NULL, &result) || !result) return false;
    void *value = api.object_unbox(result);
    return value && *(uint8_t *)value != 0;
}

static bool resolve_game(MonoImage *image) {
    game.lobby = api.class_from_name(image, "MoonCleaners", "MoonSteamLobby");
    game.crew = api.class_from_name(image, "MoonCleaners", "MoonCrewSession");
    game.session = api.class_from_name(image, "MoonCleaners", "MoonSession");
    game.new_hire = api.class_from_name(image, "MoonCleaners", "MoonNewHire");
    if (!game.lobby || !game.crew || !game.session || !game.new_hire) return false;
    game.member = nested_class(game.lobby, "Member");
    game.join = nested_class(game.crew, "CrewJoin");
    if (!game.member || !game.join) {
        bridge_log("mono_bridge nested_types member=%s crew_join=%s",
                   game.member ? "yes" : "no", game.join ? "yes" : "no");
        return false;
    }

    game.domain = api.domain_get();
    if (!game.domain || !api.thread_attach(game.domain)) return false;
    game.lobby_vtable = api.class_vtable(game.domain, game.lobby);
    if (!game.lobby_vtable) return false;
    api.runtime_class_init(game.lobby_vtable);

#define FIELD(member, klass, name) game.member = field(game.klass, name); if (!game.member) return false
    FIELD(member_list_field, lobby, "members");
    FIELD(lobby_id, lobby, "lobby");
    FIELD(host_id, lobby, "<HostId>k__BackingField");
    FIELD(host_flag, lobby, "<IsHost>k__BackingField");
    FIELD(crew_name, lobby, "<CrewName>k__BackingField");
    FIELD(shift_started, lobby, "<ShiftStarted>k__BackingField");
    FIELD(busy, lobby, "<Busy>k__BackingField");
    FIELD(privacy, lobby, "<CurrentPrivacy>k__BackingField");
    FIELD(status, lobby, "<Status>k__BackingField");
    FIELD(status_error, lobby, "<StatusIsError>k__BackingField");
    FIELD(member_id, member, "id");
    FIELD(member_name, member, "name");
    FIELD(member_look, member, "look");
    FIELD(member_ready, member, "ready");
    FIELD(member_host, member, "host");
    FIELD(member_local, member, "local");
#undef FIELD

    api.field_static_get_value(game.lobby_vtable, game.member_list_field, &game.member_list);
    if (!game.member_list) return false;
    MonoClass *list_class = api.object_get_class(game.member_list);
    game.list_clear = method(list_class, "Clear", 0);
    game.list_add = method(list_class, "Add", 1);
    game.crew_instance = method(game.crew, "get_Instance", 0);
    game.crew_create = method(game.crew, "Create", 0);
    game.join_ctor = method(game.join, ".ctor", 3);
    game.begin_join = method(game.crew, "BeginJoin", 1);
    game.in_menu = method(game.session, "get_InMenu", 0);
    game.new_hire_active = method(game.new_hire, "get_Active", 0);
    return game.list_clear && game.list_add && game.crew_instance && game.crew_create &&
           game.join_ctor && game.begin_join && game.in_menu && game.new_hire_active;
}

bool MoonMac_GameBridgeInitialize(MonoImage *image, MoonMacBridgeLog log_callback) {
    if (game.initialized) return true;
    logger = log_callback;
    if (!resolve_api() || !resolve_game(image)) {
        bridge_log("mono_bridge initialized=no reason=game_api_unavailable");
        return false;
    }
    game.initialized = true;
    MoonMac_SetRosterCallback(MoonMac_GameBridgeRoster);
    bridge_log("mono_bridge initialized=yes build=1.0.5");
    return true;
}

bool MoonMac_IsMoonSteamUpdate(MonoMethod *candidate) {
    if (!game.initialized || !candidate) return false;
    MonoClass *klass = api.method_get_class(candidate);
    const char *name_space = klass ? api.class_get_namespace(klass) : NULL;
    const char *class_name = klass ? api.class_get_name(klass) : NULL;
    const char *method_name = api.method_get_name(candidate);
    return name_space && class_name && method_name && strcmp(name_space, "MoonCleaners") == 0 &&
           strcmp(class_name, "MoonSteam") == 0 && strcmp(method_name, "Update") == 0;
}

static MonoObject *member_object(const MoonMacRosterMember *member, uint64_t server_id, uint64_t local_id) {
    MonoObject *object = api.object_new(game.domain, game.member);
    if (!object) return NULL;
    uint64_t id = member->id;
    uint8_t ready = 1;
    uint8_t host = id == server_id;
    uint8_t local = id == local_id;
    MonoString *name = api.string_new(game.domain, member->name);
    MonoString *look = api.string_new(game.domain, "");
    api.field_set_value(object, game.member_id, &id);
    api.field_set_value(object, game.member_name, name);
    api.field_set_value(object, game.member_look, look);
    api.field_set_value(object, game.member_ready, &ready);
    api.field_set_value(object, game.member_host, &host);
    api.field_set_value(object, game.member_local, &local);
    return object;
}

void MoonMac_GameBridgeRoster(uint64_t server_id, uint64_t local_id,
                              const MoonMacRosterMember *members, size_t count) {
    if (!game.initialized || !members || !count || count > 16) return;
    game.server_id = server_id;
    game.local_id = local_id;
    snprintf(game.server_name, sizeof(game.server_name), "%s", members[0].name);
    (void)invoke(game.list_clear, game.member_list, NULL, NULL);
    for (size_t i = 0; i < count; i++) {
        MonoObject *entry = member_object(&members[i], server_id, local_id);
        if (entry) {
            void *args[] = {entry};
            (void)invoke(game.list_add, game.member_list, args, NULL);
        }
    }
    uint8_t no = 0, yes = 1;
    uint64_t id = server_id;
    int privacy = 2;
    MonoString *name = api.string_new(game.domain, members[0].name);
    MonoString *empty = api.string_new(game.domain, "");
    set_static(game.lobby_id, &id);
    set_static(game.host_id, &id);
    set_static(game.host_flag, &no);
    set_static(game.crew_name, name);
    set_static(game.shift_started, &yes);
    set_static(game.busy, &no);
    set_static(game.privacy, &privacy);
    set_static(game.status, empty);
    set_static(game.status_error, &no);
    bridge_log("mono_bridge roster_applied members=%zu server_id=%llu", count,
               (unsigned long long)server_id);
}

static void begin_join_if_ready(void) {
    if (game.joined || !MoonMac_ClientAuthenticated() || !static_bool(game.in_menu) || static_bool(game.new_hire_active)) return;
    MonoObject *crew = NULL;
    if (!invoke(game.crew_instance, NULL, NULL, &crew) || !crew) {
        if (!invoke(game.crew_create, NULL, NULL, &crew) || !invoke(game.crew_instance, NULL, NULL, &crew) || !crew) return;
    }
    MonoObject *join = api.object_new(game.domain, game.join);
    MonoString *name = api.string_new(game.domain, game.server_name[0] ? game.server_name : "Moon Sweepers server");
    if (!join || !name) return;
    uint64_t host = game.server_id;
    void *constructor_args[] = {&host, &host, name};
    if (!invoke(game.join_ctor, join, constructor_args, NULL)) return;
    void *join_args[] = {join};
    if (!invoke(game.begin_join, crew, join_args, NULL)) return;
    game.joined = true;
    bridge_log("mono_bridge begin_join requested server_id=%llu", (unsigned long long)game.server_id);
}

void MoonMac_GameBridgeUpdate(void) {
    if (!game.initialized) return;
    MoonMac_Tick();
    begin_join_if_ready();
}
