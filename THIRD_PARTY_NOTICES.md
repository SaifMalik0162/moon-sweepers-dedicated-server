# Third-party components

The native module uses [MinHook](https://github.com/TsudaKageyu/minhook) at commit
`c3fcafdc10146beb5919319d0683e44e3c30d537`. The build downloads its source and
includes the required notices with the resulting DLL. A copy of its license is
retained in [LICENSES/MinHook-LICENSE.txt](LICENSES/MinHook-LICENSE.txt).

Moon Sweepers, Unity, Steam, and Steamworks belong to their respective owners.
This is an unofficial adapter. The repository and release ZIP do not include
Moon Sweepers game files, Unity binaries, Steam account data, or Steam SDK source.
Users supply their own licensed game installation. The container obtains Valve's
official server redistributables at runtime; their hashes and source manifest are
recorded in `container/server/redist-lock.json`.
