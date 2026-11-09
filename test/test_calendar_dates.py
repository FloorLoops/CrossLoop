"""Compile the pure calendar date/recurrence module and exercise its boundaries."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('cl.exe') or shutil.which('c++')
if not compiler:
    raise SystemExit('Run in a C++20 developer environment.')
with tempfile.TemporaryDirectory(prefix='crossloop-calendar-dates-') as directory:
    directory = Path(directory)
    exe = directory/'calendar.exe'
    sources = [ROOT/'test/personal/CalendarDatesTest.cpp', ROOT/'src/util/CalendarDates.cpp', ROOT/'src/util/CalendarZone.cpp', ROOT/'src/util/IcalReader.cpp']
    if Path(compiler).name.lower() == 'cl.exe':
        args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/W4', '/I'+str(ROOT/'test/web_text/stubs'), '/I'+str(ROOT/'src')] + list(map(str,sources)) + ['/Fe:'+str(exe)]
    else:
        args = [compiler, '-std=c++20', '-Wall', '-Wextra', '-I'+str(ROOT/'test/web_text/stubs'), '-I'+str(ROOT/'src')] + list(map(str,sources)) + ['-o',str(exe)]
    subprocess.run(args,cwd=directory,check=True)
    subprocess.run([str(exe)],check=True)
