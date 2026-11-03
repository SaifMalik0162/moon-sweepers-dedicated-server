#ifndef MOON_MAC_GAME_BRIDGE_H
#define MOON_MAC_GAME_BRIDGE_H

#include "client_network.h"
#include <stdbool.h>

typedef struct _MonoImage MonoImage;
typedef struct _MonoMethod MonoMethod;
typedef void (*MoonMacBridgeLog)(const char *message);

bool MoonMac_GameBridgeInitialize(MonoImage *image, MoonMacBridgeLog logger);
bool MoonMac_IsMoonSteamUpdate(MonoMethod *method);
void MoonMac_GameBridgeUpdate(void);
void MoonMac_GameBridgeRoster(uint64_t server_id, uint64_t local_id,
                              const MoonMacRosterMember *members, size_t count);

#endif
