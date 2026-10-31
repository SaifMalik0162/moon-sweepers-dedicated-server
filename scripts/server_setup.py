#!/usr/bin/env python3
"""Prepare explicit owned-game and player settings for the anonymous server."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import secrets
import shlex
import struct
import tempfile

SUPPORTED_SHA256 = "14f711a6a3b1b7cd803a03f85cc6c6201c5b546ab9b90329533f5cb8ae2e7f13"


def steam_id(value):
    value = value.strip()
    match = re.fullmatch(r"https?://steamcommunity\.com/profiles/([0-9]{17})/?", value)
    if match:
        value = match.group(1)
    if not re.fullmatch(r"[0-9]{17}", value):
        raise ValueError("Use a numeric 17-digit SteamID64 or its /profiles/ URL.")
    number = int(value)
    if number >> 56 != 1 or (number >> 52) & 15 != 1 or not number & 0xffffffff:
        raise ValueError("Use a public individual player SteamID64, not the anonymous server ID.")
    return value


def allowlist(value):
    if not value.strip():
        return ""
    values = [steam_id(part) for part in value.split(",")]
    if not 1 <= len(values) <= 15 or len(values) != len(set(values)):
        raise ValueError("Provide 1-15 distinct player SteamID64s, separated by commas.")
    return ",".join(values)


def server_password(value):
    if not 8 <= len(value) <= 128 or any(not 32 <= ord(character) <= 126 for character in value):
        raise ValueError("The server password must contain 8-128 printable ASCII characters.")
    return value


def generate_password():
    alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789"
    return "-".join("".join(secrets.choice(alphabet) for _ in range(6)) for _ in range(4))


def env_quote(value):
    if any(character in value for character in ("\0", "\r", "\n")):
        raise ValueError("Settings cannot contain NUL or newline characters.")
    escaped = value.replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$")
    return '"' + escaped + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-source", type=Path)
    parser.add_argument("--password", help="Server password; omitted generates a readable secure password")
    parser.add_argument("--steam-ids", default="", help="Optional additional SteamID64 allowlist, comma separated; 1-15 allowed")
    parser.add_argument("--slot", type=int, choices=range(1, 6), default=1)
    parser.add_argument("--timezone", default="America/Vancouver")
    parser.add_argument("--game-port", type=int, default=27025, help="Steam GameServer API game port; joining still uses a Steam ID")
    parser.add_argument("--query-port", type=int, default=27026, help="Steam GameServer query port")
    parser.add_argument("--backup-keep", type=int, default=10)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent.parent / ".server.env")
    parser.add_argument("--force", action="store_true", help="Replace an existing settings file atomically")
    args = parser.parse_args()
    if args.game_source is None:
        args.game_source = Path(input("Owned Moon Sweepers folder on this Ubuntu server: ").strip())
    source = args.game_source.expanduser().resolve()
    for name in ("MoonSweepers.exe", "GameAssembly.dll", "MoonSweepers_Data"):
        if not (source / name).exists():
            parser.error(f"The selected game folder is missing {name}.")
    digest = hashlib.sha256((source / "GameAssembly.dll").read_bytes()).hexdigest()
    try:
        with (source / "MoonSweepers_Data/il2cpp_data/Metadata/global-metadata.dat").open("rb") as stream:
            header = stream.read(8)
    except OSError:
        parser.error("The selected installation is missing IL2CPP metadata.")
    if digest != SUPPORTED_SHA256 or len(header) != 8 or struct.unpack("<II", header) != (0xFAB11BAF, 108):
        parser.error("This release supports Moon Sweepers 1.0.5 / Steam build 26317497 only. Update the server module for other game builds.")
    try:
        password = server_password(args.password if args.password is not None else generate_password())
        players = allowlist(args.steam_ids)
        source_text = env_quote(str(source))
        password_text = env_quote(password)
        timezone_text = env_quote(args.timezone)
    except ValueError as error:
        parser.error(str(error))
    if not 1024 <= args.game_port <= 65535 or not 1024 <= args.query_port <= 65535 or args.game_port == args.query_port:
        parser.error("Game/query ports must be distinct numbers from 1024 to 65535.")
    if not 1 <= args.backup_keep <= 100:
        parser.error("Backup retention must be from 1 to 100.")
    output = args.output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() and not args.force:
        parser.error(f"{output} already exists. Edit it, or use --force to replace it.")
    settings = (
        "# Anonymous Moon Sweepers server; no Steam account credentials.\n"
        f"MOON_GAME_SOURCE={source_text}\nMOON_SERVER_PASSWORD={password_text}\nMOON_ALLOWED_STEAM_IDS={players}\n"
        f"MOON_SLOT={args.slot}\nMOON_GAME_PORT={args.game_port}\nMOON_QUERY_PORT={args.query_port}\n"
        f"MOON_BACKUP_KEEP={args.backup_keep}\nTZ={timezone_text}\n"
    )
    descriptor, temporary = tempfile.mkstemp(prefix=".server-env-", dir=output.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(settings)
        if not args.force and output.exists():
            parser.error("Settings appeared during setup; they were not overwritten.")
        os.replace(temporary, output)
    finally:
        Path(temporary).unlink(missing_ok=True)
    command_prefix = "docker compose --env-file " + shlex.quote(str(output)) + " -f compose.server.yaml"
    print(f"Saved {output}.")
    print("Server password: " + password)
    print("Player access: password plus Steam game ownership" + (" and the configured SteamID allowlist." if players else "."))
    print("Start: " + command_prefix + " up -d --build")
    print("Status: " + command_prefix + " exec -T server /opt/moon/bin/entrypoint status")


if __name__ == "__main__":
    main()
