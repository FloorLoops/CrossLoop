"""Exercise production holiday and timed-light policy with a native C++ compiler."""
from pathlib import Path
import datetime
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    compiler = os.environ.get('CXX') or shutil.which('cl.exe') or shutil.which('c++')
    if not compiler:
        raise SystemExit('Run in a C++20 developer environment.')
    cases = []
    for year in range(2025, 2100):
        for month, day, expected in [(1, 1, 'NewYear'), (2, 14, 'Valentine'), (3, 17, 'StPatrick'),
                                     (7, 4, 'Independence'), (10, 31, 'Halloween'), (12, 25, 'Winter'),
                                     (1, 2, 'Standard'), (11, 1, 'Standard'), (12, 27, 'Standard')]:
            weekday = (datetime.date(year, month, day).weekday() + 1) % 7
            cases.append(f'CHECK(holidayForDate(true,{year},{month},{day},{weekday}) == Holiday::{expected});')
            cases.append(f'CHECK(holidayForDate(false,{year},{month},{day},{weekday}) == Holiday::Standard);')
        thanksgiving = next(day for day in range(22, 29) if datetime.date(year, 11, day).weekday() == 3)
        for day in range(15, 31):
            weekday = (datetime.date(year, 11, day).weekday() + 1) % 7
            expected = 'Thanksgiving' if day == thanksgiving else 'Standard'
            cases.append(f'CHECK(holidayForDate(true,{year},11,{day},{weekday}) == Holiday::{expected});')
    source = r'''
#include "CrossLoopHoliday.h"
#include "LightRestorePolicy.h"
#include <iostream>
using namespace crossloop;
int count=0, failed=0;
#define CHECK(x) do {++count; if(!(x)){++failed; std::cerr << __LINE__ << ": " #x "\n";}} while(0)
int main(){
''' + '\n'.join(cases) + r'''
CHECK(holidayForDate(true,1970,1,1,4)==Holiday::Standard);
CHECK(holidayForDate(true,2026,0,1,4)==Holiday::Standard);
CHECK(holidayForDate(true,2026,13,1,4)==Holiday::Standard);
CHECK(holidayForDate(true,2026,12,32,4)==Holiday::Standard);
constexpr int64_t sleep=1794168000;
CHECK(!restoreLight(0,true,true,true,sleep,sleep+1,5));
CHECK(restoreLight(1,true,false,false,0,0,5));
CHECK(!restoreLight(1,false,true,true,sleep,sleep,5));
CHECK(restoreLight(2,true,true,true,sleep,sleep,5));
CHECK(restoreLight(2,true,true,true,sleep,sleep+299,5));
CHECK(restoreLight(2,true,true,true,sleep,sleep+300,5));
CHECK(!restoreLight(2,true,true,true,sleep,sleep+301,5));
CHECK(!restoreLight(2,true,false,true,sleep,sleep+1,5));
CHECK(!restoreLight(2,true,true,false,sleep,sleep+1,5));
CHECK(!restoreLight(2,true,true,true,0,sleep,5));
CHECK(!restoreLight(2,true,true,true,sleep,sleep-1,5));
CHECK(!restoreLight(2,true,true,true,sleep,0,5));
CHECK(!restoreLight(2,true,true,true,sleep,sleep+1,0));
CHECK(!restoreLight(2,true,true,true,sleep,sleep+1,61));
CHECK(!restoreLight(255,true,true,true,sleep,sleep+1,5));
CHECK(restoreLight(2,false,true,true,sleep,sleep+1,5));
std::cout<<count<<" policy checks; "<<failed<<" failures\n";
return failed?1:0;
}
'''
    with tempfile.TemporaryDirectory(prefix='crossloop-policy-') as directory:
        directory = Path(directory)
        cpp = directory/'policy.cpp'
        cpp.write_text(source)
        exe = directory/'policy.exe'
        if Path(compiler).name.lower() == 'cl.exe':
            args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/W4', '/I'+str(ROOT/'src/util'), str(cpp), '/Fe:'+str(exe)]
        else:
            args = [compiler, '-std=c++20', '-Wall', '-Wextra', '-I'+str(ROOT/'src/util'), str(cpp), '-o', str(exe)]
        subprocess.run(args, cwd=directory, check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
