#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>
#include <fcntl.h>

// Numeric flags stay consistent with this build's host API. The product only
// combines symbolic flags; no guest binary depends on their native values.
static constexpr int O_READ = O_RDONLY;
static constexpr int O_WRITE = O_WRONLY;
static constexpr int FILE_READ = O_RDONLY;

// Populated but currently never consumed by the product abstraction.
struct DirFat_t
{
};

class XtgvHostStorage;

class XtgvHostFile
{
public:
    XtgvHostFile() = default;
    explicit operator bool() const;
    bool isDirectory() const;
    uint64_t size() const;
    int available() const;
    int read();
    int read(void* bytes, size_t count);
    size_t write(uint8_t byte);
    size_t write(const void* bytes, size_t count);
    bool seek(uint64_t position);
    bool flush();
    void close();
    bool truncate(uint64_t length);
    bool rename(const char* path);
    bool getName(char* name, size_t capacity) const;
    XtgvHostFile openNextFile();

    void dirEntry(DirFat_t*) const
    {
    }

private:
    struct State;
    std::shared_ptr<State> state_;
    friend class XtgvHostStorage;
};

class XtgvHostStorage
{
public:
    XtgvHostStorage();
    ~XtgvHostStorage();
    XtgvHostStorage(const XtgvHostStorage&) = delete;
    XtgvHostStorage& operator=(const XtgvHostStorage&) = delete;
    bool begin(const std::filesystem::path& root);
    XtgvHostFile open(const char* path, int flags = O_RDONLY);
    bool exists(const char* path) const;
    bool mkdir(const char* path);
    bool remove(const char* path);
    bool rename(const char* from, const char* to);
    bool closeAll();

private:
    struct Retained;
    static constexpr size_t retainedLimit = 8;
    std::vector<std::unique_ptr<Retained>> retained_;
    bool closeFailed_ = false;
    std::vector<std::weak_ptr<XtgvHostFile::State>> handles_;
    std::filesystem::path root_;
    std::filesystem::path resolve(const char* path) const;
    std::unique_ptr<Retained> acquire(const std::filesystem::path& path, int flags);
    void retain(std::unique_ptr<Retained> handle);
    void discard(const std::filesystem::path& path);
    friend class XtgvHostFile;
};
