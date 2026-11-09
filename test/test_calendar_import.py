"""Exercise real subscription import, recurrence exceptions and SD rollback."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
compiler=os.environ.get('CXX') or shutil.which('cl.exe') or shutil.which('c++')
if not compiler:
    raise SystemExit('Run in a C++20 developer environment.')
with tempfile.TemporaryDirectory(prefix='crossloop-calendar-import-') as directory:
    directory=Path(directory)
    exe=directory/'calendar-import.exe'
    includes=[ROOT/'test/web_text/stubs',ROOT/'src',ROOT/'lib/Memory']
    sources=[ROOT/'test/personal/CalendarImportTest.cpp']+[ROOT/'src/util'/name for name in
             ['CalendarImport.cpp','CalendarDates.cpp','CalendarZone.cpp','IcalReader.cpp','PersonalCache.cpp','CalendarAgenda.cpp']]
    if Path(compiler).name.lower()=='cl.exe':
        args=[compiler,'/nologo','/std:c++20','/EHsc','/W4']+['/I'+str(x) for x in includes]+list(map(str,sources))+['/Fe:'+str(exe)]
    else:
        args=[compiler,'-std=c++20','-Wall','-Wextra']+['-I'+str(x) for x in includes]+list(map(str,sources))+['-o',str(exe)]
    subprocess.run(args,cwd=directory,check=True)
    subprocess.run([str(exe)],check=True)
