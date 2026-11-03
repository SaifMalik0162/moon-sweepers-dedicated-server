#!/bin/sh
set -eu

if [ "$#" -lt 1 ]; then
    printf '%s\n' "Steam did not pass the Moon Sweepers app path." >&2
    exit 64
fi

app_path=$1
shift
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
client_dylib="$script_dir/../native/macos/out/libmoon-sweepers-client.dylib"

if [ ! -f "$client_dylib" ]; then
    printf 'Mac client dylib not found: %s\nBuild it with native/macos/build.sh first.\n' "$client_dylib" >&2
    exit 66
fi

if [ -n "${STEAM_DYLD_INSERT_LIBRARIES:-}" ]; then
    insert_libraries="$STEAM_DYLD_INSERT_LIBRARIES:$client_dylib"
else
    insert_libraries=$client_dylib
fi

exec /usr/bin/open --env "DYLD_INSERT_LIBRARIES=$insert_libraries" -nW "$app_path" --args "$@"
