#ifndef MOON_SERVER_NETWORK_H
#define MOON_SERVER_NETWORK_H
#include <stdbool.h>
#include <stdint.h>

/* Called by the existing loader on the Unity thread after IL2CPP initialization. */
bool MoonNet_Requested(void);
bool MoonNet_Install(const void *game_image,void (*log_line)(const char *));
bool MoonNet_IsDedicated(void);
bool MoonNet_IsClient(void);
bool MoonNet_BackendReady(void);
bool MoonNet_IsStopped(void);
uint64_t MoonNet_ServerId(void);
int MoonNet_GamePort(void);
int MoonNet_QueryPort(void);
unsigned MoonNet_AuthenticatedPeers(void);
const char *MoonNet_Phase(void);

/* Keep normal Steam.Update for clients. Dedicated mode supplies its own genuine
   GameServer callback pump. Both modes need Tick on the Unity thread. */
void MoonNet_Tick(void);
void MoonNet_SetWorldReady(bool ready);
void MoonNet_Shutdown(void);
#endif
