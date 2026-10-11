#include "storage.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *what) {
    if (!condition) throw std::runtime_error(what);
}
int main() {
    namespace fs = std::filesystem;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() /
        fs::u8path("xtgv-storage-fixture-" + std::to_string(unique) + u8" spaces žluťoučký");
    fs::create_directory(root);
    try {
        XtgvHostStorage storage;
        require(storage.begin(root), "mount fixture root");
        require(storage.mkdir("/A/0"), "create card-relative directories");
        require(!storage.open("../escape", O_CREAT | O_WRITE), "reject traversal");
        require(!storage.open("Z:/escape", O_CREAT | O_WRITE), "reject host drive or absent parent");
        const uint8_t binary[] = {0, 0x1a, 0xff, 'X'};
        auto file = storage.open("/A/0/TEST.DAT", O_CREAT | O_WRITE);
        require(bool(file) && file.write(binary, sizeof(binary)) == sizeof(binary), "binary write");
        auto alias = file;
        require(file.flush() && file.size() == 4, "flush and size");
        file.close();
        require(!alias, "explicit close invalidates copied handle");
        file = storage.open("A/0/TEST.DAT", O_CREAT | O_WRITE);
        require(file.size() == 4, "create+write does not implicitly truncate");
        require(file.seek(128) && file.write(uint8_t('R')) == 1, "random record position");
        require(file.flush() && file.size() == 129, "file extension");
        file.close();
        file = storage.open("./A/0/TEST.DAT", O_READ);
        uint8_t read[4] = {};
        require(file.read(read, 4) == 4 && std::memcmp(read, binary, 4) == 0, "binary round-trip");
        require(file.seek(128) && file.read() == 'R' && file.read() == -1, "seek/EOF");
        require(file.write(uint8_t('!')) == 0 && !file.truncate(0), "read-only handle refuses writes");
        file.close();
        file = storage.open("A/0/TEST.DAT", O_RDWR);
        require(file.truncate(4) && file.size() == 4, "truncate");
        require(file.rename("A/0/MOVED.DAT") && storage.exists("/A/0/MOVED.DAT"), "open-handle rename");
        file.close();
        file = storage.open("A/0/MOVED.DAT", O_WRITE | O_APPEND);
        require(file.seek(0) && file.write(uint8_t('A')) == 1 && file.flush(), "append ignores seek for writes");
        require(file.size() == 5, "append preserves existing bytes");
        file.close();
        auto directory = storage.open("A/0");
        auto entry = directory.openNextFile();
        char name[13];
        require(bool(entry) && entry.getName(name, sizeof(name)) &&
                std::strcmp(name, "MOVED.DAT") == 0, "directory enumeration/name");
        require(!directory.openNextFile(), "directory exhaustion");
        entry.close(); directory.close();
        file = storage.open("A/0/7XTGVTRM.NEW", O_CREAT | O_WRITE);
        require(bool(file) && file.write(binary, sizeof(binary)) == 4 && file.flush(), "save candidate");
        file.close();
        require(storage.rename("A/0/MOVED.DAT", "A/0/7XTGVTRM.BAK") &&
                storage.rename("A/0/7XTGVTRM.NEW", "A/0/7XTGVTRM.INI"), "NEW/BAK transaction");
        require(!storage.rename("A/0/7XTGVTRM.BAK", "A/0/7XTGVTRM.INI"), "no overwrite on rename");
        std::error_code error;
        fs::create_directory_symlink(root.parent_path(), root / "escape-link", error);
        if (!error) require(!storage.open("escape-link/outside"), "reject symlink escape");
        std::cout << "Host File/SD adapter: PASS; isolated fixture=" << root.u8string() << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "; fixture retained at " << root.u8string() << '\n';
        return 1;
    }
    return 0;
}
