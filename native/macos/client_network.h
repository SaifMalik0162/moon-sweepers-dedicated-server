#ifndef MOON_MAC_CLIENT_NETWORK_H
#define MOON_MAC_CLIENT_NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*MoonMacLog)(const char *message);
typedef struct { uint64_t id; char name[64]; } MoonMacRosterMember;
typedef void (*MoonMacRosterCallback)(uint64_t server_id, uint64_t local_id,
                                      const MoonMacRosterMember *members, size_t count);

uint64_t MoonMac_ServerIdFromArgs(void);
void MoonMac_SetLogger(MoonMacLog logger);
void MoonMac_SetRosterCallback(MoonMacRosterCallback callback);
void MoonMac_Configure(uint64_t server_id);
void MoonMac_OnSteamLibraryLoaded(void *library);
void MoonMac_OnSteamCallback(int32_t callback_id, int32_t size, const void *data);
bool MoonMac_ClientAuthenticated(void);
void MoonMac_Tick(void);
void MoonMac_Shutdown(void);

int MoonMac_PromptPassword(char *output, size_t capacity);
void MoonMac_ShowNotice(const char *message);

#endif
