# Native macOS client prototype

The native Steam build is Apple Silicon arm64 and uses Mono. It does not use the Windows `version.dll` or IL2CPP hooks. This adapter loads as a macOS dylib, talks to the existing Steam API, and hands an authenticated roster to the game's `MoonCrewSession.BeginJoin` flow.

## Supported game build

The prototype is pinned to Moon Sweepers 1.0.5 / Steam build `26317497`, Unity `6000.6.0f1`, bundle ID `fi.dragdrop.moonsweepers`. It checks these SHA-256 fingerprints before enabling the client:

| File | SHA-256 |
| --- | --- |
| `Contents/MacOS/MoonSweepers` | `aec13ede1ca057b175de1ff00037153a264141846815682d86284bf935f50355` |
| `Contents/Frameworks/UnityPlayer.dylib` | `f38328449c849c25b0e78aa489d1f1fff0f61e0d618fdd611208db93b1472e3b` |
| `Contents/Resources/Data/Managed/Assembly-CSharp.dll` | `75c93a9a74eea70d06c48d6c512b72a1fe6fc4f26f8fd2e1e3030ee48e27c0ae` |
| `Contents/PlugIns/steam_api.bundle/Contents/MacOS/libsteam_api.dylib` | `a11755aeb0d5bb020dac630d21103d318a2be55957e38ea9fd8112d7e689859e` |

The Steam networking library exports the user-ticket and Steam Networking Messages APIs used by the Windows client. The adapter uses Steam for ticket validation and control-channel traffic, then populates the Mono lobby state and invokes the game's existing world-join method.

## Build and launch

Build on an Apple Silicon Mac from the repository root:

```sh
./native/macos/build.sh
```

In Steam, open Moon Sweepers **Properties → General → Launch Options** and enter:

```text
"/absolute/path/to/moon-sweepers-dedicated-server/scripts/launch-macos-client.sh" %command% -moon-connect SERVER_STEAM_ID
```

Replace the script path with its full path on your Mac and `SERVER_STEAM_ID` with the current ID from the server. Build the dylib first. The wrapper preserves Steam's own injected libraries, adds the client dylib, and opens the game app with the connection argument. `%command%` is Steam's launch-option placeholder; do not run that line in Terminal. Start Moon Sweepers from Steam. The adapter asks for the server password in a secure prompt.

Logs are written to `~/Library/Logs/DragDropGames/MoonSweepers/moon-macos-adapter.log`. The adapter does not log passwords or Steam ticket contents.

## Current verification status

The dylib builds as arm64 and the installed game has been verified as the pinned Mono build. Previous probe runs confirmed the runtime hook, game join methods, and Steam networking exports. The client ticket/password flow and Mono lobby bridge are implemented in this prototype, but native Mac authentication, password acceptance, world entry, mixed Mac/Windows sessions, reconnects, and restart joins still need live verification before macOS joining can be called supported.
