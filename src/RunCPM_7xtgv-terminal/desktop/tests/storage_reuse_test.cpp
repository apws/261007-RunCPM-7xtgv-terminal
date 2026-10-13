#include "storage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

static void require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

static void hostWrite(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    require(bool(out), "external write");
}

static std::string hostRead(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "external read");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static std::string read(XtgvHostFile& file)
{
    std::string text;
    for (int c; (c = file.read()) >= 0;)
        text += char(c);
    return text;
}

static size_t handleCount()
{
#if defined(_WIN32)
    DWORD count = 0;
    require(GetProcessHandleCount(GetCurrentProcess(), &count), "count native handles");
    return count;
#else
    return size_t(std::distance(fs::directory_iterator("/proc/self/fd"), fs::directory_iterator()));
#endif
}

int main()
{
    const auto root =
        fs::temp_directory_path() /
        fs::u8path("xtgv-reuse-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                   u8" žluťoučký");
    fs::create_directories(root / "A/0");
    try
    {
        XtgvHostStorage storage;
        require(storage.begin(root), "mount fixture");
        const auto path = root / "A/0/VTREUSE.DAT";
        auto f = storage.open("A/0/VTREUSE.DAT", O_CREAT | O_RDWR);
        require(bool(f) && f.write("abcdef", 6) == 6, "initial write");
        auto alias = f;
        f.close();
        require(!alias, "logical close invalidates aliases");
        require(hostRead(path) == "abcdef", "logical close flushes before native eviction");

        f = storage.open("A/0/VTREUSE.DAT");
        auto second = storage.open("A/0/VTREUSE.DAT");
        require(f.read() == 'a' && second.read() == 'a' && f.read() == 'b',
                "simultaneous opens have independent offsets");
        f.close();
        require(second.read() == 'b', "closing another open preserves stream");
        second.close();
        f = storage.open("A/0/VTREUSE.DAT");
        require(read(f) == "abcdef" && f.read() == -1, "first read reaches EOF");
        f.close();
        hostWrite(path, "12345678");
        f = storage.open("A/0/VTREUSE.DAT");
        require(read(f) == "12345678", "reopen discards stale buffering and EOF");
        f.close();

        auto appender = storage.open("A/0/VTREUSE.DAT", O_WRITE | O_APPEND);
        require(appender.seek(0) && appender.write(uint8_t('A')) == 1, "append ignores seek");
        appender.close();
        f = storage.open("A/0/VTREUSE.DAT", O_RDWR);
        require(f.seek(1) && f.write(uint8_t('B')) == 1, "update preserves its own mode");
        f.close();
        appender = storage.open("A/0/VTREUSE.DAT", O_WRITE | O_APPEND);
        require(appender.seek(0) && appender.write(uint8_t('C')) == 1, "reused append ignores seek");
        appender.close();
        require(hostRead(path) == "1B345678AC", "interleaved write modes and close visibility");
        require(!storage.open("A/0/VTREUSE.DAT", O_CREAT | O_EXCL | O_RDWR), "exclusive create not bypassed");
        f = storage.open("A/0/VTREUSE.DAT", O_CREAT | O_RDWR);
        require(f.size() == 10, "create existing preserves contents");
        require(f.truncate(3), "truncate with other idle modes");
        f.close();
        require(hostRead(path) == "1B3", "truncate visibility");

        // Keep size/time identical across replacement: identity, rather than
        // metadata freshness heuristics, must select the new file.
        const auto stamp = fs::last_write_time(path);
        fs::rename(path, root / "A/0/VTOLD.DAT");
        hostWrite(path, "NEW");
        fs::last_write_time(path, stamp);
        f = storage.open("A/0/VTREUSE.DAT", O_RDWR);
        require(read(f) == "NEW", "external replacement selects new file identity");
        require(f.seek(0) && f.write(uint8_t('!')) == 1, "write to replacement");
        f.close();
        require(hostRead(root / "A/0/VTOLD.DAT") == "1B3", "replaced old file not modified");
        require(hostRead(path) == "!EW", "new file modified");
        require(storage.rename("A/0/VTREUSE.DAT", "A/0/VTMOVED.DAT"), "adapter rename evicts idle handles");
        require(!storage.open("A/0/VTREUSE.DAT"), "renamed path absent");
        f = storage.open("A/0/VTMOVED.DAT", O_RDWR);
        require(f.rename("A/0/VTREUSE.DAT") && f.write(uint8_t('Q')) == 1, "open-file rename retains position");
        f.close();
        require(storage.remove("A/0/VTREUSE.DAT"), "adapter remove evicts idle handles");
        require(!storage.open("A/0/VTREUSE.DAT"), "deleted path absent");
        hostWrite(path, "again");
        f = storage.open("A/0/VTREUSE.DAT");
        f.close();
        fs::remove(path);
        require(!storage.open("A/0/VTREUSE.DAT"), "external deletion not hidden by retained handle");
        f = storage.open("A/0/VTREUSE.DAT", O_CREAT | O_EXCL | O_RDWR);
        require(bool(f) && f.size() == 0, "exclusive recreate after deletion");
        f.close();
        f = storage.open("A/0/VTREUSE.DAT", O_TRUNC | O_RDWR);
        require(bool(f) && f.size() == 0, "truncate open reaches filesystem");
        f.close();

        // Resolving each logical open still rejects a newly introduced escape.
        const auto outside = root.parent_path() / (root.filename().u8string() + "-outside");
        hostWrite(outside, "outside");
        fs::remove(path);
        std::error_code error;
        fs::create_symlink(outside, path, error);
        if (!error)
            require(!storage.open("A/0/VTREUSE.DAT"), "retained path cannot bypass symlink containment");

        require(storage.closeAll(), "close all before handle bound check");
        const auto baseline = handleCount();
        for (int n = 0; n < 64; ++n)
        {
            const auto name = "A/0/VT" + std::to_string(n) + ".DAT";
            auto entry = storage.open(name.c_str(), O_CREAT | O_RDWR);
            require(bool(entry), "many file opens");
            entry.close();
            require(handleCount() <= baseline + 8, "idle native handles remain bounded");
        }
        require(handleCount() > baseline, "reuse actually retains idle handles");
        f = storage.open("A/0/VT0.DAT");
        require(storage.closeAll() && !f, "closeAll invalidates active logical files");
        require(handleCount() == baseline, "closeAll releases all native handles");

        f = storage.open("A/0/VT0.DAT");
        fs::create_directories(root / "other/A/0");
        require(storage.begin(root / "other") && !f, "remount invalidates old handles");
        XtgvHostFile survivor;
        {
            XtgvHostStorage owner;
            require(owner.begin(root), "temporary owner mount");
            survivor = owner.open("A/0/VT0.DAT");
        }
        require(!survivor, "owner destruction invalidates surviving aliases");
        survivor.close();
        std::cout << "Desktop retained handles: PASS; fixture=" << root.u8string() << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << "; fixture=" << root.u8string() << '\n';
        return 1;
    }
    return 0;
}
