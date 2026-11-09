#include "ImageImportActivity.h"

#include <Bitmap.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_random.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"
#include "network/WebUrl.h"
#include "util/SleepImageCache.h"

namespace fui = freeink::ui;
void ImageImportActivity::onEnter() {
  UiListActivity::onEnter();
  setScreen(Screen::Home, StrId::STR_IMAGE_IMPORT_HELP);
}
void ImageImportActivity::discard() {
  previewBitmap.reset();
  previewFile.close();
  if (!rawPath.empty()) Storage.remove(rawPath.c_str());
  if (!preparedPath.empty()) Storage.remove(preparedPath.c_str());
  rawPath.clear();
  preparedPath.clear();
}
void ImageImportActivity::onExit() {
  closeRouting();
  download.stop();
  discard();
  if (ownsWifi) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}
const char* ImageImportActivity::headerTitle() const { return tr(STR_IMAGE_IMPORT); }
void ImageImportActivity::setScreen(Screen value, StrId message) {
  RenderLock lock(*this);
  closeRouting();
  screen = value;
  status = message;
  nav.reset();
  count = screen == Screen::Preview ? 3 : 1;
  for (int i = 0; i < count; ++i) {
    rows[i] = {};
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  switch (screen) {
    case Screen::Home:
      rows[0].label = tr(STR_WEB_OPEN_URL);
      break;
    case Screen::Downloading:
      rows[0].label = tr(STR_CANCEL);
      break;
    case Screen::Preview:
      rows[0].label = tr(STR_PERSONAL_USE_FOR_SLEEP);
      rows[1].label = tr(STR_IMAGE_KEEP);
      rows[2].label = tr(STR_IMAGE_DISCARD);
      break;
  }
  requestUpdate();
}
void ImageImportActivity::enterUrl() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(
      renderer, mappedInput, tr(STR_IMAGE_IMPORT), std::string("https://"), crossloop::web::MAX_URL, InputType::Url);
  if (!keyboard) {
    setScreen(Screen::Home, StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* entered = std::get_if<KeyboardResult>(&result.data);
    if (!entered || !crossloop::web::validUrl(entered->text)) {
      setScreen(Screen::Home, StrId::STR_WEB_BAD_URL);
      return;
    }
    url = entered->text;
    connect();
  });
}
void ImageImportActivity::connect() {
  if (WiFi.status() == WL_CONNECTED) {
    startDownload();
    return;
  }
  ownsWifi = ownsWifi || WiFi.getMode() == WIFI_MODE_NULL;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    setScreen(Screen::Home, StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    const auto* connected = std::get_if<WifiResult>(&result.data);
    if (!result.isCancelled && connected && connected->connected)
      startDownload();
    else
      setScreen(Screen::Home, StrId::STR_WIFI_CONN_FAILED);
  });
}
void ImageImportActivity::startDownload() {
  discard();
  if (!Storage.ensureDirectoryExists("/.crosspoint/sleep-cache")) {
    setScreen(Screen::Home, StrId::STR_FILE_OPEN_FAILED);
    return;
  }
  char name[96];
  snprintf(name, sizeof(name), "/.crosspoint/sleep-cache/import-%08lx%08lx.part",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  rawPath = name;
  if (Storage.exists(rawPath.c_str())) {
    setScreen(Screen::Home, StrId::STR_FILE_OPEN_FAILED);
    rawPath.clear();
    return;
  }
  setScreen(Screen::Downloading, StrId::STR_IMAGE_DOWNLOADING);
  requestUpdateAndWait();
  {
    RenderLock lock(*this);
    if (auto* fonts = renderer.getFontCacheManager()) fonts->releaseSdFontCaches();
  }
  if (!download.start(url, rawPath, 4 * 1024 * 1024)) setScreen(Screen::Home, StrId::STR_MEMORY_ERROR);
}
void ImageImportActivity::finishDownload() {
  const auto result = download.result();
  download.stop();
  if (result != VerifiedDownload::Result::Ok) {
    discard();
    setScreen(Screen::Home, StrId::STR_IMAGE_DOWNLOAD_FAILED);
    return;
  }
  {
    auto input = Storage.open(rawPath.c_str());
    uint8_t signature[8]{};
    if (!input || input.read(signature, sizeof(signature)) != sizeof(signature) ||
        !((signature[0] == 0xff && signature[1] == 0xd8) || !memcmp(signature, "\x89PNG\r\n\x1a\n", 8))) {
      discard();
      setScreen(Screen::Home, StrId::STR_IMAGE_PREPARE_FAILED);
      return;
    }
  }
  setScreen(Screen::Downloading, StrId::STR_IMAGE_PREPARING);
  requestUpdateAndWait();
  const bool crop = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;
  const bool ok =
      crossloop::prepareSleepImage(rawPath, crop, crossloop::sleepOriginalThresholds(renderer), preparedPath, true);
  Storage.remove(rawPath.c_str());
  rawPath.clear();
  if (!ok) {
    discard();
    setScreen(Screen::Home, StrId::STR_IMAGE_PREPARE_FAILED);
    return;
  }
  if (!openPreview()) {
    discard();
    setScreen(Screen::Home, StrId::STR_IMAGE_PREPARE_FAILED);
    return;
  }
  setScreen(Screen::Preview, StrId::STR_IMAGE_PREVIEW);
}
bool ImageImportActivity::openPreview() {
  previewReadable.store(false);
  previewBitmap.reset();
  previewFile.close();
  previewFile = Storage.open(preparedPath.c_str());
  if (!previewFile) return false;
  previewBitmap = makeUniqueNoThrow<Bitmap>(previewFile, true);
  return previewBitmap && previewBitmap->parseHeaders() == BmpReaderError::Ok;
}
bool ImageImportActivity::keepImage(bool apply) {
  previewBitmap.reset();
  previewFile.close();
  if (preparedPath.empty() || !Storage.ensureDirectoryExists("/Pictures/CrossLoop")) return false;
  const auto name = preparedPath.substr(preparedPath.find_last_of('/') + 1);
  const auto target = std::string("/Pictures/CrossLoop/") + name;
  if (target.size() >= sizeof(SETTINGS.personalFolderImagePath) || Storage.exists(target.c_str()) ||
      !Storage.rename(preparedPath.c_str(), target.c_str()))
    return false;
  if (apply) {
    auto old = makeUniqueNoThrow<char[]>(sizeof(SETTINGS.personalFolderImagePath));
    if (!old) {
      Storage.rename(target.c_str(), preparedPath.c_str());
      return false;
    }
    memcpy(old.get(), SETTINGS.personalFolderImagePath, sizeof(SETTINGS.personalFolderImagePath));
    const auto oldMode = SETTINGS.sleepScreen;
    snprintf(SETTINGS.personalFolderImagePath, sizeof(SETTINGS.personalFolderImagePath), "%s", target.c_str());
    SETTINGS.sleepScreen = CrossPointSettings::PERSONAL_FOLDER_IMAGE;
    if (!SETTINGS.saveToFile()) {
      memcpy(SETTINGS.personalFolderImagePath, old.get(), sizeof(SETTINGS.personalFolderImagePath));
      SETTINGS.sleepScreen = oldMode;
      Storage.rename(target.c_str(), preparedPath.c_str());
      return false;
    }
  }
  preparedPath.clear();  // The kept file now belongs to the user's Pictures folder.
  return true;
}
void ImageImportActivity::activateIndex(int index) {
  app.clearTapFlash();
  switch (screen) {
    case Screen::Home:
      enterUrl();
      return;
    case Screen::Downloading:
      download.cancel();
      return;
    case Screen::Preview:
      // Stop rendering the file before closing, moving or deleting it. setScreen
      // waits for the render mutex; resource operations below run off-screen.
      if (index != 2 && !previewReadable.load()) return;
      setScreen(Screen::Home, StrId::STR_IMAGE_PREPARING);
      if (index == 2) {
        discard();
        setScreen(Screen::Home, StrId::STR_IMAGE_IMPORT_HELP);
      } else if (keepImage(index == 0))
        setScreen(Screen::Home, StrId::STR_IMAGE_SAVED);
      else {
        if (openPreview())
          setScreen(Screen::Preview, StrId::STR_IMAGE_PREPARE_FAILED);
        else {
          discard();
          setScreen(Screen::Home, StrId::STR_IMAGE_PREPARE_FAILED);
        }
      }
      return;
  }
}
void ImageImportActivity::onBackButton() {
  if (screen == Screen::Downloading)
    download.cancel();
  else if (screen == Screen::Preview) {
    setScreen(Screen::Home, StrId::STR_IMAGE_IMPORT_HELP);
    discard();
  } else
    finish();
}
void ImageImportActivity::loop() {
  if (screen == Screen::Downloading && download.done()) {
    finishDownload();
    return;
  }
  UiListActivity::loop();
}
void ImageImportActivity::buildScreen(UiScreen& view) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  view.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                              static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(view, I18N.get(status), view.theme().smallText, 3);
  if (screen == Screen::Preview) {
    // Keep the apply/discard controls visible on short orientations too.
    const auto area = view.takeTop(static_cast<int16_t>(std::clamp(int(view.contentRect().height) - 160, 72, 300)));
    if (previewBitmap && previewBitmap->rewindToData() == BmpReaderError::Ok) {
      const float scale =
          std::min(float(area.width) / previewBitmap->getWidth(), float(area.height) / previewBitmap->getHeight());
      const int width = static_cast<int>(previewBitmap->getWidth() * scale);
      const int height = static_cast<int>(previewBitmap->getHeight() * scale);
      previewReadable.store(renderer.drawBitmap(*previewBitmap, area.x + (area.width - width) / 2,
                                                area.y + (area.height - height) / 2, width, height));
    } else
      previewReadable.store(false);
    rows[0].enabled = rows[1].enabled = previewReadable.load();
  }
  fui::ListProps props;
  props.items = rows;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(view, props);
  view.list(props);
}
