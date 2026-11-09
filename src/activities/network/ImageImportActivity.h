#pragma once

#include <Bitmap.h>
#include <I18n.h>

#include <atomic>

#include "activities/UiListActivity.h"
#include "network/VerifiedDownload.h"

class ImageImportActivity final : public UiListActivity {
 public:
  ImageImportActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("ImageImport", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return download.running(); }

 protected:
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& view) override;
  void activateIndex(int index) override;
  void onBackButton() override;

 private:
  enum class Screen { Home, Downloading, Preview };
  void enterUrl();
  void connect();
  void startDownload();
  void finishDownload();
  void setScreen(Screen value, StrId message);
  bool keepImage(bool apply);
  void discard();
  bool openPreview();

  Screen screen = Screen::Home;
  VerifiedDownload download;
  bool ownsWifi = false;
  std::string url;
  std::string rawPath;
  std::string preparedPath;
  HalFile previewFile;
  std::unique_ptr<Bitmap> previewBitmap;
  std::atomic<bool> previewReadable{false};
  StrId status = StrId::STR_IMAGE_IMPORT_HELP;
  freeink::ui::ListItem rows[3]{};
  int count = 0;
};
