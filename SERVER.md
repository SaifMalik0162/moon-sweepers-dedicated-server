# Moon Sweepers server on Ubuntu

This Docker path runs the owned Windows game with Wine, a small virtual display,
and the genuine Steam GameServer API. It uses an anonymous server identity, so it
does not need a third playing account or Steam login. The server runs with
-batchmode/-nographics; players use their normal licensed Steam clients.

Supported game: **Moon Sweepers 1.0.5 / Steam build 26317497**, IL2CPP metadata 108.
The importer checks GameAssembly SHA-256
14f711a6a3b1b7cd803a03f85cc6c6201c5b546ab9b90329533f5cb8ae2e7f13
on both first import and every later start. Other builds fail with instructions to
use a compatible module release. Saves are kept separately from the game copy.

The Ubuntu/Wine backend has passed anonymous login, networking certificate,
relay readiness, and P2P listener tests without a GPU or account credentials.
Native world loading, saving, and restoration have been tested; password-protected client joining is
being verified. See the release
README for the final tested status; this guide does not claim a completed
multiplayer test.

## 1. Prepare Ubuntu

Use an Intel/AMD Ubuntu server with Docker Engine, the Docker Compose plugin, and
Python 3. Install Docker using its
[official Ubuntu instructions](https://docs.docker.com/engine/install/ubuntu/).
Your user must be able to run Docker.

Clone the public repository (or unpack a release ZIP) and create a source
directory on the Ubuntu server:

~~~sh
git clone https://github.com/hexonMD/moon-sweepers-dedicated-server.git ~/moon-sweepers
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
and game-license validation.

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
this optional filter. These are player IDs, distinct from the anonymous server ID.

The .server.env.example template contains all settings. MOON_SLOT selects save
slot 1–5. MOON_BACKUP_KEEP defaults to ten confirmed shutdown archives. Neither a
Steam username nor a Steam password belongs in this file.

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
15 seconds. The SteamID may change after a restart, so read the current status
before connecting. The status report also includes `join_transport`, `game_port`,
and `query_port` for the dedicated host.

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

All players need the same supported game build and this release's version.dll
beside their MoonSweepers.exe. Download the client/server ZIP from
[GitHub Releases](https://github.com/hexonMD/moon-sweepers-dedicated-server/releases),
or use the DLL built from this project's native
sources. Preserve any different existing mod's version.dll before installing.

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

Replace SERVER_STEAMID64 with the anonymous server ID from status. Keep the
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
