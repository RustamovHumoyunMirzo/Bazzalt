"""Fetch pinned Lua runtime sources, with an independent verified GitHub fallback."""
import argparse
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import urllib.request

VERSION = '5.4.9'
RELEASE_HASH = '2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6'
COMMIT = '312b9efaa1061c2c4cad08554dbc1351c3270eef'
GITHUB_HASH = '037711bab2600009d396dd83df7c64e51eb402447196e71b56ee9d70ae022115'
RUNTIME_SOURCES = set("lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump lfunc lgc linit liolib llex lmathlib lmem loadlib lobject lopcodes loslib lparser lstate lstring lstrlib ltable ltablib ltm lua lundump lutf8lib lvm lzio".split())


def Install(destination, github_only=False):
    destination = Path(destination)
    if (destination/'src/lua.h').is_file():
        if (destination/'.bazzalt-source-sha256').read_text().strip() != RELEASE_HASH:
            raise ValueError('Lua source pin mismatch')
        return
    if destination.exists():
        raise ValueError('Lua destination already exists')
    sources = [] if github_only else [(f'https://www.lua.org/ftp/lua-{VERSION}.tar.gz', RELEASE_HASH, False)]
    sources.append((f'https://codeload.github.com/lua/lua/tar.gz/{COMMIT}', GITHUB_HASH, True))
    destination.parent.mkdir(parents=True, exist_ok=True)
    for url, checksum, github in sources:
        try:
            request = urllib.request.Request(url, headers={'User-Agent':'Bazzalt-Dependency-Fetcher'})
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read(16*1024*1024+1)
        except OSError as error:
            print(f'Lua download unavailable at {url}: {error}')
            continue
        if hashlib.sha256(data).hexdigest() != checksum:
            raise ValueError('Lua archive integrity check failed')
        with tempfile.TemporaryDirectory(prefix='lua-stage-', dir=destination.parent) as temporary:
            stage = Path(temporary)/'lua'
            (stage/'src').mkdir(parents=True)
            with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
                for member in archive:
                    if not member.isfile():
                        continue
                    relative = member.name.split('/',1)[-1]
                    name = Path(relative).name
                    if github:
                        wanted = '/' not in relative and ((name.endswith('.c') and Path(name).stem in RUNTIME_SOURCES) or (name.endswith('.h') and name != 'ltests.h'))
                    else:
                        wanted = relative.startswith('src/') and '/' not in relative[4:] and name.endswith(('.c','.h','.hpp'))
                    if wanted:
                        (stage/'src'/name).write_bytes(archive.extractfile(member).read())
            if github:
                # This C++ wrapper ships in the release tarball, outside the Git source tree.
                (stage/'src/lua.hpp').write_text('extern "C" {\n#include "lua.h"\n#include "lualib.h"\n#include "lauxlib.h"\n}\n')
            required = [stage/'src'/f'{name}.c' for name in RUNTIME_SOURCES]
            required += [stage/'src/lua.h', stage/'src/lua.hpp', stage/'src/luaconf.h']
            if not all(path.is_file() for path in required):
                raise ValueError('Pinned Lua archive has an unexpected layout')
            (stage/'.bazzalt-source-sha256').write_text(RELEASE_HASH+'\n')
            (stage/'.bazzalt-source-origin').write_text(('github:'+COMMIT if github else 'release:'+RELEASE_HASH)+'\n')
            stage.rename(destination)
        print(f'Lua {VERSION} ready at {destination}')
        return
    raise RuntimeError('All pinned Lua download endpoints failed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, default=Path(__file__).resolve().parents[1]/'deps/lua')
    parser.add_argument('--github-only', action='store_true')
    args = parser.parse_args()
    Install(args.destination, args.github_only)
