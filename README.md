# Moon Sweepers dedicated server for Ubuntu

An unofficial Docker server for Ubuntu on Intel/AMD. The host runs without a GPU,
uses a genuine anonymous Steam GameServer identity, and keeps the world in a
persistent volume. You and your friend use your two existing licensed Steam
accounts. A third playing account and a Steam desktop on the server are unnecessary.

**Supported game:** Moon Sweepers 1.0.5, Steam build 26317497. All playing PCs need
this release's small client module. The installers check the game build and refuse
to overwrite a different mod's `version.dll`.

**Tested:** a real licensed Steam client joined the CPU-only Ubuntu container,
loaded its world, moved, and swept dust. The server saved the changed terrain.
Fresh installation, clean Docker shutdown, automatic backup, and restoration after
restart also passed. Password checks pass automated tests; the new password prompt
still needs a live client connection test. Two simultaneous playing clients have
not yet been tested. This is an unofficial experimental adapter.

## Install on Ubuntu

Copy this project and your owned Windows game folder to your server. Then, from
the project directory:

```sh
git clone https://github.com/hexonMD/moon-sweepers-dedicated-server.git
cd moon-sweepers-dedicated-server
python3 scripts/server_setup.py
docker compose --env-file .server.env -f compose.server.yaml up -d --build
docker compose --env-file .server.env -f compose.server.yaml exec -T server /opt/moon/bin/entrypoint status
```

Setup asks for the game folder, generates a join password, and prints it plainly.
Status prints the current anonymous `server_steam_id`. Read the complete [Ubuntu installation guide](SERVER.md)
for the Windows-to-Ubuntu copy command, settings, backups, and troubleshooting.

No Steam username or password is used. The image contains Wine and our module;
it imports your game files from a read-only mount and downloads only Valve's
official server redistributables anonymously. Steam tickets and game ownership are
checked before a player can join. Any licensed player with the join password can
connect. An optional SteamID allowlist can restrict access further.

Set the password with Docker's `MOON_SERVER_PASSWORD` environment variable (8-128
printable ASCII characters). Compose reads it from `.server.env`:

```dotenv
MOON_SERVER_PASSWORD='your-chosen-server-password'
MOON_ALLOWED_STEAM_IDS=
```

Run `docker compose --env-file .server.env -f compose.server.yaml up -d` after
changing it so Compose recreates the container with the new environment. This is
the game's shared join password;
it is separate from every player's Steam password.

## Ports and joining

```dotenv
MOON_GAME_PORT=27025
MOON_QUERY_PORT=27026
```

These configure the Steam GameServer API inside the container. Both must be
different numbers from 1024 to 65535. The status command reports their values.

**Players connect using the current server Steam ID and join password. Direct
`IP:port` joining is not implemented.** Changing these settings does not create an
IP-based join endpoint. The default Compose file publishes no inbound ports;
the adapter uses Steam P2P/relay networking. Forwarding a port on your router
does not change the connection method. See [networking details](SERVER.md#ports-and-connection-method).

## Connect each Windows player

Download the client/server ZIP from [GitHub Releases](https://github.com/hexonMD/moon-sweepers-dedicated-server/releases).
Close Moon Sweepers, open PowerShell in the unpacked release, and run:

```powershell
.\scripts\install-client.ps1 -GamePath "D:\SteamLibrary\steamapps\common\Moon Sweepers" -ServerId YOUR_SERVER_STEAM_ID -Launch
```

Use that player's actual game folder and the server ID from Docker status.
Alternatively, install the DLL with the script and set this game's Steam launch
options to `-moon-connect YOUR_SERVER_STEAM_ID`. Each player remains signed into
their own normal Steam client. Enter the server password in the connection prompt.
Complete the game's first-time new-hire intro when
it appears. The server ID can change on restart; use the current one.

Launch again with saved connection settings:

```powershell
.\scripts\install-client.ps1 -Action Launch -GamePath "D:\SteamLibrary\steamapps\common\Moon Sweepers"
```

Pass a new `-ServerId` when the server's ID changes. To remove the client module,
use `-Action Uninstall`; this preserves game saves and Steam settings.

## Save and stop

```sh
docker compose --env-file .server.env -f compose.server.yaml exec -T server /opt/moon/bin/entrypoint save
docker compose --env-file .server.env -f compose.server.yaml stop
```

Docker stop asks the game to finish saving and quit. A confirmed clean stop creates
a save archive in `/data/backups`, with ten retained by default. The `server-data`
volume stores saves, backups, logs, imported game files, and the Wine prefix.
Export backups to another disk. Avoid `down -v` when keeping the world.

Development details are in [native/README.md](native/README.md).
Third-party attribution is in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
