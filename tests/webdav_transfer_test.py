#!/usr/bin/env python3
"""Build and run tests/webdav/WebDavTransferTests.cpp against the checkout's
WebDAV server (lib/http/webdav over Meatloaf's FlashMFile).

Meatloaf never compiles on a host: meatloaf.cpp instantiates every filesystem,
and the WebDAV path needs only FlashMFileSystem. So the needed sources are
mirrored into the work directory with the other filesystems left out of
meatloaf.cpp and util_get_canonical_path() lifted out of utils.cpp, then
compiled with the stand-in headers in tests/webdav/. The checkout is not
modified. Exit 0 = pass, 1 = fail, 2 = build error.
"""
import argparse
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
UTIL_SOURCES = ('string_utils.cpp', 'peoples_url_parser.cpp', 'U8Char.cpp', 'punycode.cpp')


def braced_end(text, open_at):
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError('unbalanced braces')


def extract_function(text, signature_re):
    m = re.search(signature_re, text)
    if not m:
        raise ValueError(f'no match for {signature_re}')
    return text[m.start():braced_end(text, m.end() - 1)]


def flash_only_meatloaf(text):
    """meatloaf.cpp with every filesystem but FlashMFileSystem left out."""
    keep = ('meatloaf.h', 'meat_buffer.h', 'string_utils.h', 'peoples_url_parser.h',
            'MIOException.h', 'debug.h', 'device/flash.h')
    out = []
    for line in text.splitlines():
        m = re.match(r'\s*#include\s+"([^"]+)"', line)
        if m and not m.group(1).endswith(keep):
            continue
        out.append(line)
    text = '\n'.join(out)
    start = text.index('FlashMFileSystem defaultFS;')
    vec = text.index('std::vector<MFileSystem*> MFSOwner::availableFS')
    end = text.index('};', vec) + 2
    return (text[:start] + 'FlashMFileSystem defaultFS;\n'
            'std::vector<MFileSystem*> MFSOwner::availableFS { &defaultFS };' + text[end:])


def mirror(repo, work):
    """The sources to compile and the include directories, in work/tree."""
    tree = work / 'tree'
    stubs = HERE / 'webdav'
    shutil.rmtree(tree, ignore_errors=True)

    (tree / 'include').mkdir(parents=True)
    for name in ('debug.h', 'ansi_codes.h'):
        shutil.copy(repo / 'include' / name, tree / 'include' / name)
    shutil.copytree(repo / 'lib/http/webdav', tree / 'lib/http/webdav')
    shutil.copy(stubs / 'httpService.h', tree / 'lib/http/httpService.h')
    shutil.copy(stubs / 'fnSession.h', tree / 'lib/http/fnSession.h')
    (tree / 'lib/config').mkdir()
    shutil.copy(stubs / 'fnPassword.h', tree / 'lib/config/fnPassword.h')
    sources = [tree / 'lib/http/webdav' / f
               for f in ('handler.cpp', 'request.cpp', 'response.cpp', 'webdav_server.cpp')]

    shutil.copytree(repo / 'lib/meatloaf', tree / 'lib/meatloaf')
    ml = tree / 'lib/meatloaf/meatloaf.cpp'
    ml.write_text('#include <fcntl.h>\n' + flash_only_meatloaf(ml.read_text(errors='replace')))
    sources += [ml, tree / 'lib/meatloaf/device/flash.cpp']

    utils = tree / 'lib/utils'
    utils.mkdir()
    for f in (repo / 'lib/utils').iterdir():
        if f.suffix == '.h' or f.name in UTIL_SOURCES:
            shutil.copy(f, utils / f.name)
    shutil.copy(stubs / 'utils.h', utils / 'utils.h')
    canonical = extract_function((repo / 'lib/utils/utils.cpp').read_text(errors='replace'),
                                 r'std::string\s+util_get_canonical_path\s*\(\s*std::string[^)]*\)\s*\{')
    (utils / 'utils_canonical.cpp').write_text(
        '#include <cstring>\n#include <sstream>\n#include <stack>\n#include <string>\n'
        '#include <vector>\n#include "utils.h"\n' + canonical + '\n')
    sources += [utils / 'utils_canonical.cpp'] + [utils / n for n in UTIL_SOURCES]

    sources += [stubs / 'fake_httpd.cpp', stubs / 'WebDavTransferTests.cpp']
    includes = [HERE / 'esp_stubs', stubs, tree / 'lib/http/webdav', tree / 'lib/http',
                tree / 'lib/meatloaf', utils, tree / 'lib', repo / 'components_pc']
    return sources, includes


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', required=True)
    p.add_argument('--source', required=True, help='repository root')
    p.add_argument('--work', required=True, help='scratch directory for the mirror and binary')
    p.add_argument('--mbedtls-include', default='')
    p.add_argument('--mbedtls-lib', default='')
    args = p.parse_args()

    repo = pathlib.Path(args.source).resolve()
    work = pathlib.Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    sources, includes = mirror(repo, work)
    if args.mbedtls_include:
        includes.append(pathlib.Path(args.mbedtls_include))

    exe = work / 'webdav_transfer_tests'
    cmd = [args.compiler, '-std=c++20', '-w', '-o', str(exe)]
    cmd += [f'-I{i}' for i in includes] + [str(s) for s in sources]
    if args.mbedtls_lib:
        cmd.append(f'-L{args.mbedtls_lib}')
    cmd.append('-lmbedcrypto')
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        print('ERROR: build failed')
        return 2

    root = work / 'root'
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir()
    return subprocess.run([str(exe)], env={'WEBDAV_TEST_ROOT': str(root)}).returncode


if __name__ == '__main__':
    sys.exit(main())
