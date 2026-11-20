"""Run the real saved-item and daily-goal code against storage fault simulators."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('c++')
if not compiler:
    raise SystemExit('A C++20 compiler is required')
with tempfile.TemporaryDirectory(prefix='crossloop-dream-') as folder:
    for name, source, extra in [
        ('SavedItems', 'SavedItems.cpp', [ROOT/'test/dream/stubs', ROOT/'test/library_builder/stubs']),
        ('ReadingGoal', 'ReadingGoal.cpp', [ROOT/'test/web_text/stubs', ROOT/'lib/Memory']),
    ]:
        output = Path(folder)/name
        args = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror']
        args += ['-I'+str(p) for p in [*extra, ROOT/'src']]
        args += [str(ROOT/'test/dream'/f'{name}Test.cpp'), str(ROOT/'src/util'/source), '-o', str(output)]
        subprocess.run(args, check=True)
        subprocess.run([str(output)], check=True)
