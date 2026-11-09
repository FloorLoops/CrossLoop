"""Run real streaming account parser/cache with malformed JSON and SD failures."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('cl.exe') or shutil.which('c++')
if not compiler:
    raise SystemExit('Run in a C++20 developer environment.')
with tempfile.TemporaryDirectory(prefix='crossloop-weather-') as directory:
    directory = Path(directory)
    exe = directory / 'weather.exe'
    includes = [ROOT/'test/web_text/stubs', ROOT/'src', ROOT/'lib/Memory', ROOT/'freeink-sdk/libs/network/JsonSax/include']
    sources = [ROOT/'test/personal/WeatherTest.cpp', ROOT/'src/util/PersonalCache.cpp', ROOT/'src/util/WeatherData.cpp',
               ROOT/'freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp']
    if Path(compiler).name.lower() == 'cl.exe':
        args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/W4'] + ['/I'+str(x) for x in includes] + list(map(str, sources)) + ['/Fe:'+str(exe)]
    else:
        args = [compiler, '-std=c++20', '-Wall', '-Wextra'] + ['-I'+str(x) for x in includes] + list(map(str, sources)) + ['-o', str(exe)]
    subprocess.run(args, cwd=directory, check=True)
    subprocess.run([str(exe)], check=True)
