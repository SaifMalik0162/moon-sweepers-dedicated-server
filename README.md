# Moon Sweepers dedicated server for Ubuntu

An unofficial Docker server for Ubuntu on Intel/AMD. The host runs without a GPU
and keeps the world in a persistent volume. It defaults to anonymous Steam
GameServer login, or can use an optional Game Server Login Token (GSLT) for a
persistent server ID. A third playing account and a Steam desktop on the server
are unnecessary.

**Supported game:** Moon Sweepers 1.0.5, Steam build 26317497. All playing PCs need
this release's small client module. The installers check the game build and refuse
to overwrite a different mod's `version.dll`.

**Earlier upstream baseline:** a licensed Steam client joined the CPU-only Ubuntu
container, loaded its world, moved, and swept dust. The server saved the changed
terrain. Those observations predate the current fork changes; they do not cover
the GSLT login, Steam ticket retry, or gameplay sync changes in this branch. This
remains an unofficial experimental adapter.

## Install on Ubuntu

Use a checkout of this fork, not the upstream `v0.1.0-alpha.2` release ZIP: that
release does not contain the fixes in this branch. Copy the fork and your owned
Windows game folder to your server. Then, from the project directory:

```sh
python3 scripts/server_setup.py
docker compose --env-file .server.env -f compose.server.yaml up -d --build
docker compose --env-file .server.env -f compose.server.yaml exec -T server /opt/moon/bin/entrypoint status
```

Setup asks for the game folder, generates a join password, and prints it plainly.
Status prints the server `server_steam_id`; with a GSLT, the ID persists across
restarts. Read the complete [Ubuntu installation guide](SERVER.md)
for the Windows-to-Ubuntu copy command, settings, backups, and troubleshooting.

For an anonymous server, get the current ID after a restart with:

```sh
docker compose --env-file .server.env -f compose.server.yaml exec -T server /opt/moon/bin/entrypoint status
```

Copy the value of `server_steam_id` from the JSON output.

No personal Steam username or password is used. The image contains Wine and our module;
it imports your game files from a read-only mount and downloads only Valve's
official server redistributables anonymously. Steam tickets and game ownership are
checked before a player can join. Any licensed player with the join password can
connect. An optional SteamID allowlist can restrict access further. A GSLT is
optional and is stored in the private server environment file.

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
it appears. An anonymous server ID can change on restart. On TrueNAS or Ubuntu,
get the latest `server_steam_id` with the status command above before players
join.

The installer creates **Join Moon Sweepers** on your Windows desktop, using the
game's icon. After that, double-click it and enter the server password to join.
Its launcher is installed beside the game and reads the saved connection settings;
you can remove the extracted download folder after installing. The password is
requested by the game each time and is not stored in the shortcut.

If the client module is already installed, create the shortcut without reinstalling:

```powershell
.\scripts\install-client.ps1 -Action Shortcut -GamePath "D:\SteamLibrary\steamapps\common\Moon Sweepers"
```

To update the saved ID without creating or changing a shortcut, run:

```powershell
.\scripts\install-client.ps1 -Action Configure -GamePath "D:\SteamLibrary\steamapps\common\Moon Sweepers" -ServerId NEW_SERVER_STEAM_ID
```

This updates the connection settings read by the existing shortcut and launcher.
Use `-NoDesktopShortcut` during installation if you do not want a desktop icon.
If Windows cannot save a shortcut to a redirected Desktop, installation still
finishes and `-Launch` still starts the game. You can also run the installed
`moon-client-launch.ps1` beside `MoonSweepers.exe` directly.

You can also launch again from the release folder with saved connection settings:

```powershell
.\scripts\install-client.ps1 -Action Launch -GamePath "D:\SteamLibrary\steamapps\common\Moon Sweepers"
```

Use `-Action Configure -ServerId NEW_SERVER_STEAM_ID` to save a changed ID before
joining. You can also pass `-ServerId NEW_SERVER_STEAM_ID` with `-Action Launch`
to update the saved ID and start in one command. To remove the client module, use
`-Action Uninstall`; this removes the owned shortcut and installed launcher, and
preserves game saves and Steam settings.

If a join fails, check the timestamped `moon-host.log` beside
`MoonSweepers.exe`. Add `-moon-diagnostics` to Steam launch options to record
the client's join stages there. The server log is available with:

```sh
docker compose --env-file .server.env -f compose.server.yaml logs --tail 100 server
```

Unsupported game builds need a matching adapter release. Steam ticket or
ownership errors mean the player should confirm Steam is online and signed into
the account that owns the game. A password rejection means re-entering the
server's join password. A world-load timeout means checking that the server is
ready and that the client is using its current ID. Persistent server identity
using a GSLT is a separate server configuration; it is not required to update a
client after an anonymous server restart.

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
