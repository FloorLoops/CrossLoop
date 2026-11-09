#include "BookDetailsActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <LibraryBuilder.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiTextBand.h"

namespace fui = freeink::ui;
namespace books = crossloop::books;
namespace {
constexpr StrId FIELD_LABELS[] = {StrId::STR_BOOK_TITLE, StrId::STR_BOOK_AUTHOR, StrId::STR_BOOK_GENRE,
                                  StrId::STR_BOOK_SERIES, StrId::STR_BOOK_SERIES_NUMBER};
void copy(char* to, size_t capacity, const std::string& from) {
  const auto size = utf8SafeTruncateBuffer(from.data(), static_cast<int>(std::min(capacity - 1, from.size())));
  memcpy(to, from.data(), size);
  to[size] = 0;
}
char* detailField(books::Details& details, unsigned index) {
  switch (index) {
    case 0:
      return details.title;
    case 1:
      return details.author;
    case 2:
      return details.genre;
    case 3:
      return details.series;
    default:
      return details.seriesNumber;
  }
}
}  // namespace
char* BookDetailsActivity::field(unsigned index) { return detailField(draft, index); }
const char* BookDetailsActivity::baseField(unsigned index) const {
  switch (index) {
    case 0:
      return embedded.title;
    case 1:
      return embedded.author;
    case 2:
      return embedded.genre;
    case 3:
      return embedded.series;
    default:
      return embedded.seriesNumber;
  }
}
size_t BookDetailsActivity::fieldSize(unsigned index) {
  constexpr size_t sizes[] = {193, 129, 97, 97, 17};
  return index < 5 ? sizes[index] : 0;
}
const char* BookDetailsActivity::headerTitle() const { return tr(STR_BOOK_DETAILS); }
void BookDetailsActivity::onEnter() {
  UiListActivity::onEnter();
  writable = books::loadDetails(path, draft) != books::DetailsResult::Error;
  copy(embedded.title, sizeof(embedded.title), FsHelpers::getFileNameWithoutExtension(path));
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    Epub::SyncMetadata data;
    if (epub.loadSyncMetadata(data)) {
      if (!data.title.empty()) copy(embedded.title, sizeof(embedded.title), data.title);
      copy(embedded.author, sizeof(embedded.author), data.author);
      copy(embedded.genre, sizeof(embedded.genre), data.genre);
      copy(embedded.series, sizeof(embedded.series), data.series);
      if (data.seriesIndex && std::isfinite(*data.seriesIndex) && std::abs(*data.seriesIndex) <= 1000000)
        snprintf(embedded.seriesNumber, sizeof(embedded.seriesNumber), "%.6g", static_cast<double>(*data.seriesIndex));
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      copy(embedded.title, sizeof(embedded.title), xtc.getTitle());
      copy(embedded.author, sizeof(embedded.author), xtc.getAuthor());
    }
  }
  rebuild(writable ? StrId::STR_BOOK_DETAILS_HELP : StrId::STR_BOOK_DETAILS_CORRUPT);
}
void BookDetailsActivity::onExit() {
  closeRouting();
  Activity::onExit();
}
void BookDetailsActivity::rebuild(StrId message) {
  RenderLock lock(*this);
  closeRouting();
  status = message;
  for (unsigned i = 0; i < 8; ++i) {
    rows[i] = {};
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  for (unsigned i = 0; i < 5; ++i) {
    rows[i].label = I18N.get(FIELD_LABELS[i]);
    const char* value = field(i)[0] ? field(i) : baseField(i);
    rows[i].subtitle = value[0] ? value : tr(STR_BOOK_DETAILS_UNSET);
    rows[i].enabled = writable;
  }
  rows[5].label = tr(STR_SAVE);
  rows[5].enabled = writable;
  rows[6].label = tr(STR_BOOK_DETAILS_RESET);
  rows[6].enabled = writable;
  rows[7].label = tr(STR_CANCEL);
  requestUpdate();
}
void BookDetailsActivity::activateIndex(int index) {
  app.clearTapFlash();
  if (index == 7) {
    onBackButton();
    return;
  }
  if (!writable) return;
  if (index >= 0 && index < 5) {
    const std::string initial = field(index)[0] ? field(index) : baseField(index);
    auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(FIELD_LABELS[index]),
                                                             initial, fieldSize(index) - 1);
    if (!keyboard) {
      rebuild(StrId::STR_MEMORY_ERROR);
      return;
    }
    startActivityForResult(std::move(keyboard), [this, index](const ActivityResult& result) {
      if (result.isCancelled) return;
      const auto* entered = std::get_if<KeyboardResult>(&result.data);
      if (!entered || entered->text.size() >= fieldSize(index)) return;
      auto proposed = makeUniqueNoThrow<books::Details>(draft);
      if (!proposed) {
        rebuild(StrId::STR_MEMORY_ERROR);
        return;
      }
      auto* value = detailField(*proposed, index);
      memset(value, 0, fieldSize(index));
      memcpy(value, entered->text.data(), entered->text.size());
      const bool valid = books::validDetails(*proposed);
      {
        RenderLock lock(*this);
        closeRouting();
        if (valid) draft = *proposed;
      }
      rebuild(valid ? StrId::STR_BOOK_DETAILS_HELP : StrId::STR_BOOK_DETAILS_INVALID);
    });
  } else if (index == 5) {
    if (!books::saveDetails(path, draft)) {
      rebuild(StrId::STR_BOOK_DETAILS_FAILED);
      return;
    }
    library::markLibraryIndexDirty();
    setResult(ActivityResult{});
    finish();
  } else if (index == 6) {
    auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_BOOK_DETAILS_RESET),
                                                                tr(STR_BOOK_DETAILS_RESET_CONFIRM));
    if (!confirmation) {
      rebuild(StrId::STR_MEMORY_ERROR);
      return;
    }
    startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
      if (result.isCancelled) return;
      {
        RenderLock lock(*this);
        closeRouting();
        memset(&draft, 0, sizeof(draft));
      }
      rebuild(StrId::STR_BOOK_DETAILS_HELP);
    });
  }
}
void BookDetailsActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
void BookDetailsActivity::buildScreen(UiScreen& view) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  view.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                              static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  uiTextBand(view, I18N.get(status), view.theme().smallText, 3);
  fui::ListProps props;
  props.items = rows;
  props.count = 8;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.subtitleText = view.theme().smallText;
  props.subtitleText.maxLines = 3;
  syncListViewport(view, props);
  view.list(props);
}
