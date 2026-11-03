# Moon Sweepers native server and client module

This Windows x64 `version.dll` forwards the system VERSION API and, when enabled,
adapts the owned game to an anonymous or GSLT-backed Steam GameServer host, or a
direct-connect client. It contains our adapter code and MinHook, without game binaries, accounts,
or copied game source. Runtime methods and fields are resolved through IL2CPP.

Supported game: Moon Sweepers 1.0.5, Steam build 26317497, Unity 6000.6.0f1.
The import and client installation tools reject a different GameAssembly hash.
Both players still need their own Steam accounts and game licenses. The server
defaults to Valve's anonymous GameServer login, which consumes no playing account.
Set the optional `MOON_STEAM_TOKEN` GSLT for a persistent GameServer SteamID;
never use a personal Steam password. Status reports `steam_login_mode` and
`steam_account_type` so the configured identity can be checked.

Build from the repository root with Linux Docker:

```sh
docker build -f native/Dockerfile.build --output native/out native
```

MinHook is pinned to commit `c3fcafdc10146beb5919319d0683e44e3c30d537`.
Its license is exported beside the DLL. The image build compiles the same sources.
Never replace a different mod's `version.dll`; the client installer checks this.

Server opt-in is `MOON_DEDICATED=1` or `-moon-dedicated`. The Docker runtime also
sets `MOON_HOST=1` and uses `-batchmode -nographics -moon-headless`.
CPU progress, world simulation, and saves remain active. GPU-only progress texture
operations and menu rendering are suppressed. No GPU device is mounted.

Client opt-in is `-moon-connect SERVER_STEAM_ID` (or `MOON_CONNECT`). Launch through
Steam normally. Client graphics and normal Steam authentication remain enabled.
The adapter obtains a genuine session ticket, and the server requires successful
Steam ticket validation and a Moon Sweepers license before admitting the player.
`MOON_SERVER_PASSWORD` supplies the required shared join password; clients prompt
for it before requesting admission. Passwords stay out of logs and launch arguments.
An empty `MOON_ALLOWED_STEAM_IDS` allows licensed players with the password; a
nonempty value adds a player filter. Server and clients must use the same release
(protocol v2).

For troubleshooting, set `MOON_DIAGNOSTICS=1` in the server environment or add
`-moon-diagnostics` to a client's Steam launch options. The default is off. The
host log uses UTC timestamps with millisecond precision and correlation IDs for
each process and connected peer. It records join stages and coarse host/world
state changes. It does not write password values, raw Steam ticket bytes, or
Steam's unfiltered connection debug strings. Packet diagnostics contain direction,
channel, type, size, and result only. The 1.0.5 metadata maps the related game
paths: `MoonCrewSession.TickRobotPoses` / `SendRobotPoses`, owned-vehicle report
handling, and `MoonWorld.WriteCrewStations` / `ApplyCrewStations`. These names
locate the serialization paths but do not reveal their runtime field values.
In the dedicated roster, only authenticated client identities are marked local;
the GameServer identity represents the host endpoint, not a player.

The host defaults to save slot 1; `MOON_SLOT` or `-moon-slot` selects 1-5.
When the world is bound, the dedicated host also enters the game's `MoonCrew`
world state using the stable internal root `moon-sweepers-slot-N` and the
GameServer SteamID as host. `MoonCrew.Joined` gates the game's robot pose,
owned-vehicle, and station synchronization; the synthetic lobby alone does not
enable those paths. The root is based on the save slot, not the SteamID, so an
anonymous server identity changing after restart does not change the world key.
The adapter leaves this state during shutdown.
The ordinary game save directory is retained within the persistent Wine prefix.
The module reports a world ready only after CPU initialization has completed and
the crew session is bound to that same world.

`MOON_HOST_LOG`, `MOON_HOST_STATE`, and `MOON_HOST_COMMAND` select the log, atomic
JSON heartbeat, and command-file paths. Default filenames beside the game are
`moon-host.log`, `moon-host-state.json`, and `moon-host-command.txt`.
A command file containing `save` invokes the game's complete-strokes save and
waits for its task. `stop` saves first, shuts down networking, and requests the
game's normal quit. Save failure/cancellation/90-second timeout is reported.
Docker's runner allows additional time for process exit before forced cleanup.

The normal game autosave remains enabled. Back up the persistent volume while
stopped. A health check verifies both ready state and a recent heartbeat; it does
not establish that a remote player has joined. See the root installation guide
for the current multiplayer verification status.

With no opt-in, or in processes other than MoonSweepers.exe, the DLL only forwards
VERSION exports. Removing an owned installation disables the module. This is an
unofficial experimental adapter and must be revalidated after a game update.
