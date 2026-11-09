"""Compile the production URL/parser headers and exercise stream boundaries."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('cl.exe') or shutil.which('c++')
if not compiler:
    raise SystemExit('Run in a C++20 developer environment.')
with tempfile.TemporaryDirectory(prefix='crossloop-web-') as directory:
    directory = Path(directory)
    exe = directory / 'web.exe'
    source = ROOT / 'test/web_text/WebTextTest.cpp'
    if Path(compiler).name.lower() == 'cl.exe':
        args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/W4', '/I'+str(ROOT/'src'), str(source), '/Fe:'+str(exe)]
    else:
        args = [compiler, '-std=c++20', '-Wall', '-Wextra', '-I'+str(ROOT/'src'), str(source), '-o', str(exe)]
    subprocess.run(args, cwd=directory, check=True)
    subprocess.run([str(exe)], check=True)
