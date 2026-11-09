"""Check library overrides and bounded SD shelf sorting with real storage faults."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('cl.exe') or shutil.which('c++')
if not compiler:
    raise SystemExit('Run in a C++20 developer environment.')
with tempfile.TemporaryDirectory(prefix='crossloop-book-details-') as directory:
    directory = Path(directory)
    exe = directory / 'book-details.exe'
    includes = [ROOT/'test/library_builder/stubs', ROOT/'src', ROOT/'lib/LibraryIndex']
    sources = [ROOT/'test/personal/BookDetailsTest.cpp', ROOT/'src/util/BookDetails.cpp', ROOT/'lib/LibraryIndex/LibraryShelfSort.cpp']
    if Path(compiler).name.lower() == 'cl.exe':
        args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/W4'] + ['/I'+str(x) for x in includes] + list(map(str,sources)) + ['/Fe:'+str(exe)]
    else:
        args = [compiler, '-std=c++20', '-Wall', '-Wextra'] + ['-I'+str(x) for x in includes] + list(map(str,sources)) + ['-o',str(exe)]
    subprocess.run(args, cwd=directory, check=True)
    subprocess.run([str(exe)], check=True)
