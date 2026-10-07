#include "storage.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
#include <vector>
#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace {
FILE *openFile(const fs::path &path, int flags) {
    const int access = flags & (O_WRONLY | O_RDWR);
    const char *mode = access == O_RDONLY ? "rb" : access == O_WRONLY ? "wb" : "r+b";
#if defined(_WIN32)
    const int fd = _wopen(path.c_str(), flags | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return nullptr;
    FILE *file = _fdopen(fd, mode);
    if (!file) _close(fd);
#else
    const int fd = ::open(path.c_str(), flags, 0666);
    if (fd < 0) return nullptr;
    FILE *file = fdopen(fd, mode);
    if (!file) ::close(fd);
#endif
    return file;
}
bool seekFile(FILE *file, uint64_t position) {
    if (position > uint64_t(std::numeric_limits<int64_t>::max())) return false;
#if defined(_WIN32)
    return _fseeki64(file, int64_t(position), SEEK_SET) == 0;
#else
    return fseeko(file, off_t(position), SEEK_SET) == 0;
#endif
}
int64_t filePosition(FILE *file) {
#if defined(_WIN32)
    return _ftelli64(file);
#else
    return ftello(file);
#endif
}
}

struct XtgvHostFile::State {
    FILE *file = nullptr;
    XtgvHostStorage *owner = nullptr;
    fs::path path;
    int flags = 0;
    bool valid = false, directory = false;
    std::vector<fs::path> entries;
    size_t next = 0;
    ~State() { if (file) std::fclose(file); }
};

bool XtgvHostStorage::begin(const fs::path &root) {
    std::error_code error;
    auto canonical = fs::canonical(root, error);
    if (error || !fs::is_directory(canonical, error) || error) return false;
    root_ = canonical;
    return true;
}
fs::path XtgvHostStorage::resolve(const char *name) const {
    if (!name || root_.empty()) return {};
    fs::path requested(name);
    if (requested.has_root_name()) return {}; // no Windows drive/UNC escape
    requested = requested.relative_path(); // leading slash is card-relative
    for (const auto &component : requested)
        if (component == "..") return {};
    std::error_code error;
    auto resolved = fs::weakly_canonical(root_ / requested, error);
    if (error) return {};
    auto rootPart = root_.begin(), pathPart = resolved.begin();
    for (; rootPart != root_.end(); ++rootPart, ++pathPart)
        if (pathPart == resolved.end() || *rootPart != *pathPart) return {};
    return resolved;
}
XtgvHostFile XtgvHostStorage::open(const char *name, int flags) {
    XtgvHostFile result;
    auto path = resolve(name);
    if (path.empty()) return result;
    const int accepted = O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND | O_EXCL;
    if ((flags & ~accepted) || (flags & (O_WRONLY | O_RDWR)) == (O_WRONLY | O_RDWR))
        return result;
    auto state = std::make_shared<XtgvHostFile::State>();
    state->owner = this;
    state->path = path;
    state->flags = flags;
    std::error_code error;
    if (fs::is_directory(path, error)) {
        if (flags != O_RDONLY) return result;
        state->directory = true;
        fs::directory_iterator entries(path, error);
        if (error) return result;
        for (; entries != fs::directory_iterator(); entries.increment(error)) {
            if (error) return result;
            state->entries.push_back(entries->path());
        }
        if (error) return result;
        std::sort(state->entries.begin(), state->entries.end());
    } else {
        state->file = openFile(path, flags);
        if (!state->file) return result;
    }
    state->valid = true;
    if (handles_.size() >= 64)
        handles_.erase(std::remove_if(handles_.begin(), handles_.end(),
                       [](const auto &handle) { return handle.expired(); }), handles_.end());
    handles_.push_back(state);
    result.state_ = std::move(state);
    return result;
}
bool XtgvHostStorage::exists(const char *name) const {
    auto path = resolve(name);
    std::error_code error;
    return !path.empty() && fs::exists(path, error) && !error;
}
bool XtgvHostStorage::mkdir(const char *name) {
    auto path = resolve(name);
    std::error_code error;
    return !path.empty() && fs::create_directories(path, error) && !error;
}
bool XtgvHostStorage::remove(const char *name) {
    auto path = resolve(name);
    std::error_code error;
    if (path.empty() || path == root_ || fs::is_directory(path, error)) return false;
    return fs::remove(path, error) && !error;
}
bool XtgvHostStorage::rename(const char *from, const char *to) {
    auto source = resolve(from), target = resolve(to);
    std::error_code error;
    if (source.empty() || target.empty() || source == root_ ||
        fs::exists(target, error) || error) return false;
    fs::rename(source, target, error);
    return !error;
}
bool XtgvHostStorage::closeAll() {
    bool ok = true;
    for (auto &handle : handles_) {
        if (auto state = handle.lock()) {
            if (state->file && std::fclose(state->file) != 0) ok = false;
            state->file = nullptr;
            state->valid = false;
        }
    }
    handles_.clear();
    return ok;
}
XtgvHostFile::operator bool() const { return state_ && state_->valid; }
bool XtgvHostFile::isDirectory() const { return bool(*this) && state_->directory; }
uint64_t XtgvHostFile::size() const {
    if (!*this || isDirectory()) return 0;
    std::error_code error;
    if ((state_->flags & (O_WRONLY | O_RDWR)) && std::fflush(state_->file) != 0) return 0;
    const auto bytes = fs::file_size(state_->path, error);
    return error ? 0 : bytes;
}
int XtgvHostFile::available() const {
    if (!*this || isDirectory()) return 0;
    const int64_t position = filePosition(state_->file);
    const uint64_t length = size();
    if (position < 0 || uint64_t(position) >= length) return 0;
    return int(std::min<uint64_t>(length - uint64_t(position), INT_MAX));
}
int XtgvHostFile::read() {
    if (!*this || isDirectory() || (state_->flags & O_WRONLY)) return -1;
    return std::fgetc(state_->file);
}
int XtgvHostFile::read(void *bytes, size_t count) {
    if (!*this || isDirectory() || (state_->flags & O_WRONLY)) return -1;
    const size_t read = std::fread(bytes, 1, std::min<size_t>(count, INT_MAX), state_->file);
    return std::ferror(state_->file) ? -1 : int(read);
}
size_t XtgvHostFile::write(uint8_t byte) { return write(&byte, 1); }
size_t XtgvHostFile::write(const void *bytes, size_t count) {
    if (!*this || isDirectory() || !(state_->flags & (O_WRONLY | O_RDWR))) return 0;
    return std::fwrite(bytes, 1, count, state_->file);
}
bool XtgvHostFile::seek(uint64_t position) {
    return bool(*this) && !isDirectory() && seekFile(state_->file, position);
}
bool XtgvHostFile::flush() {
    return bool(*this) && !isDirectory() && std::fflush(state_->file) == 0;
}
void XtgvHostFile::close() {
    if (state_) {
        if (state_->file) std::fclose(state_->file);
        state_->file = nullptr;
        state_->valid = false; // aliases observe explicit close, no double-close
    }
    state_.reset();
}
bool XtgvHostFile::truncate(uint64_t length) {
    if (!*this || isDirectory() || !(state_->flags & (O_WRONLY | O_RDWR)) ||
        length > uint64_t(std::numeric_limits<int64_t>::max()) || !flush()) return false;
#if defined(_WIN32)
    return _chsize_s(_fileno(state_->file), length) == 0;
#else
    return ftruncate(fileno(state_->file), off_t(length)) == 0;
#endif
}
bool XtgvHostFile::rename(const char *name) {
    if (!*this || isDirectory()) return false;
    auto destination = state_->owner->resolve(name);
    if (destination.empty() || !flush()) return false;
    const auto position = filePosition(state_->file);
    std::fclose(state_->file);
    state_->file = nullptr;
    const auto relative = state_->path.lexically_relative(state_->owner->root_);
    const bool renamed = state_->owner->rename(relative.generic_string().c_str(), name);
    if (renamed) state_->path = destination;
    state_->file = openFile(state_->path, state_->flags & ~(O_CREAT | O_TRUNC | O_EXCL));
    state_->valid = state_->file != nullptr;
    if (state_->file && position >= 0) seekFile(state_->file, uint64_t(position));
    return renamed && state_->valid;
}
bool XtgvHostFile::getName(char *name, size_t capacity) const {
    if (!*this || !name || !capacity) return false;
    const auto filename = state_->path.filename().string();
    const auto count = std::min(filename.size(), capacity - 1);
    std::memcpy(name, filename.data(), count);
    name[count] = 0;
    return count == filename.size();
}
XtgvHostFile XtgvHostFile::openNextFile() {
    if (!isDirectory() || state_->next == state_->entries.size()) return {};
    const auto relative = state_->entries[state_->next++].lexically_relative(state_->owner->root_);
    return state_->owner->open(relative.generic_string().c_str());
}
