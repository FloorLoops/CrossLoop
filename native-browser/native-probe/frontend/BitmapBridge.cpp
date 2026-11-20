/* Bounded RGBA8 NetSurf bitmap storage for the offline X4 Pro probe. GPL-2.0. */
#include "BitmapBridge.h"

#include <cstddef>
#include <cstdint>

#include "EngineMemory.h"

/* NetSurf only forward-declares this platform-owned type. */
struct bitmap {
  uint8_t* pixels;
  size_t rowstride;
  int width;
  int height;
  bool opaque;
  uint32_t revision;
};

namespace {
void* create(int width, int height, gui_bitmap_flags flags) {
  if (width <= 0 || height <= 0 || width > BITMAP_BRIDGE_MAX_DIMENSION || height > BITMAP_BRIDGE_MAX_DIMENSION ||
      (static_cast<unsigned>(flags) & ~(BITMAP_OPAQUE | BITMAP_CLEAR)) != 0)
    return nullptr;
  const size_t rowstride = static_cast<size_t>(width) * 4;
  const size_t bytes = rowstride * static_cast<size_t>(height);
  if (bytes > BITMAP_BRIDGE_MAX_PAYLOAD) return nullptr;
  auto* image = static_cast<bitmap*>(engine_memory_calloc(1, sizeof(bitmap)));
  if (image == nullptr) return nullptr;
  image->pixels =
      static_cast<uint8_t*>((flags & BITMAP_CLEAR) != 0 ? engine_memory_calloc(1, bytes) : engine_memory_malloc(bytes));
  if (image->pixels == nullptr) {
    engine_memory_free(image);
    return nullptr;
  }
  image->rowstride = rowstride;
  image->width = width;
  image->height = height;
  image->opaque = (flags & BITMAP_OPAQUE) != 0;
  return image;
}

void destroy(void* opaque_bitmap) {
  if (opaque_bitmap == nullptr) return;
  auto* image = static_cast<bitmap*>(opaque_bitmap);
  engine_memory_free(image->pixels);
  engine_memory_free(image);
}

void set_opaque(void* opaque_bitmap, bool opaque) {
  if (opaque_bitmap != nullptr) static_cast<bitmap*>(opaque_bitmap)->opaque = opaque;
}

bool get_opaque(void* opaque_bitmap) { return opaque_bitmap != nullptr && static_cast<bitmap*>(opaque_bitmap)->opaque; }

unsigned char* get_buffer(void* opaque_bitmap) {
  return opaque_bitmap == nullptr ? nullptr : static_cast<bitmap*>(opaque_bitmap)->pixels;
}

size_t get_rowstride(void* opaque_bitmap) {
  return opaque_bitmap == nullptr ? 0 : static_cast<bitmap*>(opaque_bitmap)->rowstride;
}

int get_width(void* opaque_bitmap) { return opaque_bitmap == nullptr ? 0 : static_cast<bitmap*>(opaque_bitmap)->width; }

int get_height(void* opaque_bitmap) {
  return opaque_bitmap == nullptr ? 0 : static_cast<bitmap*>(opaque_bitmap)->height;
}

void modified(void* opaque_bitmap) {
  if (opaque_bitmap != nullptr) ++static_cast<bitmap*>(opaque_bitmap)->revision;
}

nserror render(bitmap*, hlcache_handle*) { return NSERROR_NOT_IMPLEMENTED; }
}  // namespace

extern "C" const bitmap_fmt_t bitmap_bridge_format = {BITMAP_LAYOUT_R8G8B8A8, false};

extern "C" {
gui_bitmap_table bitmap_bridge_table = {
    .create = create,
    .destroy = destroy,
    .set_opaque = set_opaque,
    .get_opaque = get_opaque,
    .get_buffer = get_buffer,
    .get_rowstride = get_rowstride,
    .get_width = get_width,
    .get_height = get_height,
    .modified = modified,
    .render = render,
};
}

extern "C" bool bitmap_bridge_describe(const void* opaque_bitmap, bitmap_bridge_view* out) {
  if (opaque_bitmap == nullptr || out == nullptr) return false;
  const auto* image = static_cast<const bitmap*>(opaque_bitmap);
  *out = {image->pixels, image->rowstride, image->width, image->height, image->opaque, image->revision};
  return true;
}
