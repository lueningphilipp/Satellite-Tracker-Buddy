#pragma once
#include <Arduino.h>
#include <FS.h>
#include <mutex>
#include <vector>

// Picture storage for slideshow mode. Pictures live on LittleFS in the
// "spiffs" data partition that esp32dev's default partition table already
// provisions (1.375MB, unused until now) - no partition-table change, so OTA
// stays compatible, and OTA only ever rewrites the app slots, so uploaded
// pictures survive firmware updates.
//
// Each picture is stored already converted to the panel's native format -
// 800x480, 1 bit per pixel, MSB first, 1 = black, rows padded to bytes (the
// same packing Adafruit_GFX::drawBitmap() takes) - so it's exactly
// SLIDE_BYTES. The config page's JavaScript does the PNG/JPEG decode, scale,
// crop and dither in the browser; the device never decodes an image format
// (a PNG decoder would cost ~30-40KB of the ~106KB app-slot headroom plus
// a big RAM spike on a board without PSRAM).
//
// Files: /slides/<id>.bin (pixels) + /slides/<id>.txt (original filename,
// for the page's list). ids only ever increase, so sorting by id is upload
// order.
//
// Thread safety: the web handlers (async_tcp task) commit/delete pictures
// while loop() reads them for rendering, so every operation that touches
// more than the upload's own temp file takes `mutex_`. All of them are short
// (one flash write/erase or one small read) - nothing here holds the lock
// across an e-paper refresh.

static const int SLIDE_W = 800, SLIDE_H = 480;
static const size_t SLIDE_BYTES = (size_t)SLIDE_W / 8 * SLIDE_H;   // 48000
// The partition fits ~27 pictures at 12+1 4KB blocks each; 16 keeps
// comfortable headroom for LittleFS metadata, wear levelling and the
// in-flight upload temp file.
static const int SLIDE_MAX = 16;

struct SlideInfo {
    long id;
    String name;
};

class SlideStore {
public:
    // Mounts LittleFS (formats it on first use - the partition has never
    // held a filesystem before this feature) and removes a temp file left
    // by an upload that was cut off mid-transfer.
    bool begin();
    bool mounted() const { return mounted_; }

    std::vector<SlideInfo> list();   // sorted by id (upload order)
    std::vector<long> ids();         // same order, without reading the name files
    int count();
    bool hasRoom();                  // below SLIDE_MAX and enough free flash for one more

    // Upload, called from the web server's body handler: openUpload() on the
    // first chunk, writes go straight to the returned File, commitUpload()
    // from the request handler once the body is complete. Returns the new
    // id, or -1 (wrong size, store full, write error).
    File openUpload();
    long commitUpload(const String& name);
    void discardUpload();

    bool remove(long id);
    // Reads `len` bytes at `offset` of picture `id` (one locked open/seek/
    // read/close, so a concurrent delete can't pull the file out from under
    // a render half-way through - it just makes the remaining reads fail).
    bool read(long id, size_t offset, uint8_t* buf, size_t len);
    String path(long id) const;      // for serving the raw bytes as-is

    size_t freeBytes();

    // Picture currently on the panel (0 = none), set by main.cpp's
    // serviceSlideshow() so the config page can mark it in the list.
    volatile long shownId = 0;

private:
    bool mounted_ = false;
    std::mutex mutex_;
};

extern SlideStore slides;
