"""Exercise production profile lifecycle, storage and JSON serialization with host stubs.

Run in a C++20 developer environment: python test/test_book_text_profiles.py.
Use --source-root <snapshot> to run against the retained pre-fix source.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--source-root", type=Path, default=ROOT)
args = parser.parse_args()
source = args.source_root
settings = (source / "src/CrossPointSettings.cpp").read_text(encoding="utf-8")
header = (source / "src/CrossPointSettings.h").read_text(encoding="utf-8")
reader = (source / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")


def block(text, signature, semicolon=False):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end + int(semicolon)]


style = block(header, "struct BookTextStyle {", True)
style_getter = next(line.strip() for line in header.splitlines() if "BookTextStyle bookTextStyle() const" in line)
use_decl = next(line.strip() for line in header.splitlines() if "void useBookTextStyle(" in line)
setting_methods = ["void CrossPointSettings::toJson", "void CrossPointSettings::clearSdFontFamily", "void CrossPointSettings::applyBookTextStyle",
                   "void CrossPointSettings::useBookTextStyle", "void CrossPointSettings::restoreGlobalTextStyle"]
if "CrossPointSettings::BookTextStyle CrossPointSettings::bookTextStyle" in settings:
    setting_methods.append("CrossPointSettings::BookTextStyle CrossPointSettings::bookTextStyle")
setter = "setBookTextProfile" if "void EpubReaderActivity::setBookTextProfile" in reader else "toggleBookTextProfile"
setter_decl = "void setBookTextProfile(bool enabled);" if setter.startswith("set") else "void toggleBookTextProfile();"
invoke = "setBookTextProfile(enabled);" if setter.startswith("set") else "(void)enabled; toggleBookTextProfile();"
methods = block((ROOT / "src/ReaderFontSizes.cpp").read_text(encoding="utf-8"), "uint8_t snapToNearestPointSize") + "\n"
methods += "\n".join(block(settings, name) for name in setting_methods)
methods += "\n" + "\n".join(block(reader, name) for name in (
    "std::string bookProfilePath", "void EpubReaderActivity::onEnter", "void EpubReaderActivity::onExit",
    "bool EpubReaderActivity::saveBookTextProfile", "void EpubReaderActivity::" + setter))

harness = r'''
#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#define FREEINK_DEVICE_X4PRO 1
#define FREEINK_DEVICE_X3 0
#define FREEINK_DEVICE_X4 0
#define LOG_ERR(...) ((void)0)
constexpr size_t OBF_KEY_BUF = 64;
constexpr char bookProfilesDirectory[] = "/.crosspoint/book-profiles";
constexpr size_t bookProfileBytes = 37;
constexpr uint8_t BUILTIN_READER_POINT_SIZES[] = {12,14,16,18};
uint8_t snapToNearestPointSize(const uint8_t*, size_t, uint8_t);
constexpr const char* LANGUAGE_CODES[] = {"EN"};
int getLanguageCount() { return 1; }
namespace obfuscation { std::string obfuscateToBase64(const char* s) { return s; } }
namespace bleturner { void writeJson(int, JsonDocument&) {} }
class CrossPointSettings {
 public:
  static constexpr uint8_t FONT_FAMILY_COUNT = 1, LINE_COMPRESSION_COUNT = 8, STORED_NOTOSANS = 1;
  uint8_t fontPointSize = 14, lineSpacing = 2, embeddedStyle = 1, fontFamily = 0;
  char sdFontFamilyName[32] = "DavidLibre", dictionaryName[32] = "";
  uint8_t frontButtonBack=0, frontButtonConfirm=1, frontButtonLeft=2, frontButtonRight=3;
  uint8_t longPressMenuFunction=0, shortPwrBtn=1, language=0;
  uint16_t keyboardLayouts=0;
  int ble=0;
  @STYLE@
  @GETTER@
  @USE@
  void restoreGlobalTextStyle();
  void applyBookTextStyle(BookTextStyle style);
  bool hasBookTextStyle() const { return globalTextStyle.has_value(); }
  void toJson(JsonDocument&) const;
  void clearSdFontFamily();
  void saveToFile() { JsonDocument doc; toJson(doc); }
  std::optional<BookTextStyle> globalTextStyle;
} SETTINGS;
struct SettingInfo {
  const char* key;
  uint8_t CrossPointSettings::*valuePtr;
  size_t stringOffset=0;
  bool obfuscated=false;
};
template<class F> void forEachBaseSetting(F visit) {
  visit(SettingInfo{"lineHeight", &CrossPointSettings::lineSpacing});
  visit(SettingInfo{"embeddedStyle", &CrossPointSettings::embeddedStyle});
}
struct TestStorage;
struct HalFile {
  TestStorage* storage=nullptr;
  std::string path;
  bool valid=false;
  explicit operator bool() const { return valid; }
  bool isDirectory() const { return false; }
  size_t size() const;
  int read(uint8_t*, size_t);
  size_t write(const uint8_t*, size_t);
  bool close();
};
struct TestStorage {
  std::map<std::string,std::vector<uint8_t>> files;
  bool failWrite=false, failClose=false, failReplace=false, failRemove=false;
  int replacements=0;
  bool exists(const char* path) const { return files.contains(path); }
  bool mkdir(const char*) { return true; }
  HalFile open(const char* path) { return {this,path,exists(path)}; }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    files[path].clear(); out={this,path,true}; return true;
  }
  bool replaceFile(const char* from,const char* to) {
    if(failReplace) return false;
    files[to]=files.at(from); files.erase(from); ++replacements; return true;
  }
  bool remove(const char* path) { if(failRemove) return false; files.erase(path); return true; }
} Storage;
size_t HalFile::size() const { return storage->files.at(path).size(); }
int HalFile::read(uint8_t* out,size_t n) {
  n=std::min(n,size()); std::copy_n(storage->files.at(path).begin(),n,out); return static_cast<int>(n);
}
size_t HalFile::write(const uint8_t* p,size_t n) {
  const size_t written=storage->failWrite ? 0 : n;
  storage->files[path].assign(p,p+written); return written;
}
bool HalFile::close() { return !storage->failClose; }
struct RenderLock {};
struct ActivityResult { bool isCancelled=false; };
constexpr int STR_PERSONAL_BOOK_PROFILE_RESET=1, STR_PERSONAL_BOOK_PROFILE_ENABLE=2, STR_FAILED_LOWER=3;
const char* tr(int) { return "test"; }
void delay(int) {}
struct Gui { void drawPopup(int,const char*) {} } GUI;
struct ConfirmationActivity { ConfirmationActivity(int,int,const char*,const char*) {} };
template<class T, class... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a) {
  return std::make_unique<T>(std::forward<A>(a)...);
}
bool cancelNext=false;
struct ReaderActivity { void onEnter() {} void onExit() {} };
JsonDocument persisted;
class EpubReaderActivity : public ReaderActivity {
 public:
  std::string bookPath;
  bool bookTextProfileActive=false;
  CrossPointSettings::BookTextStyle globalTextStyleAtEntry{};
  int renderer=0,mappedInput=0;
  explicit EpubReaderActivity(std::string path):bookPath(std::move(path)) {}
  void onEnter(); void onExit(); bool saveBookTextProfile();
  @SETTER_DECL@
  void chooseProfile(bool enabled) { @INVOKE@ }
  void startActivityForResult(std::unique_ptr<ConfirmationActivity>, std::function<void(const ActivityResult&)> f) {
    const bool cancel=cancelNext; cancelNext=false; f(ActivityResult{cancel});
  }
  void applyReaderTextSettings() {
    if(bookTextProfileActive) saveBookTextProfile();
    persisted.clear(); SETTINGS.toJson(persisted);
  }
};
@METHODS@
int failures=0, checks=0;
void check(bool ok,const char* name) {
  ++checks; if(!ok) { ++failures; std::cerr<<"FAIL: "<<name<<'\n'; }
}
void reset() { SETTINGS=CrossPointSettings{}; Storage=TestStorage{}; persisted.clear(); cancelNext=false; }
void edit(const char* family,uint8_t size) {
  std::fill(std::begin(SETTINGS.sdFontFamilyName),std::end(SETTINGS.sdFontFamilyName),'\0');
  std::copy_n(family,std::min(std::strlen(family),size_t{31}),SETTINGS.sdFontFamilyName); SETTINGS.fontPointSize=size;
}
bool current(const char* family,int size) {
  return std::strcmp(SETTINGS.sdFontFamilyName,family)==0 && SETTINGS.fontPointSize==size;
}
bool global(const char* family,int size) {
  JsonDocument doc; SETTINGS.toJson(doc);
  volatile char overwriteStack[128]{}; (void)overwriteStack;
  return std::strcmp(doc["sdFontFamilyName"] | "",family)==0 && doc["fontSize"].as<int>()==size;
}
int main() {
  reset();
  EpubReaderActivity a("A.epub"); a.onEnter(); a.chooseProfile(true);
  edit("Literata",20); a.applyReaderTextSettings();
  check(global("DavidLibre",14),"book edits do not change serialized global font and size");
  a.chooseProfile(true);
  check(a.bookTextProfileActive,"repeated Save keeps profile enabled");
  check(global("DavidLibre",14),"repeated Save preserves original defaults");
  const int writes=Storage.replacements; a.saveBookTextProfile();
  check(writes==Storage.replacements,"unchanged profile avoids redundant replacement");
  a.onExit(); check(current("DavidLibre",14),"leaving A restores global font and size");
  EpubReaderActivity b("B.epub"); b.onEnter();
  check(current("DavidLibre",14),"unprofiled B inherits unchanged defaults");
  b.chooseProfile(true); edit("NotoSerif",24); b.applyReaderTextSettings(); b.onExit();
  EpubReaderActivity a2("A.epub"); a2.onEnter();
  check(current("Literata",20),"A retains its own font and size after B changes");
  a2.onExit(); EpubReaderActivity b2("B.epub"); b2.onEnter();
  check(current("NotoSerif",24),"B retains its own font and size");
  b2.chooseProfile(false);
  check(!b2.bookTextProfileActive && current("DavidLibre",14),"explicit global choice resets only B");
  check(Storage.files.contains(bookProfilePath("A.epub")),"reset B preserves A file");
  check(!Storage.files.contains(bookProfilePath("B.epub")),"reset B removes B file");
  b2.onExit(); EpubReaderActivity a3("A.epub"); a3.onEnter();
  check(current("Literata",20),"A survives B reset"); a3.onExit();
  SETTINGS=CrossPointSettings{}; EpubReaderActivity afterBoot("A.epub"); afterBoot.onEnter();
  check(current("Literata",20),"profile survives fresh settings/reader instances"); afterBoot.onExit();

  reset(); EpubReaderActivity late("late.epub"); late.onEnter();
  edit("Literata",22); late.applyReaderTextSettings(); late.chooseProfile(true); late.onExit();
  check(current("DavidLibre",14),"Save after editing restores defaults captured before editing");
  check(global("DavidLibre",14),"Save after editing repairs serialized defaults");
  EpubReaderActivity late2("late.epub"); late2.onEnter();
  check(current("Literata",22),"Save after editing retains book selection"); late2.onExit();

  reset(); EpubReaderActivity fontFailure("font.epub"); fontFailure.onEnter(); fontFailure.chooseProfile(true);
  edit("Literata",22); fontFailure.applyReaderTextSettings();
  const auto fontProfile=Storage.files.at(bookProfilePath("font.epub"));
  SETTINGS.clearSdFontFamily(); // Production failure callback from SdCardFontSystem::ensureLoaded().
  check(current("Literata",22),"font load failure retains active book selection for retry");
  fontFailure.onExit();
  check(Storage.files.at(bookProfilePath("font.epub"))==fontProfile,"font load failure does not overwrite the sidecar on exit");
  EpubReaderActivity fontRetry("font.epub"); fontRetry.onEnter();
  check(current("Literata",22),"reopening after failed load retries the saved family and size");
  edit("",16); fontRetry.applyReaderTextSettings(); fontRetry.onExit();
  EpubReaderActivity builtin("font.epub"); builtin.onEnter();
  check(current("",16),"explicit built-in selection still replaces the saved SD family"); builtin.onExit();
  SETTINGS.clearSdFontFamily();
  check(current("",14),"font failure outside a book profile preserves existing global fallback behavior");

  reset(); Storage.files[bookProfilePath("legacy.epub")]={1,12,2,1};
  EpubReaderActivity old("legacy.epub"); old.onEnter();
  check(old.bookTextProfileActive && current("DavidLibre",12),"v1 keeps size and inherits known global family");
  old.onExit(); check(Storage.files.at(bookProfilePath("legacy.epub")).size()==37,"v1 migrates to explicit v2 layout");
  for(const auto& bad : std::vector<std::vector<uint8_t>>{{1,12,2},{1,12,255,1},{9,12,2,1},std::vector<uint8_t>(38,0)}) {
    reset(); Storage.files[bookProfilePath("bad.epub")]=bad;
    EpubReaderActivity corrupt("bad.epub"); corrupt.onEnter();
    check(!corrupt.bookTextProfileActive && current("DavidLibre",14),"invalid profile leaves global state intact");
    corrupt.onExit(); check(Storage.files.at(bookProfilePath("bad.epub"))==bad,"invalid profile retained for recovery");
  }
  reset(); std::vector<uint8_t> noTerminator(37,'x');
  noTerminator[0]=2;noTerminator[1]=12;noTerminator[2]=2;noTerminator[3]=1;noTerminator[4]=0;
  Storage.files[bookProfilePath("bad.epub")]=noTerminator;
  EpubReaderActivity badString("bad.epub"); badString.onEnter();
  check(!badString.bookTextProfileActive,"reject unterminated family");

  reset(); EpubReaderActivity failed("fail.epub"); failed.onEnter(); failed.chooseProfile(true);
  const auto saved=Storage.files.at(bookProfilePath("fail.epub"));
  edit("Literata",18); Storage.failReplace=true;
  check(!failed.saveBookTextProfile(),"replacement failure reported");
  check(Storage.files.at(bookProfilePath("fail.epub"))==saved,"replacement failure preserves prior profile");
  check(!Storage.files.contains(bookProfilePath("fail.epub")+".part"),"failed temporary file cleaned");
  Storage.failReplace=false; Storage.failRemove=true; failed.chooseProfile(false);
  check(failed.bookTextProfileActive && current("Literata",18),"failed reset preserves active state");
  Storage.failRemove=false; cancelNext=true; failed.chooseProfile(false);
  check(failed.bookTextProfileActive,"cancel reset preserves profile");
  std::cout<<checks<<" checks, "<<failures<<" failures\n";
  return failures ? 1 : 0;
}
'''
for key, value in {"STYLE": style, "GETTER": style_getter, "USE": use_decl,
                   "SETTER_DECL": setter_decl, "INVOKE": invoke, "METHODS": methods}.items():
    harness = harness.replace("@" + key + "@", value)
json_header = ROOT / ".pio/libdeps/x4pro-gh_release/ArduinoJson/src"
compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++") or shutil.which("cl")
if not compiler:
    raise SystemExit("A C++20 host compiler is required; use the existing developer environment.")
with tempfile.TemporaryDirectory(prefix="crossloop-profile-") as temp:
    temp = Path(temp)
    cpp = temp / "profiles.cpp"
    cpp.write_text(harness, encoding="utf-8")
    exe = temp / ("profiles.exe" if os.name == "nt" else "profiles")
    if Path(compiler).stem.lower() == "cl":
        command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/utf-8", "/W3", "/wd4101",
                   "/I" + str(json_header), str(cpp), "/Fe:" + str(exe)]
    else:
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Wno-unused-variable",
                   "-I", str(json_header), str(cpp), "-o", str(exe)]
    subprocess.run(command, cwd=temp, check=True)
    subprocess.run([str(exe)], check=True)
