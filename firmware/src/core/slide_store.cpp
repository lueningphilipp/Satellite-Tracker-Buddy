#include "slide_store.h"
#include <LittleFS.h>
#include <algorithm>

SlideStore slides;

static const char* DIR = "/slides";
static const char* UPLOAD_TMP = "/slides/upload.tmp";
// Kept free on top of the picture itself so LittleFS never runs completely
// full (copy-on-write metadata updates need spare blocks).
static const size_t FREE_MARGIN = 32 * 1024;

String SlideStore::path(long id) const {
    return String(DIR) + "/" + String(id) + ".bin";
}

static String namePath(long id) {
    return String(DIR) + "/" + String(id) + ".txt";
}

bool SlideStore::begin() {
    std::lock_guard<std::mutex> lock(mutex_);
    mounted_ = LittleFS.begin(/*formatOnFail=*/true);
    if (!mounted_) {
        Serial.println("LittleFS mount failed - slideshow disabled");
        return false;
    }
    // Directory walk rather than exists()/remove() on a guessed path: the
    // core logs an error for every lookup of a path that isn't there.
    File dir = LittleFS.open(DIR);
    if (!dir || !dir.isDirectory()) {
        LittleFS.mkdir(DIR);   // first use, right after the format
    } else {
        bool staleUpload = false;
        for (String p = dir.getNextFileName(); p.length(); p = dir.getNextFileName())
            if (p == UPLOAD_TMP) staleUpload = true;
        dir.close();
        if (staleUpload) LittleFS.remove(UPLOAD_TMP);
    }
    Serial.printf("LittleFS: %u / %u bytes used\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    return true;
}

// Caller holds mutex_.
static std::vector<long> listIds() {
    std::vector<long> ids;
    File dir = LittleFS.open(DIR);
    if (!dir || !dir.isDirectory()) return ids;
    for (String p = dir.getNextFileName(); p.length(); p = dir.getNextFileName()) {
        if (!p.endsWith(".bin")) continue;
        String base = p.substring(p.lastIndexOf('/') + 1);
        long id = base.toInt();
        if (id > 0) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<SlideInfo> SlideStore::list() {
    std::vector<SlideInfo> out;
    if (!mounted_) return out;
    std::lock_guard<std::mutex> lock(mutex_);
    for (long id : listIds()) {
        String name;
        File f = LittleFS.open(namePath(id), FILE_READ);
        if (f) { name = f.readString(); f.close(); }
        out.push_back({id, name});
    }
    return out;
}

std::vector<long> SlideStore::ids() {
    if (!mounted_) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    return listIds();
}

int SlideStore::count() {
    if (!mounted_) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)listIds().size();
}

size_t SlideStore::freeBytes() {
    if (!mounted_) return 0;
    return LittleFS.totalBytes() - LittleFS.usedBytes();
}

bool SlideStore::hasRoom() {
    return mounted_ && count() < SLIDE_MAX && freeBytes() >= SLIDE_BYTES + FREE_MARGIN;
}

File SlideStore::openUpload() {
    if (!mounted_) return File();
    std::lock_guard<std::mutex> lock(mutex_);
    return LittleFS.open(UPLOAD_TMP, FILE_WRITE);
}

void SlideStore::discardUpload() {
    if (!mounted_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    LittleFS.remove(UPLOAD_TMP);
}

long SlideStore::commitUpload(const String& name) {
    if (!mounted_) return -1;
    std::lock_guard<std::mutex> lock(mutex_);
    File tmp = LittleFS.open(UPLOAD_TMP, FILE_READ);
    size_t size = tmp ? tmp.size() : 0;
    if (tmp) tmp.close();
    std::vector<long> ids = listIds();
    if (size != SLIDE_BYTES || (int)ids.size() >= SLIDE_MAX) {
        LittleFS.remove(UPLOAD_TMP);
        return -1;
    }
    long id = ids.empty() ? 1 : ids.back() + 1;
    if (!LittleFS.rename(UPLOAD_TMP, path(id))) {
        LittleFS.remove(UPLOAD_TMP);
        return -1;
    }
    File f = LittleFS.open(namePath(id), FILE_WRITE);
    if (f) { f.print(name); f.close(); }
    return id;
}

bool SlideStore::remove(long id) {
    if (!mounted_) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    LittleFS.remove(namePath(id));
    return LittleFS.remove(path(id));
}

bool SlideStore::read(long id, size_t offset, uint8_t* buf, size_t len) {
    if (!mounted_) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    File f = LittleFS.open(path(id), FILE_READ);
    if (!f) return false;
    bool ok = f.seek(offset) && f.read(buf, len) == len;
    f.close();
    return ok;
}
