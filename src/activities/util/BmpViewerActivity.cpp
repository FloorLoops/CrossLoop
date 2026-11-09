#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RecoverableFile.h"
#include "util/SleepImageCache.h"

namespace {
constexpr char CUSTOM_SLEEP_ROOT_BMP[] = "/sleep.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr size_t COPY_BUFFER_SIZE = 2048;

bool validStoredCover(const std::string& path, bool png) {
  auto file = Storage.open(path.c_str());
  if (!file || file.isDirectory() || !file.size() || file.size() > 4 * 1024 * 1024) return false;
  if (png) {
    PngToFramebufferConverter decoder;
    ImageDimensions dimensions;
    return decoder.getDimensions(path, dimensions) && dimensions.width > 0 && dimensions.height > 0 &&
           dimensions.width <= 2048 && dimensions.height <= 3072;
  }
  auto bitmap = makeUniqueNoThrow<Bitmap>(file);
  return bitmap && bitmap->parseHeaders() == BmpReaderError::Ok && bitmap->getWidth() > 0 && bitmap->getHeight() > 0 &&
         bitmap->getWidth() <= 4096 && bitmap->getHeight() <= 4096;
}

RenderConfig fitImage(const ImageDimensions& dimensions, const GfxRenderer& renderer) {
  const float scale = std::min({static_cast<float>(renderer.getScreenWidth()) / dimensions.width,
                                static_cast<float>(renderer.getScreenHeight()) / dimensions.height, 1.0f});
  const int width = std::max(1, std::min(renderer.getScreenWidth(), static_cast<int>(dimensions.width * scale)));
  const int height = std::max(1, std::min(renderer.getScreenHeight(), static_cast<int>(dimensions.height * scale)));
  RenderConfig config{(renderer.getScreenWidth() - width) / 2, (renderer.getScreenHeight() - height) / 2, width,
                      height};
  config.useExactDimensions = true;
  return config;
}

bool overlaySleepMode() {
  const uint8_t mode = SETTINGS.sleepScreen;
  return mode == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM ||
         mode == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT;
}
}  // namespace

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                     const Mode mode)
    : Activity(mode == Mode::TickTickSnapshot ? "TickTick" : "BmpViewer", renderer, mappedInput),
      filePath(std::move(path)),
      mode(mode) {}

void BmpViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  const auto name = makeUniqueNoThrow<char[]>(500);
  if (!name) {
    LOG_ERR("BMP", "OOM: sibling filename buffer");
    return;
  }
  // Keep only the immediate neighbours. A large wallpaper folder must not
  // allocate one string per image on the C3; navigation rescans the directory.
  siblingImages.reserve(3);
  std::string previous;
  std::string next;
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name.get(), 500);
      if (name[0] != '.') {
        std::string fname(name.get());
        if (FsHelpers::hasImageExtension(fname)) {
          if (FsHelpers::naturalLess(fname, fileName) && (previous.empty() || FsHelpers::naturalLess(previous, fname)))
            previous = fname;
          if (FsHelpers::naturalLess(fileName, fname) && (next.empty() || FsHelpers::naturalLess(fname, next)))
            next = fname;
        }
      }
    }
    file.close();
    delay(1);
  }
  dir.close();

  if (!previous.empty()) siblingImages.push_back(std::move(previous));
  currentImageIndex = static_cast<int>(siblingImages.size());
  siblingImages.push_back(std::move(fileName));
  if (!next.empty()) siblingImages.push_back(std::move(next));
}

bool BmpViewerActivity::canSetSleepCover() const {
  if (mode == Mode::TickTickSnapshot) return snapshotReady;
  return FsHelpers::hasBmpExtension(filePath) || FsHelpers::hasJpgExtension(filePath) ||
         (overlaySleepMode() && FsHelpers::hasPngExtension(filePath));
}

bool BmpViewerActivity::renderImage() {
  const bool jpeg = FsHelpers::hasJpgExtension(filePath);
  JpegToFramebufferConverter jpegDecoder;
  PngToFramebufferConverter pngDecoder;
  ImageToFramebufferDecoder& decoder =
      jpeg ? static_cast<ImageToFramebufferDecoder&>(jpegDecoder) : static_cast<ImageToFramebufferDecoder&>(pngDecoder);
  ImageDimensions dimensions;
  if (!decoder.getDimensions(filePath, dimensions)) return false;
  if (dimensions.width <= 0 || dimensions.height <= 0) return false;

  const auto config = fitImage(dimensions, renderer);
  const bool hasPrevious = siblingImages.size() > 1 && currentImageIndex > 0;
  const bool hasNext = siblingImages.size() > 1 && currentImageIndex != -1 &&
                       currentImageIndex < static_cast<int>(siblingImages.size()) - 1;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), canSetSleepCover() ? tr(STR_SET_SLEEP_COVER) : "",
                                            hasPrevious ? "<" : "", hasNext ? ">" : "");
  const auto drawImage = [&]() {
    if (!decoder.decodeToFramebuffer(filePath, renderer, config)) return false;
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return true;
  };

  if (!drawImage()) return false;
  if (!jpeg) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return true;
  }

  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return false;
  if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  bool ready = true;
  // ponytail: decode each plane; add a pixel cache if large-image latency needs it.
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.clearScreen(absolute ? 0xFF : 0x00);
    renderer.setRenderMode(mode);
    if (!drawImage()) {
      ready = false;
      break;
    }
    if (mode == GfxRenderer::GRAYSCALE_LSB) {
      renderer.copyGrayscaleLsbBuffers();
    } else {
      renderer.copyGrayscaleMsbBuffers();
    }
  }
  if (ready) renderer.displayGrayBuffer();

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  if (!drawImage()) ready = false;
  renderer.cleanupGrayscaleWithFrameBuffer();
  return ready;
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();
  snapshotReady = false;

  if (mode == Mode::Image && siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  if (FsHelpers::hasPngExtension(filePath) || FsHelpers::hasJpgExtension(filePath)) {
    renderer.clearScreen();
    if (!renderImage()) {
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, "", "", "");
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    return;
  }

  HalFile file;
  // 1. Open the BMP file
  if (Storage.openFileForRead("BMP", filePath, file)) {
    Bitmap bitmap(file, true,
                  renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                      display.getController() == HalDisplay::Controller::SSD1677);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
      bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                      currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

      const char* actionLabel = mode == Mode::TickTickSnapshot ? tr(STR_PERSONAL_USE_FOR_SLEEP)
                                                               : (canSetSleepCover() ? tr(STR_SET_SLEEP_COVER) : "");
      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), actionLabel, (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));

      GUI.fillPopupProgress(renderer, popupRect, 50);

      renderer.clearScreen();
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
        renderer.clearScreen();
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        return;
      }

      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      if (bitmap.hasGreyscale()) {
        const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
        if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return;
        if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
        bool planesReady = true;
        for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
          if (bitmap.rewindToData() != BmpReaderError::Ok) {
            LOG_ERR("BMP", "Failed to rewind bitmap for grayscale rendering");
            planesReady = false;
            break;
          }
          renderer.clearScreen(absolute ? 0xFF : 0x00);
          renderer.setRenderMode(mode);
          if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
            planesReady = false;
            break;
          }
          GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
          if (mode == GfxRenderer::GRAYSCALE_LSB) {
            renderer.copyGrayscaleLsbBuffers();
          } else {
            renderer.copyGrayscaleMsbBuffers();
          }
        }
        if (planesReady) renderer.displayGrayBuffer();

        // Rebuild the BW framebuffer for popups and subsequent differential updates.
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.clearScreen();
        if (bitmap.rewindToData() != BmpReaderError::Ok ||
            !renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
          LOG_ERR("BMP", "Failed to rewind bitmap to restore the BW framebuffer");
          renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
          planesReady = false;
        }
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        renderer.cleanupGrayscaleWithFrameBuffer();
        if (!planesReady) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        snapshotReady = planesReady;
      } else {
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        snapshotReady = true;
      }

    } else {
      // Handle file parsing error
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_BMP_FILE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }

    file.close();
  } else {
    // Handle file open error
    renderer.clearScreen();
    const auto message =
        mode == Mode::TickTickSnapshot ? StrId::STR_PERSONAL_NO_TICKTICK_SNAPSHOT : StrId::STR_FILE_OPEN_FAILED;
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, I18N.get(message));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void BmpViewerActivity::doSetSleepCover() {
#if FREEINK_DEVICE_X4PRO || (FREEINK_DEVICE_X3 && !FREEINK_DEVICE_X4)
  if (mode == Mode::TickTickSnapshot) {
    if (!snapshotReady) return;
    const auto previousMode = SETTINGS.sleepScreen;
    SETTINGS.sleepScreen = CrossPointSettings::PERSONAL_TICKTICK;
    const bool saved = SETTINGS.saveToFile();
    if (!saved) SETTINGS.sleepScreen = previousMode;
    GUI.drawPopup(renderer, saved ? tr(STR_DONE) : tr(STR_FAILED_LOWER));
    delay(800);
    onEnter();
    return;
  }
#endif
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  const bool jpeg = FsHelpers::hasJpgExtension(filePath);
  const bool transparentMode = !jpeg && overlaySleepMode();
  if (!canSetSleepCover()) return;

  const char* destination =
      transparentMode ? (FsHelpers::hasPngExtension(filePath) ? TRANSPARENT_SLEEP_ROOT_PNG : TRANSPARENT_SLEEP_ROOT_BMP)
                      : CUSTOM_SLEEP_ROOT_BMP;
  const bool png = transparentMode && FsHelpers::hasPngExtension(filePath);
  std::string source = filePath;
  bool success = true;
  if (jpeg) {
    success = crossloop::prepareSleepImage(
        filePath, SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP,
        crossloop::sleepOriginalThresholds(renderer), source);
  }
  success = success && validStoredCover(source, png);
  if (success && source != destination) {
    auto buffer = makeUniqueNoThrow<uint8_t[]>(COPY_BUFFER_SIZE);
    success = false;
    if (buffer) {
      const std::string temporary = std::string(destination) + ".tmp";
      auto existingTemp = Storage.open(temporary.c_str());
      if (existingTemp && existingTemp.isDirectory()) {
        GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
        return;
      }
      existingTemp.close();
      ScopedCleanup cleanup{[&] { Storage.remove(temporary.c_str()); }};
      HalFile input, output;
      if (Storage.openFileForRead("BMP", source, input) && Storage.openFileForWrite("BMP", temporary, output)) {
        const auto expected = input.size();
        size_t copied = 0;
        int count = 0;
        bool copiedOk = true;
        while ((count = input.read(buffer.get(), COPY_BUFFER_SIZE)) > 0) {
          if (output.write(buffer.get(), static_cast<size_t>(count)) != static_cast<size_t>(count)) {
            copiedOk = false;
            break;
          }
          copied += count;
        }
        const bool closed = output.close();
        input.close();
        const bool primaryValid = validStoredCover(destination, png);
        success = copiedOk && count == 0 && copied == expected && closed && validStoredCover(temporary, png) &&
                  (!Storage.exists(destination) || primaryValid) &&
                  publishRecoverableFile(temporary, destination, primaryValid);
      }
    }
  }
  if (success) {
    const auto previousMode = SETTINGS.sleepScreen;
    if (!transparentMode) SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    success = SETTINGS.saveToFile();
    if (!success) SETTINGS.sleepScreen = previousMode;
  }
  GUI.drawPopup(renderer, success ? tr(STR_DONE) : tr(STR_FAILED_LOWER));

  delay(1000);
  onEnter();
}

void BmpViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  auto openSibling = [this](const int delta) {
    if (currentImageIndex < 0) {
      return false;
    }
    const int nextIndex = currentImageIndex + delta;
    if (siblingImages.size() <= 1 || nextIndex < 0 || nextIndex >= static_cast<int>(siblingImages.size())) {
      return false;
    }
    currentImageIndex = nextIndex;
    std::string dirPath = FsHelpers::extractFolderPath(filePath);
    if (dirPath.back() != '/') dirPath += "/";
    filePath = dirPath + siblingImages[currentImageIndex];
    siblingImages.clear();
    onEnter();
    return true;
  };

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mode == Mode::TickTickSnapshot) {
      finish();
    } else {
      activityManager.goToFileBrowser(filePath);
    }
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    openSibling(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    openSibling(-1);
    return;
  }

  // Touch boards draw no button hints; long-press stands in for Confirm, as for file browser actions.
  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenLongPress(touchX, touchY)) {
    if (!canSetSleepCover()) return;

    const char* description = mode == Mode::TickTickSnapshot ? tr(STR_PERSONAL_SLEEP_TICKTICK) : nullptr;
    auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(
        renderer, mappedInput, tr(STR_SET_SLEEP_COVER_CONFIRM),
        description ? std::string(description) : filePath.substr(filePath.find_last_of('/') + 1));
    if (!confirmation) {
      LOG_ERR("BMP", "OOM: sleep cover confirmation (%zu bytes)", sizeof(ConfirmationActivity));
      return;
    }
    startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
      if (result.isCancelled) {
        onEnter();
        return;
      }
      doSetSleepCover();
    });
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    openSibling(1);
    return;
  }
}
