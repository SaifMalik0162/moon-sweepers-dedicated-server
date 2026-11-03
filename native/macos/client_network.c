#define _DARWIN_C_SOURCE
#include "client_network.h"

#include <crt_externs.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { CONTROL_CHANNEL = 3, CONTROL_VERSION = 2, MAX_PEERS = 15, MAX_TICKET = 2048,
       MAX_PASSWORD = 128, CONTROL_MAGIC = 0x5344534d };
enum { AUTH_REQUEST = 1, ROSTER = 2, REJECT = 3, PING = 4, GOODBYE = 5,
       WAIT_WORLD = 6, PASSWORD_REJECTED = 7, AUTH_RETRY = 8 };

typedef struct { int32_t kind, bytes; union { uint64_t steam_id; uint8_t raw[128]; } data; } NetIdentity;
typedef struct {
    void *data; int32_t size; uint32_t connection; NetIdentity peer;
    int64_t user, time, number; void *free_data, *release;
    int32_t channel, flags; int64_t message_user; uint16_t lane, pad;
} NetMessage;
#pragma pack(push, 1)
typedef struct { uint32_t magic; uint16_t version, type; uint32_t bytes; } ControlHeader;
typedef struct { uint64_t id; char name[64]; } RosterEntry;
typedef struct { uint64_t server; uint32_t count; RosterEntry entries[MAX_PEERS + 1]; } RosterPayload;
typedef struct {
    uint32_t bytes; uint16_t password_length; char name[64];
    uint8_t password[MAX_PASSWORD], ticket[MAX_TICKET];
} AuthPayload;
#pragma pack(pop)

_Static_assert(sizeof(NetIdentity) == 136, "Steam networking identity ABI");
_Static_assert(sizeof(NetMessage) == 216 && offsetof(NetMessage, peer) == 16, "Steam networking message ABI");
_Static_assert(sizeof(ControlHeader) == 12, "Moon control header ABI");
_Static_assert(sizeof(RosterEntry) == 72, "Moon roster entry ABI");
_Static_assert(offsetof(AuthPayload, ticket) == 198, "Moon auth payload ABI");

typedef void *(*SteamUserFn)(void);
typedef void *(*SteamMessagesFn)(void);
typedef void *(*SteamUtilsFn)(void);
typedef uint64_t (*SteamUserIdFn)(void *user);
typedef uint32_t (*SteamAuthTicketFn)(void *user, void *ticket, int max_bytes,
                                      uint32_t *ticket_bytes, const NetIdentity *remote);
typedef void (*SteamCancelTicketFn)(void *user, uint32_t handle);
typedef int (*SteamSendFn)(void *messages, const NetIdentity *remote, const void *data,
                           uint32_t bytes, int flags, int channel);
typedef int (*SteamReceiveFn)(void *messages, int channel, NetMessage **messages_out, int max_messages);
typedef void (*SteamReleaseFn)(NetMessage *message);
typedef void (*SteamRelayInitFn)(void *utils);

typedef struct {
    SteamUserFn user;
    SteamMessagesFn messages;
    SteamUtilsFn utils;
    SteamUserIdFn user_id;
    SteamAuthTicketFn auth_ticket;
    SteamCancelTicketFn cancel_ticket;
    SteamSendFn send;
    SteamReceiveFn receive;
    SteamReleaseFn release;
    SteamRelayInitFn init_relay;
} SteamApi;

static MoonMacLog logger;
static MoonMacRosterCallback roster_callback;
static SteamApi steam;
static void *steam_library;
static void *user_api, *messages_api, *utils_api;
static uint64_t server_id, local_id;
static uint32_t ticket_handle, ticket_bytes;
static uint8_t ticket_data[MAX_TICKET];
static char password[MAX_PASSWORD + 1];
static size_t password_length;
static MoonMacRosterMember roster[MAX_PEERS + 1];
static size_t roster_count;
static bool configured, api_ready, client_ready, prompt_done, password_ready;
static bool ticket_ready, ticket_failed, connected, stopped, roster_published;
static uint64_t ticket_started_ms, auth_started_ms, last_heard_ms, next_auth_ms, next_ping_ms;

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void log_message(const char *message) {
    if (logger && message) logger(message);
}

static void log_format(const char *format, ...) {
    char message[384];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    log_message(message);
}

static void secure_clear(void *memory, size_t bytes) {
    volatile uint8_t *p = memory;
    while (bytes--) *p++ = 0;
}

static uint64_t parse_id(const char *text) {
    if (!text || !*text) return 0;
    uint64_t value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        uint64_t digit = *p - '0';
        if (value > (UINT64_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    unsigned type = (unsigned)((value >> 52) & 15);
    return (value >> 56) == 1 && (value & 0xffffffffULL) != 0 && (type == 3 || type == 4) ? value : 0;
}

uint64_t MoonMac_ServerIdFromArgs(void) {
    const char *environment = getenv("MOON_CONNECT");
    uint64_t id = parse_id(environment);
    if (id) return id;
    int *argc = _NSGetArgc();
    char ***argv = _NSGetArgv();
    if (!argc || !argv || !*argv) return 0;
    for (int i = 1; i + 1 < *argc; i++) {
        if (strcmp((*argv)[i], "-moon-connect") == 0) return parse_id((*argv)[i + 1]);
    }
    return 0;
}

void MoonMac_SetLogger(MoonMacLog callback) { logger = callback; }
void MoonMac_SetRosterCallback(MoonMacRosterCallback callback) { roster_callback = callback; }

void MoonMac_Configure(uint64_t id) {
    if (configured) return;
    configured = true;
    server_id = id;
    if (server_id) log_format("client_configured server_id=%llu", (unsigned long long)server_id);
}

static bool resolve_api(void *library) {
#define RESOLVE(member, symbol) do { \
    void *address = dlsym(library, symbol); \
    memcpy(&steam.member, &address, sizeof(address)); \
    if (!steam.member) { log_format("steam_client_api missing=%s", symbol); return false; } \
} while (0)
    RESOLVE(user, "SteamAPI_SteamUser_v023");
    RESOLVE(messages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
    RESOLVE(user_id, "SteamAPI_ISteamUser_GetSteamID");
    RESOLVE(auth_ticket, "SteamAPI_ISteamUser_GetAuthSessionTicket");
    RESOLVE(cancel_ticket, "SteamAPI_ISteamUser_CancelAuthTicket");
    RESOLVE(send, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    RESOLVE(receive, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    RESOLVE(release, "SteamAPI_SteamNetworkingMessage_t_Release");
    steam.utils = dlsym(library, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    steam.init_relay = dlsym(library, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
#undef RESOLVE
    return true;
}

void MoonMac_OnSteamLibraryLoaded(void *library) {
    if (!library || steam_library == library) return;
    steam_library = library;
    api_ready = resolve_api(library);
    log_format("steam_client_api resolved=%s", api_ready ? "yes" : "no");
}

static bool valid_password(const char *value, size_t *length) {
    size_t n = 0;
    while (n <= MAX_PASSWORD && value[n]) {
        unsigned char c = (unsigned char)value[n];
        if (c < 32 || c > 126) return false;
        n++;
    }
    if (n < 8 || n > MAX_PASSWORD) return false;
    *length = n;
    return true;
}

bool MoonMac_ClientAuthenticated(void) { return connected; }

static NetIdentity identity(uint64_t id) {
    NetIdentity result = {0};
    result.kind = 16;
    result.bytes = 8;
    result.data.steam_id = id;
    return result;
}

void MoonMac_OnSteamCallback(int32_t callback_id, int32_t size, const void *data) {
    if (!data || callback_id != 163 || size < 8 || !ticket_handle) return;
    uint32_t handle = 0;
    int32_t response = 0;
    memcpy(&handle, data, sizeof(handle));
    memcpy(&response, (const uint8_t *)data + sizeof(handle), sizeof(response));
    if (handle != ticket_handle) return;
    ticket_ready = response == 1;
    ticket_failed = !ticket_ready;
    log_format("client_ticket_callback accepted=%s", ticket_ready ? "yes" : "no");
}

static bool send_control(uint16_t type, const void *payload, uint32_t bytes) {
    if (!messages_api || bytes > 4096) return false;
    uint8_t packet[sizeof(ControlHeader) + 4096];
    ControlHeader header = {CONTROL_MAGIC, CONTROL_VERSION, type, bytes};
    memcpy(packet, &header, sizeof(header));
    if (bytes) memcpy(packet + sizeof(header), payload, bytes);
    NetIdentity remote = identity(server_id);
    int result = steam.send(messages_api, &remote, packet, (uint32_t)(sizeof(header) + bytes), 0x28, CONTROL_CHANNEL);
    if (result != 1) log_format("client_control_send type=%u result=%d", type, result);
    return result == 1;
}

static void stop_client(const char *reason) {
    log_format("client_stopped reason=%s", reason);
    stopped = true;
    if (ticket_handle && user_api) steam.cancel_ticket(user_api, ticket_handle);
    ticket_handle = 0;
    connected = false;
    secure_clear(password, sizeof(password));
    secure_clear(ticket_data, sizeof(ticket_data));
    password_ready = false;
    MoonMac_ShowNotice(reason);
}

static void send_auth_request(uint64_t now) {
    AuthPayload auth = {0};
    auth.bytes = ticket_bytes;
    auth.password_length = (uint16_t)password_length;
    snprintf(auth.name, sizeof(auth.name), "Crewmate");
    memcpy(auth.password, password, password_length);
    memcpy(auth.ticket, ticket_data, ticket_bytes);
    send_control(AUTH_REQUEST, &auth, (uint32_t)(offsetof(AuthPayload, ticket) + ticket_bytes));
    secure_clear(&auth, sizeof(auth));
    if (!auth_started_ms) {
        auth_started_ms = now;
        last_heard_ms = now;
    }
    next_auth_ms = now + 3000;
    log_message("client_auth_request_sent ticket=redacted password=redacted");
}

static void process_roster(const uint8_t *payload, uint32_t bytes) {
    if (bytes < 12) return;
    uint64_t roster_server = 0;
    uint32_t count = 0;
    memcpy(&roster_server, payload, sizeof(roster_server));
    memcpy(&count, payload + sizeof(roster_server), sizeof(count));
    if (roster_server != server_id || !count || count > MAX_PEERS + 1 ||
        bytes != 12 + count * sizeof(RosterEntry)) return;
    bool includes_self = false;
    roster_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        RosterEntry entry;
        memcpy(&entry, payload + 12 + i * sizeof(entry), sizeof(entry));
        entry.name[sizeof(entry.name) - 1] = '\0';
        if (entry.id == local_id) includes_self = true;
        roster[roster_count].id = entry.id;
        snprintf(roster[roster_count].name, sizeof(roster[roster_count].name), "%.63s", entry.name);
        roster_count++;
    }
    if (!includes_self) { roster_count = 0; return; }
    connected = true;
    client_ready = true;
    last_heard_ms = monotonic_ms();
    secure_clear(password, sizeof(password));
    password_ready = false;
    if (!roster_published) {
        roster_published = true;
        log_format("client_authenticated roster_count=%u", count);
    }
    if (roster_callback) roster_callback(server_id, local_id, roster, roster_count);
}

static void consume_control(const ControlHeader *header, const uint8_t *payload) {
    uint64_t now = monotonic_ms();
    if (header->type == PASSWORD_REJECTED && header->bytes == 0) {
        stop_client("The server password was rejected. Check it with the host and try again.");
    } else if (header->type == AUTH_RETRY && header->bytes == 0) {
        if (ticket_handle && user_api) steam.cancel_ticket(user_api, ticket_handle);
        ticket_handle = ticket_bytes = 0;
        ticket_ready = ticket_failed = false;
        auth_started_ms = 0;
        next_auth_ms = 0;
        last_heard_ms = now;
        log_message("client_ticket_refresh_requested");
    } else if (header->type == REJECT) {
        char reason[241] = {0};
        uint32_t length = header->bytes > sizeof(reason) - 1 ? sizeof(reason) - 1 : header->bytes;
        for (uint32_t i = 0; i < length; i++) {
            unsigned char c = payload[i];
            reason[i] = c >= 32 && c <= 126 ? (char)c : '?';
        }
        if (*reason) {
            char message[320];
            snprintf(message, sizeof(message), "The server rejected the join: %.240s", reason);
            stop_client(message);
        } else stop_client("The server rejected the join. Check Steam ownership and online status.");
    } else if (header->type == WAIT_WORLD && header->bytes == 0) {
        last_heard_ms = now;
        log_message("client_waiting_for_server_world");
    } else if (header->type == ROSTER) {
        process_roster(payload, header->bytes);
    }
}

static void receive_control(void) {
    if (!messages_api) return;
    NetMessage *batch[16];
    for (unsigned round = 0; round < 4; round++) {
        int count = steam.receive(messages_api, CONTROL_CHANNEL, batch, 16);
        if (count < 0 || count > 16) return;
        for (int i = 0; i < count; i++) {
            NetMessage *message = batch[i];
            if (message && message->data && message->size >= (int)sizeof(ControlHeader) &&
                message->size <= (int)(sizeof(ControlHeader) + 4096)) {
                ControlHeader header;
                memcpy(&header, message->data, sizeof(header));
                uint64_t sender = message->peer.kind == 16 && message->peer.bytes == 8
                    ? message->peer.data.steam_id : 0;
                if (sender == server_id && header.magic == CONTROL_MAGIC &&
                    header.version == CONTROL_VERSION &&
                    header.bytes == (uint32_t)message->size - sizeof(header)) {
                    consume_control(&header, (const uint8_t *)message->data + sizeof(header));
                }
            }
            if (message) steam.release(message);
        }
        if (count < 16) break;
    }
}

static bool establish_client(void) {
    if (client_ready) return true;
    if (!user_api) user_api = steam.user();
    if (!messages_api) messages_api = steam.messages();
    if (!user_api || !messages_api) return false;
    local_id = steam.user_id(user_api);
    if (!local_id) return false;
    if (steam.utils && steam.init_relay && !utils_api) {
        utils_api = steam.utils();
        if (utils_api) steam.init_relay(utils_api);
    }
    client_ready = true;
    log_format("client_steam_ready identity=present");
    return true;
}

void MoonMac_Tick(void) {
    if (!server_id || stopped || !api_ready) return;
    if (!establish_client()) return;
    if (!password_ready) {
        if (prompt_done) return;
        prompt_done = true;
        int entered = MoonMac_PromptPassword(password, sizeof(password));
        if (entered <= 0) { stop_client("Server password entry was cancelled."); return; }
        if (!valid_password(password, &password_length)) {
            stop_client("Use an 8-128 character password with ordinary ASCII characters.");
            return;
        }
        password_ready = true;
        log_message("client_password_entered secret=redacted");
    }
    /* The secure AppKit prompt blocks this thread. Start all network timers
       after it closes so time spent entering the password is not a timeout. */
    uint64_t now = monotonic_ms();
    if (!ticket_handle) {
        NetIdentity remote = identity(server_id);
        ticket_bytes = 0;
        ticket_handle = steam.auth_ticket(user_api, ticket_data, MAX_TICKET, &ticket_bytes, &remote);
        ticket_started_ms = now;
        ticket_ready = ticket_failed = false;
        if (!ticket_handle || !ticket_bytes || ticket_bytes > MAX_TICKET) {
            stop_client("Steam could not issue a session ticket. Check that Steam is online and Moon Sweepers is owned by this account.");
            return;
        }
        log_format("client_ticket_requested bytes=%u ticket=redacted", ticket_bytes);
    }
    if (ticket_failed) {
        stop_client("Steam rejected the session ticket. Restart Steam and try again.");
        return;
    }
    if (!connected && !ticket_ready && now - ticket_started_ms > 30000 && !auth_started_ms) {
        stop_client("Steam did not confirm the session ticket in time.");
        return;
    }
    if (ticket_bytes && !ticket_failed && !connected && now >= next_auth_ms) send_auth_request(now);
    receive_control();
    if (stopped) return;
    if (last_heard_ms && now - last_heard_ms > (connected ? 45000 : 60000)) {
        stop_client("No response from the server. Check that it is online and the server ID is current.");
        return;
    }
    if (connected && now >= next_ping_ms) {
        next_ping_ms = now + 5000;
        send_control(PING, NULL, 0);
    }
}

void MoonMac_Shutdown(void) {
    if (connected) send_control(GOODBYE, NULL, 0);
    if (ticket_handle && user_api) steam.cancel_ticket(user_api, ticket_handle);
    ticket_handle = ticket_bytes = 0;
    connected = false;
    secure_clear(password, sizeof(password));
    secure_clear(ticket_data, sizeof(ticket_data));
    password_ready = false;
}
