#!/usr/bin/env python3
"""Package only adapter sources, our client DLL, and installation tools."""
from pathlib import Path
import hashlib
import zipfile

root = Path(__file__).resolve().parent.parent
files = [
    'README.md', 'SERVER.md', 'THIRD_PARTY_NOTICES.md', 'LICENSES/MinHook-LICENSE.txt',
    'Dockerfile.server', '.dockerignore', '.gitattributes', '.gitignore',
    'compose.server.yaml', '.server.env.example',
    'scripts/server_setup.py', 'scripts/install-client.ps1', 'scripts/launch-client.ps1', 'scripts/package.py',
    'native/README.md', 'native/Dockerfile.build', 'native/out/version.dll',
    'native/out/MinHook-LICENSE.txt',
]
files += [str(p.relative_to(root)).replace('\\', '/') for p in sorted((root/'native').iterdir())
          if p.is_file() and p.suffix in ('.c', '.h', '.S', '.def')]
files += ['container/server/'+name for name in (
    'entrypoint', 'server-import', 'steam-redist', 'health.py', 'redist-lock.json', 'steam-probe.c')]
files = sorted(set(files))
for name in files:
    if not (root/name).is_file():
        raise SystemExit('Missing release file: '+name)
destination = root/'dist/moon-sweepers-dedicated-server.zip'
destination.parent.mkdir(exist_ok=True)
manifest = ''.join(hashlib.sha256((root/name).read_bytes()).hexdigest()+'  '+name+'\n' for name in files)
with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
    for name in files:
        archive.write(root/name, 'moon-sweepers-server/'+name)
    archive.writestr('moon-sweepers-server/SHA256SUMS', manifest)
print(str(destination))
print('Files:', len(files)+1, 'Bytes:', destination.stat().st_size)
print('SHA256:', hashlib.sha256(destination.read_bytes()).hexdigest())
