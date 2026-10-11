// Standalone diagnostic: include the real adapter in this translation unit so
// the native baseline can also use its exact private open/seek helpers.
// No emulator, SDL, source card or production behavior changes.
#include "../storage.cpp"
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using Clock = std::chrono::steady_clock;
enum Part { Open, Size, Seek, Read, Write, Close, Path, PartCount };
struct Times {
    std::array<uint64_t, PartCount> ns{}, calls{};
    template<class F> auto measure(Part part, F action) {
        const auto start = Clock::now();
        auto finish = [&] {
            ns[part] += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
            ++calls[part];
        };
        if constexpr (std::is_void_v<decltype(action())>) {
            action(); finish();
        } else {
            auto result = action(); finish(); return result;
        }
    }
    void report(int trial, const char *name, Clock::time_point start) const {
        const double total = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        std::cout << trial << ',' << name << ',' << std::fixed << std::setprecision(3) << total;
        for (auto value : ns) std::cout << ',' << value / 1e6;
        for (auto count : calls) std::cout << ',' << count;
        std::cout << std::endl;
    }
};
static void require(bool ok, const char *what) {
    if (!ok) throw std::runtime_error(what);
}
static std::array<uint8_t, 128> pattern() {
    std::array<uint8_t, 128> bytes{};
    for (size_t n = 0; n < bytes.size(); ++n) bytes[n] = uint8_t(n);
    return bytes;
}
static void seed(const fs::path &path) {
    FILE *file = openFile(path, O_CREAT | O_TRUNC | O_RDWR);
    require(file != nullptr, "create diagnostic file");
    require(std::fclose(file) == 0, "close new diagnostic file");
}

static void adapter(XtgvHostStorage &storage, const char *name, bool writing,
                    bool held, int trial) {
    const auto expected = pattern();
    std::array<uint8_t, 128> bytes{};
    Times t;
    XtgvHostFile file;
    const auto start = Clock::now();
    auto open = [&](int flags) {
        auto f = t.measure(Open, [&] { return storage.open(name, flags); });
        require(bool(f), "adapter open");
        return f;
    };
    if (held) file = open(writing ? O_RDWR : O_READ);
    for (size_t record = 0; record < 256; ++record) {
        const auto position = record * expected.size();
        if (!held && writing) {
            // Mirror the sequential _sys_extendfile check. This fixture never
            // leaves a gap, so no zero filling is required.
            auto extension = open(O_WRITE | O_APPEND);
            require(t.measure(Size, [&] { return extension.size(); }) == position, "adapter extension size");
            t.measure(Close, [&] { extension.close(); });
        }
        if (!held) file = open(writing ? O_RDWR : O_READ);
        require(t.measure(Seek, [&] { return file.seek(position); }), "adapter seek");
        if (writing) {
            require(t.measure(Write, [&] { return file.write(expected.data(), expected.size()); })
                    == expected.size(), "adapter write");
        } else {
            require(t.measure(Read, [&] { return file.read(bytes.data(), bytes.size()); }) == int(bytes.size()), "adapter read");
            require(bytes == expected, "adapter read data");
        }
        if (!held) t.measure(Close, [&] { file.close(); });
    }
    if (held) t.measure(Close, [&] { file.close(); });
    t.report(trial, held ? (writing ? "adapter-held-write" : "adapter-held-read") :
                          (writing ? "adapter-record-write" : "adapter-record-read"), start);
}

static void native(const fs::path &path, bool writing, bool held, int trial) {
    const auto expected = pattern();
    std::array<uint8_t, 128> bytes{};
    Times t;
    FILE *file = nullptr;
    const auto start = Clock::now();
    auto open = [&](int flags) {
        FILE *f = t.measure(Open, [&] { return openFile(path, flags); });
        require(f != nullptr, "native open");
        return f;
    };
    auto close = [&](FILE *f) {
        require(t.measure(Close, [&] { return std::fclose(f); }) == 0, "native close");
    };
    if (held) file = open(writing ? O_RDWR : O_READ);
    for (size_t record = 0; record < 256; ++record) {
        const auto position = record * expected.size();
        if (!held && writing) {
            FILE *extension = open(O_WRITE | O_APPEND);
            const auto size = t.measure(Size, [&] {
                require(std::fflush(extension) == 0, "native size flush");
                return fs::file_size(path);
            });
            require(size == position, "native extension size");
            close(extension);
        }
        if (!held) file = open(writing ? O_RDWR : O_READ);
        require(t.measure(Seek, [&] { return seekFile(file, position); }), "native seek");
        if (writing) {
            require(t.measure(Write, [&] { return std::fwrite(expected.data(), 1, expected.size(), file); })
                    == expected.size(), "native write");
        } else {
            require(t.measure(Read, [&] { return std::fread(bytes.data(), 1, bytes.size(), file); }) == bytes.size(), "native read");
            require(bytes == expected, "native read data");
        }
        if (!held) close(file);
    }
    if (held) close(file);
    t.report(trial, held ? (writing ? "native-held-write" : "native-held-read") :
                          (writing ? "native-record-write" : "native-record-read"), start);
}

static void metadata(const fs::path &path, int trial) {
    Times t;
    const auto start = Clock::now();
    for (int n = 0; n < 256; ++n) {
        require(!t.measure(Path, [&] { return fs::weakly_canonical(path); }).empty(), "canonical path");
        require(!t.measure(Open, [&] { return fs::is_directory(path); }), "file metadata");
        require(t.measure(Size, [&] { return fs::file_size(path); }) == 32768, "file length");
    }
    // For this row only, open_ms means is_directory metadata, not native open.
    t.report(trial, "metadata-open-is-directory", start);
}

static int run(const fs::path &parent) {
    try {
        const auto root = parent / ("vt-storage-profile-" + std::to_string(Clock::now().time_since_epoch().count()));
        require(fs::create_directory(root), "create fresh diagnostic directory");
        fs::create_directories(root / "A/0");
        XtgvHostStorage storage;
        require(storage.begin(root), "mount diagnostic root");
        std::cout << "ROOT=" << root.u8string() << '\n';
        std::cout << "trial,case,total_ms,open_ms,size_ms,seek_ms,read_ms,write_ms,close_ms,path_ms,"
                     "open_calls,size_calls,seek_calls,read_calls,write_calls,close_calls,path_calls\n";
        for (int trial = 0; trial < 3; ++trial) {
            for (bool held : {false, true}) {
                const auto path = root / "A/0/VTADAPT.DAT";
                seed(path);
                adapter(storage, "A/0/VTADAPT.DAT", true, held, trial);
                require(fs::file_size(path) == 32768, "adapter final write size");
                adapter(storage, "A/0/VTADAPT.DAT", false, held, trial);
                const auto raw = root / "A/0/VTNATIVE.DAT";
                seed(raw);
                native(raw, true, held, trial);
                require(fs::file_size(raw) == 32768, "native final write size");
                native(raw, false, held, trial);
            }
            metadata(root / "A/0/VTADAPT.DAT", trial);
        }
        std::cout << "PASS: all 32-KiB readbacks verified; fixtures retained.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
    if (argc != 2) {
        std::cerr << "Pass an existing parent directory for a fresh diagnostic fixture.\n";
        return 2;
    }
    return run(fs::path(argv[1]));
}
