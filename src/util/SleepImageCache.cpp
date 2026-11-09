#include "SleepImageCache.h"

#include <Bitmap.h>
#include <BmpToBmpConverter.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <Memory.h>
#include <PngToBmpConverter.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "RecoverableFile.h"

namespace crossloop {
namespace {
bool ready(const std::string& path) {
  auto file = Storage.open(path.c_str());
  if (!file || file.isDirectory()) return false;
  uint8_t header[14]{};
  if (file.read(header, sizeof(header)) != sizeof(header)) return false;
  uint32_t offset = 0;
  for (unsigned i = 0; i < 4; ++i) offset |= uint32_t(header[10 + i]) << (8 * i);
  auto bitmap = makeUniqueNoThrow<Bitmap>(file);
  return bitmap && bitmap->parseHeaders() == BmpReaderError::Ok && bitmap->getBpp() <= 2 && bitmap->getWidth() > 0 &&
         bitmap->getHeight() > 0 && bitmap->getWidth() <= display.getDisplayHeight() &&
         bitmap->getHeight() <= display.getDisplayWidth() && offset >= 54 &&
         file.size() == uint64_t(offset) + uint64_t(bitmap->getRowBytes()) * bitmap->getHeight();
}
}  // namespace
bool sleepOriginalThresholds(const GfxRenderer& renderer) {
  const auto mode = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Direct).supported()
                        ? HalDisplay::GrayscaleMode::Direct
                        : HalDisplay::GrayscaleMode::Absolute;
  return renderer.grayscaleCapabilities(mode).supported() &&
         display.getController() == HalDisplay::Controller::SSD1677 &&
         SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
}
bool prepareSleepImage(const std::string& path, bool crop, bool thresholds, std::string& prepared, bool force) {
  prepared.clear();
  auto source = Storage.open(path.c_str());
  if (!source || source.isDirectory() || source.size() > 4 * 1024 * 1024) return false;
  uint8_t signature[8]{};
  if (source.read(signature, sizeof(signature)) != sizeof(signature) || !source.seek(0)) return false;
  const bool bmp = signature[0] == 'B' && signature[1] == 'M';
  const bool jpeg = signature[0] == 0xff && signature[1] == 0xd8;
  const bool png = !memcmp(signature, "\x89PNG\r\n\x1a\n", 8);
  if (!bmp && !jpeg && !png) return false;
  if (bmp) {
    auto bitmap = makeUniqueNoThrow<Bitmap>(source);
    if (!bitmap || bitmap->parseHeaders() != BmpReaderError::Ok || bitmap->getWidth() > 4096 ||
        bitmap->getHeight() > 4096)
      return false;
    const bool fillsPanel =
        bitmap->getWidth() == display.getDisplayHeight() && bitmap->getHeight() == display.getDisplayWidth();
    const bool fittedPanel =
        !crop && (bitmap->getWidth() == display.getDisplayHeight() || bitmap->getHeight() == display.getDisplayWidth());
    if ((fillsPanel || fittedPanel) && bitmap->getBpp() <= 2 && ready(path)) {
      prepared = path;
      return true;
    }
  }
  const std::string key = path + '|' + std::to_string(source.size()) + '|' + std::to_string(source.modificationTime()) +
                          '|' + std::to_string(display.getDisplayHeight()) + 'x' +
                          std::to_string(display.getDisplayWidth()) + (crop ? "|c" : "|f") + (thresholds ? "|t" : "");
  MD5Builder digest;
  digest.begin();
  digest.add(reinterpret_cast<const uint8_t*>(key.data()), key.size());
  digest.calculate();
  const std::string destination = std::string("/.crosspoint/sleep-cache/") + digest.toString().c_str() + ".bmp";
  if (!force && ready(destination)) {
    prepared = destination;
    return true;
  }
  if (!force && ready(destination + ".bak")) {
    prepared = destination + ".bak";
    return true;
  }
  if (!Storage.ensureDirectoryExists("/.crosspoint/sleep-cache")) return false;
  const std::string temporary = destination + ".part";
  ScopedCleanup cleanup{[&] { Storage.remove(temporary.c_str()); }};
  HalFile output;
  if (!source.seek(0) || !Storage.openFileForWrite("SLPCACHE", temporary, output)) return false;
  bool converted;
  if (bmp)
    converted = BmpToBmpConverter::bmpFileToBmpStream(source, output, crop, thresholds);
  else if (jpeg)
    converted = JpegToBmpConverter::jpegFileToBmpStream(source, output, crop, thresholds);
  else
    converted = PngToBmpConverter::pngFileToBmpStream(source, output, crop, thresholds);
  const bool closed = output.close();
  source.close();
  if (!converted || !closed || !ready(temporary) || !publishRecoverableFile(temporary, destination, ready(destination)))
    return false;
  prepared = destination;
  LOG_INF("SLPCACHE", "prepared sleep image for %ux%u", display.getDisplayHeight(), display.getDisplayWidth());
  return true;
}
}  // namespace crossloop
