# Moon Sweepers server on Ubuntu

This Docker path runs the owned Windows game with Wine, a small virtual display,
and the genuine Steam GameServer API. It defaults to an anonymous server identity,
or can use an optional Game Server Login Token (GSLT) for a persistent server ID.
Neither mode needs a third playing account or Steam desktop login. The server runs with
-batchmode/-nographics; players use their normal licensed Steam clients.

Supported game: **Moon Sweepers 1.0.5 / Steam build 26317497**, IL2CPP metadata 108.
The importer checks GameAssembly SHA-256
14f711a6a3b1b7cd803a03f85cc6c6201c5b546ab9b90329533f5cb8ae2e7f13
on both first import and every later start. Other builds fail with instructions to
use a compatible module release. Saves are kept separately from the game copy.

The upstream baseline previously reached anonymous Steam login, relay readiness,
and a loaded/saved world on this host. Those observations predate the fork
changes. The GSLT, ticket-refresh, Xvfb restart, and gameplay corrections in this
branch have not yet been built into the current NAS image.

## 1. Prepare Ubuntu

Use an Intel/AMD Ubuntu server with Docker Engine, the Docker Compose plugin, and
Python 3. Install Docker using its
[official Ubuntu instructions](https://docs.docker.com/engine/install/ubuntu/).
Your user must be able to run Docker.

Check out this fork at `~/moon-sweepers` on the Ubuntu server. The upstream
`v0.1.0-alpha.2` ZIP does not contain the changes documented here. Create a
source directory alongside the fork checkout:

~~~sh
cd ~/moon-sweepers
mkdir -p ~/moon-sweepers/source
~~~

From your Windows PC, copy your existing installation into that directory. For
this PC, the PowerShell command is:

~~~powershell
scp -r "D:\SteamLibrary\steamapps\common\Moon Sweepers" your-user@YOUR-SERVER:moon-sweepers/source/
~~~

The source directory on Ubuntu will then be
/home/your-user/moon-sweepers/source/Moon Sweepers. No Steam appmanifest or
Steam account directory is needed. The container mounts the selected game
directory read-only and copies only the game files into its persistent data.
No Moon Sweepers binaries or account credentials are included in the image.

## 2. Set a server password

Run the setup helper from the unpacked project directory:

~~~sh
python3 scripts/server_setup.py
~~~

It asks for the owned game directory, generates a readable secure server password,
and prints the password plainly. Anyone who owns the game and knows the server ID
and password can join. Each connecting player still passes Steam authentication
and game-license validation. A temporary Steam response that the player is
offline now triggers up to two fresh-ticket retries before the join is denied,
as described by Steam's [auth-session response codes](https://partner.steamgames.com/doc/api/steam_api#EAuthSessionResponse).

Setup writes .server.env without replacing an existing configuration. You can
also supply settings directly:

~~~sh
python3 scripts/server_setup.py \
  --game-source "$HOME/moon-sweepers/source/Moon Sweepers" \
  --password 'YOUR-SERVER-PASSWORD'
~~~

MOON_SERVER_PASSWORD is required and accepts 8–128 printable ASCII characters.
It is an ordinary Docker environment setting; no separate password file is
required. Setup saves it in .server.env with owner-only file permissions.

MOON_ALLOWED_STEAM_IDS is optional. Leaving it empty disables the additional
player filter. To restrict access further, set it to 1–15 distinct public
individual SteamID64s separated by commas. Setup also accepts --steam-ids for
this optional filter. These are player IDs, distinct from the server ID.

For a stable server ID across restarts, create a Game Server Login Token for
Moon Sweepers app 5335510 in [Steam Game Server Account Management](https://steamcommunity.com/dev/managegameservers).
Put it in
`.server.env` as `MOON_STEAM_TOKEN='your-token'`. Treat the token like a password;
never use your personal Steam password. Setup also reads `MOON_STEAM_TOKEN` from
its environment and writes it to the owner-only settings file without printing it.
Without a token, the server uses anonymous login and Steam may assign a new ID
after restart. Compose keeps the same GSLT on container recreation, so the ID
stays stable while that token remains valid. Steam's [`LogOn` API](https://partner.steamgames.com/doc/api/ISteamGameServer#LogOn)
uses the token to sign in to a persistent game-server account.

The .server.env.example template contains all settings. MOON_SLOT selects save
slot 1–5. MOON_BACKUP_KEEP defaults to ten confirmed shutdown archives. Set
MOON_DIAGNOSTICS=1 to enable timestamped join and coarse world-state diagnostics;
it is off by default. The server log is `/data/logs/moon-host.log` in the
container. Add `-moon-diagnostics` to Steam launch options on a Windows client
to record its side of the join. Neither a Steam username nor a Steam password
belongs in this file.

## 3. Start and inspect

~~~sh
docker compose --env-file .server.env -f compose.server.yaml up -d --build
docker compose --env-file .server.env -f compose.server.yaml logs -f --tail 100
docker compose --env-file .server.env -f compose.server.yaml exec -T server /opt/moon/bin/entrypoint status
~~~

At first start, the container fetches only official Steamworks server
redistributables through anonymous SteamCMD: app 1007, Windows depot 1004, pinned
manifest 7604377918839582995, observed build 25329704. Required file hashes are
locked in container/server/redist-lock.json and recorded in
/data/steam-redist/.verified.json. This does not download or unlock Moon Sweepers.

Status prints the current native JSON, including the real server SteamID and
heartbeat age. Container health requires ready=true and a heartbeat no older than
15 seconds. The status report includes the server SteamID, `steam_login_mode`
(`anonymous` or `gslt`), account type (`4` anonymous or `3` GSLT),
`join_transport`, `game_port`, and `query_port`. A rejected GSLT does not silently
fall back to anonymous login.

## Ports and connection method

Set these in `.server.env` to choose the Steam GameServer API ports:

~~~dotenv
MOON_GAME_PORT=27025
MOON_QUERY_PORT=27026
~~~

Both values must be distinct integers from 1024 to 65535. Setup also accepts
`--game-port` and `--query-port`. To apply changed environment settings, run:

~~~sh
docker compose --env-file .server.env -f compose.server.yaml up -d
~~~

Joining this adapter uses `-moon-connect SERVER_STEAMID64` and the server password.
There is no `IP:port` join listener in this release. Steam's networking messages
API establishes peer sessions and handles P2P/relay transport. The default Compose
file therefore has no published ports. Adding a Docker `ports` mapping or router
forwarding does not implement direct-IP joining. The host still needs outbound
connectivity to Steam services and networking relays.

Valve documents the transport in [ISteamNetworkingMessages](https://partner.steamgames.com/doc/api/ISteamNetworkingMessages)
and the separate game/query initialization settings in [SteamGameServer_Init](https://partner.steamgames.com/doc/api/steam_gameserver#SteamGameServer_Init).

## 4. Connect the Windows clients

Extract the release ZIP. Open PowerShell in the extracted folder and run:

~~~powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\scripts\install-client.ps1 -GamePath "D:\SteamLibrary\steamapps\common\Moon Sweepers" -ServerId YOUR_SERVER_STEAM_ID -NoDesktopShortcut -Launch
~~~

The execution-policy change applies only to this PowerShell window. The command
installs the client module and launches the game without trying to create a
desktop shortcut. To create one later, run the installer with
`-Action Shortcut -GamePath "..." -ServerId YOUR_SERVER_STEAM_ID`.

All players need the same supported game build and the client `version.dll` from
the same fork revision as the server. The upstream `v0.1.0-alpha.2` ZIP does not
include these fork changes, including support for persistent server IDs. Use the
DLL built from this checkout and preserve any different existing mod's
`version.dll` before installing.

Export the module and its license from the built container:

~~~sh
mkdir -p client-mod
docker compose --env-file .server.env -f compose.server.yaml cp server:/opt/moon/mod/version.dll ./client-mod/version.dll
docker compose --env-file .server.env -f compose.server.yaml cp server:/opt/moon/mod/MinHook-LICENSE.txt ./client-mod/MinHook-LICENSE.txt
~~~

Transfer those two files to each player's PC, then place version.dll beside that
player's MoonSweepers.exe while the game is closed.

Each player launches the game through their own licensed Steam account with:

~~~text
-moon-connect SERVER_STEAMID64
~~~

Replace SERVER_STEAMID64 with the server ID from status. The updated installer
accepts anonymous and GSLT server IDs. Keep the
normal game graphics enabled on each playing client, and enter the server password
when the module prompts. If you configured the optional allowlist, each player's
individual SteamID must also be listed. Recreate the container after changing
password or allowlist settings:

~~~sh
docker compose --env-file .server.env -f compose.server.yaml up -d
~~~

The server module and client module are the same binary; the launch flags select
their roles. Client world-download and gameplay behavior must match the tested
release status in the README.

## Save, stop, and back up

Request a save without stopping:

~~~sh
docker compose --env-file .server.env -f compose.server.yaml exec -T server /opt/moon/bin/entrypoint save
docker compose --env-file .server.env -f compose.server.yaml exec -T server /opt/moon/bin/entrypoint status
~~~

The save command queues a native save. Check save_pending/save_failed in status
and the native log for completion.

Stop through Docker so that it stays stopped:

~~~sh
docker compose --env-file .server.env -f compose.server.yaml stop
~~~

SIGTERM writes an atomic native stop command to /data/control/command. The
native module saves and quits; the runner allows up to 110 seconds before forcing
termination. Docker allows 180 seconds for shutdown and backup cleanup.

After a confirmed stopped state with save_pending=false and save_failed=false,
the runner archives the game's save directory to /data/backups. It keeps the
configured number of archives. A forced or unconfirmed stop does not create a
new confirmed backup. A fresh-install Docker SIGTERM test saved, exited with code
0, produced the backup archive, and restored its world on restart.

All saves, the Wine prefix, the imported game, dependencies, logs, and backups
persist in the server-data volume. Export backup archives to a separate host
directory after a clean stop:

~~~sh
docker compose --env-file .server.env -f compose.server.yaml cp server:/data/backups ./server-backups
~~~

Avoid down -v when keeping a world: it deletes the data volume and its backups.
Run one server container per data volume.

## Independent backend test

For SDK/runtime diagnosis without launching a game or accepting players:

~~~sh
docker build -f Dockerfile.server --target server-runtime -t moon-server-runtime:local .
docker run --rm \
  -v moon-server-probe:/data \
  --mount type=bind,src="/absolute/path/Moon Sweepers",dst=/import/game,readonly \
  moon-server-runtime:local probe
~~~

The probe obtains an anonymous identity for the real AppID 5335510, verifies
networking authentication and relay readiness, creates a short-lived unadvertised
P2P listener, and shuts down. Its persistent probe volume contains the imported
owned game and the Wine prefix. It is separate from the production save volume.
