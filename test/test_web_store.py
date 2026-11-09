"""Exercise the actual SD cache implementation with bounded write/read failures."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('cl.exe') or shutil.which('c++')
if not compiler:
    raise SystemExit('Run in a C++20 developer environment.')
with tempfile.TemporaryDirectory(prefix='crossloop-web-store-') as directory:
    directory = Path(directory)
    exe = directory / 'store.exe'
    includes = [ROOT/'test/web_text/stubs', ROOT/'src', ROOT/'lib/Memory']
    sources = [ROOT/'test/web_text/WebPageStoreTest.cpp', ROOT/'src/util/WebPageStore.cpp']
    if Path(compiler).name.lower() == 'cl.exe':
        args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/W4'] + ['/I'+str(x) for x in includes] + list(map(str, sources)) + ['/Fe:'+str(exe)]
    else:
        args = [compiler, '-std=c++20', '-Wall', '-Wextra'] + ['-I'+str(x) for x in includes] + list(map(str, sources)) + ['-o', str(exe)]
    subprocess.run(args, cwd=directory, check=True)
    subprocess.run([str(exe)], check=True)
