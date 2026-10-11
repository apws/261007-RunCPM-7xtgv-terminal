#define XTGV_TERMINAL 1
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdlib.h>
#include <map>
#include <string>

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;

#define FOLDERCHAR '/'
#define O_READ 1
#define O_WRITE 2
#define O_CREAT 4
#define O_APPEND 8
#define DRV_SET 14
#define F_USERNUM 32

static std::map<std::string, std::string> files;
static std::string output;
static uint8 currentDrive = 0;
static uint8 currentUser = 7;
static uint8 pageSize = 22;
static uint8 mockKey = 0;
static int pagePauses = 0;
static uint16 inBuf = 0x100;
static char ram[512] = {};
static bool extraArea = false;

struct File {
    std::string content;
    std::string name;
    std::string path;
    size_t cursor = 0;
    bool open = false;
    bool directory = false;
    explicit operator bool() const { return open; }
    int read() {
        return cursor < content.size() ? (uint8)content[cursor++] : -1;
    }
    void close() { open = false; }
    void flush() {}
    size_t write(const uint8_t *buffer, size_t count) {
        if (!open || path.empty()) return 0;
        files[path].append(reinterpret_cast<const char *>(buffer), count);
        return count;
    }
    bool seek(size_t position) {
        if (position > content.size()) return false;
        cursor = position;
        return true;
    }
    bool isDirectory() const { return directory; }
    uint32 size() const { return content.size(); }
    void getName(char *name, size_t capacity) const {
        if (capacity) {
            strncpy(name, this->name.c_str(), capacity - 1);
            name[capacity - 1] = 0;
        }
    }
    File openNextFile() {
        if (!directory) return File();
        const std::string prefix = path + "/";
        size_t index = 0;
        for (const auto &item : files) {
            if (item.first.compare(0, prefix.size(), prefix) != 0 ||
                item.first.find('/', prefix.size()) != std::string::npos)
                continue;
            if (index++ != cursor) continue;
            ++cursor;
            File result;
            result.open = true;
            result.name = item.first.substr(prefix.size());
            result.content = item.second;
            return result;
        }
        return File();
    }
};

struct MockSD {
    File open(const char *path, int flags) {
        if (!strcmp(path, "A/0") || !strcmp(path, "A/7") ||
            !strcmp(path, "B/0") || (extraArea && !strcmp(path, "A/8"))) {
            File result;
            result.open = true;
            result.directory = true;
            result.path = path;
            return result;
        }
        auto found = files.find(path);
        if (found == files.end() && !(flags & O_CREAT)) return File();
        File result;
        if (found != files.end()) result.content = found->second;
        else files[path] = "";
        result.open = true;
        result.path = path;
        return result;
    }
    bool exists(const char *path) const { return files.count(path) != 0; }
    bool remove(const char *path) { return files.erase(path) != 0; }
    bool rename(const char *from, const char *to) {
        auto found = files.find(from);
        if (found == files.end()) return false;
        files[to] = found->second;
        files.erase(found);
        return true;
    }
} SD;

static void _putcon(char ch) { output += ch; }
static uint8 _RamRead(uint16 address) { return ram[address]; }
static uint16 _ccp_bdos(uint8, uint16) { return 0; }
static void _ccp_askForKey() { ++pagePauses; }
static uint16_t mockLogicalColumns = 80;
static uint16_t terminalLogicalColumns() { return mockLogicalColumns; }
#define HIGH_REGISTER(reg) mockKey
static uint8 _ccp_strEqual(const char *a, const char *b) {
    return !strcmp(a, b);
}

static bool failAllocation = false;
static void *catalogRealloc(void *pointer, size_t size) {
    return failAllocation ? nullptr : std::realloc(pointer, size);
}
#define realloc catalogRealloc
#include "../ccp_catalog.h"
#undef realloc

int main() {
    files["A/0/VOL.7XF"] = "a CP/M system and programs\r\n\x1a";
    files["A/7/DIR.7XF"] =
        "mbasic       MBASIC programs and terminal visual tests\r\n"
        "vtests.bas   ANSI and DEC test menu\r\n"
        "VTPAL.BAS    Compare inks and papers\r\n\x1a";
    files["A/7/VTESTS.BAS"] = std::string(65536, 'x');
    files["A/7/VTPAL.BAS"] = std::string(129, 'x');
    files["A/8/DIR.7XF"] = "DEMOS        Demos and games\r\n\x1a";
    files["A/0/DIR.7XF"] = "InfoText for Drive A0:\r\n\x1a";
    files["B/0/TEST.DAT"] = std::string(1025, 'b');
    files["A/0/DRV.7XF"] = "/a/0\r\n/b/0\r\n\x1a";

    char alias[13], description[81];
    assert(_ccp_catArea(0, 7, alias, description));
    assert(!strcmp(alias, "mbasic"));
    assert(!strcmp(description, "MBASIC programs and terminal visual tests"));
    assert(_ccp_catDescription(0, 7, "VTESTS.BAS", description));
    assert(!strcmp(description, "ANSI and DEC test menu"));
    assert(!_ccp_catDescription(0, 7, "MISSING.COM", description));
    assert(_ccp_catDescription(0, 7, "vtests.bas", description));
    assert(_ccp_catResolveAlias(0, "MBASIC") == 7);
    files["A/6/DIR.7XF"] = std::string(140, 'X') +
                            "\r\nNOTALIAS     This is a file line\r\n\x1a";
    assert(!_ccp_catArea(0, 6, alias, description));
    assert(!_ccp_catArea(0, 0, alias, description));
    files["A/A/DIR.7XF"] = "TOOLONG99    Invalid alias\r\n\x1a";
    assert(!_ccp_catArea(0, 10, alias, description));

    uint8 drive = 0, user = 0;
    assert(_ccp_catTarget("/A/MBASIC", &drive, &user) == 0);
    assert(drive == 0 && user == 7);
    assert(_ccp_catTarget("A15:", &drive, &user) == 0);
    assert(drive == 0 && user == 15);
    assert(_ccp_catTarget("A16:", &drive, &user) == -3);
    assert(_ccp_catTarget("/A/UNKNOWN", &drive, &user) == -1);
    assert(_ccp_catTarget("/A/7", &drive, &user) == 0 && user == 7);
    assert(_ccp_catTarget("", &drive, &user) == 0);
    assert(drive == currentDrive && user == currentUser);

    output.clear();
    char testName[] = "VTESTS.BAS";
    assert(_ccp_catFilename(testName));
    assert(_ccp_catMatch(testName, "*.*"));
    assert(_ccp_catMatch(testName, "*"));
    assert(_ccp_catMatch("README", "*"));
    assert(_ccp_catMatch(testName, "vt*"));
    assert(_ccp_catMatch("VT", "VT*"));
    assert(!_ccp_catMatch("OTHER.BAS", "VT*"));
    _ccp_catListFiles(0, 7);
    assert(output.find("\r\n MBASIC programs") != std::string::npos);
    assert(output.find("/a/mbasic") == std::string::npos);
    assert(output.find("64k  vtests.bas") !=
           std::string::npos);
    assert(output.find("ANSI and DEC test menu") == std::string::npos);
    assert(currentDrive == 0 && currentUser == 7);
    output.clear();
    _ccp_catFileRow("LIMIT.BIN", "", 65535, false, NULL);
    assert(output.find("64k  limit.bin") != std::string::npos);
    output.clear();
    _ccp_catFileRow("LIMIT.BIN", "", 65536, false, NULL);
    assert(output.find("64k  limit.bin") != std::string::npos);
    output.clear();
    _ccp_catFileRow("LARGE.BIN", "", 1048576, false, NULL);
    assert(output.find("1,024k  large.bin") != std::string::npos);
    mockLogicalColumns = 40;
    output.clear();
    _ccp_catFileRow("VTESTS.BAS", "This description must not wrap", 10368,
                    true, NULL);
    assert(output.find("\r\n") == 39);
    assert(output.find("description must") == std::string::npos);
    output.clear();
    _CcpCatTotals largeTotals;
    largeTotals.addFile(1048576);
    _ccp_catCatalogRow(largeTotals, "/a/mbasic",
                       "This description must not wrap", NULL, true);
    assert(output.find("/a/mbasic     This") != std::string::npos);
    assert(output.find("\r\n") == 39);
    assert(output.find('\t') == std::string::npos);
    mockLogicalColumns = 80;
    pageSize = 2;
    mockKey = 3;
    output.clear();
    _ccp_catListFiles(0, 7);
    assert(pagePauses == 1);
    assert(output.find("vtests.bas") == std::string::npos);
    pageSize = 22;
    mockKey = 0;

    output.clear();
    _ccp_catListAreas(0);
    assert(output.find("\r\n CP/M system and programs\r\n") == 0);
    assert(output.find("65k /a/mbasic\r\n") != std::string::npos);
    assert(output.find("MBASIC programs") == std::string::npos);
    output.clear();
    _ccp_catListAreas(0, true);
    assert(output.find("\r\n CP/M system and programs\r\n") == 0);
    assert(output.find("/a/mbasic     MBASIC programs") !=
           std::string::npos);
    assert(output.find("516    4 /a/mbasic") !=
           std::string::npos);
    assert(output.find("/a/0") != std::string::npos);
    assert(output.find("/a/6") == std::string::npos);
    assert(output.find("/a/DEMOS") == std::string::npos);
    extraArea = true;
    files["A/8/.gitkeep"] = "";
    files["A/8/.gitignore"] = "placeholder";
    assert(!_ccp_catAreaTotals(0, 8).hasContent);
    assert(_ccp_catResolveAlias(0, "DEMOS") == 8);
    // Direct listing remains available, even when the area overview hides it.
    output.clear();
    _ccp_catListFiles(0, 8);
    assert(output.find("/a/DEMOS  Demos and games") != std::string::npos);
    files["A/8/ZERO.COM"] = "";
    output.clear();
    _ccp_catListAreas(0);
    assert(output.find("/a/DEMOS") != std::string::npos);
    files.erase("A/8/ZERO.COM");
    files.erase("A/8/.gitkeep");
    files.erase("A/8/.gitignore");
    extraArea = false;

    output.clear();
    _ccp_catListDrives();
    assert(output.find("65k /a            CP/M system and programs") != std::string::npos);
    output.clear();
    _ccp_catListDrives(true);
    assert(output.find("/a            CP/M system and programs") !=
           std::string::npos);
    assert(output.find("/b            --") != std::string::npos);
    assert(output.find("/c") == std::string::npos);
    output.clear();
    _ccp_catCatalogRow(largeTotals, "/a/10", "", NULL);
    assert(output.find("1,024k /a/10") != std::string::npos);
    char numberedPath[17];
    _ccp_catDisplayPath(numberedPath, 0, 10, NULL);
    assert(!strcmp(numberedPath, "/a/10"));
    strcpy(ram + inBuf + 2, "LS /");
    assert(_ccp_ls() == 0);
    output.clear();
    strcpy(ram + inBuf + 2, "LS ..");
    assert(_ccp_ls() == 0);
    assert(output.find("\r\n CP/M system and programs\r\n") == 0);
    assert(output.find("/a/mbasic") != std::string::npos);
    assert(output.find("/b") == std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS A7:");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS -L A7:*.*");
    assert(_ccp_ls() == 0);
    assert(output.find("65,536   512    2  vtests.bas") != std::string::npos);
    assert(output.find("129     2    1  vtpal.bas") != std::string::npos);
    assert(output.find("vtests.bas    ANSI and DEC test menu") !=
           std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LL /A");
    assert(_ccp_ls() == 0);
    assert(output.find("/a/mbasic     MBASIC programs") !=
           std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS / -L");
    assert(_ccp_ls() == 0);
    assert(output.find("/a            CP/M system and programs") !=
           std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS /A/MBASIC/*.*");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    assert(output.find("vtpal.bas") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS *");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    assert(output.find("vtpal.bas") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS /A/MBASIC/*");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    assert(output.find("vtpal.bas") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS A7:*");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    assert(output.find("vtpal.bas") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS VT*");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    assert(output.find("vtpal.bas") != std::string::npos);
    assert(output.find("dir.7xf") == std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS /A/MBASIC/VTEST*");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    assert(output.find("vtpal.bas") == std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS A7:VT*");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    assert(output.find("vtpal.bas") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS /A/MBASIC/VTESTS.*");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    assert(output.find("vtpal.bas") == std::string::npos);
    assert(output.find("ANSI and DEC test menu") == std::string::npos);
    output.clear();
    files["A/7/REPORT.7XF"] = "old content";
    pageSize = 2;
    strcpy(ram + inBuf + 2, "LS -O REPORT.7XF /A/MBASIC/VTESTS.*");
    const int pausesBeforeCapture = pagePauses;
    assert(_ccp_ls() == 0);
    assert(output.empty());
    assert(pagePauses == pausesBeforeCapture);
    assert(files["A/7/REPORT.7XF"].find("vtests.bas") !=
           std::string::npos);
    assert(files["A/7/REPORT.7XF"].find("old content") ==
           std::string::npos);
    const std::string firstResult = files["A/7/REPORT.7XF"];
    assert(_ccp_ls() == 0);
    assert(files["A/7/REPORT.7XF"] == firstResult);
    output.clear();
    strcpy(ram + inBuf + 2, "LL /A/MBASIC/VTESTS.* -O REPORT.7XF");
    assert(_ccp_ls() == 0);
    assert(output.empty());
    assert(files["A/7/REPORT.7XF"] != firstResult);
    assert(files["A/7/REPORT.7XF"].find("65,536") != std::string::npos);
    const std::string longResult = files["A/7/REPORT.7XF"];
    pageSize = 22;
    output.clear();
    strcpy(ram + inBuf + 2, "LS /A/MBASIC/VTESTS.*");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    assert(files["A/7/REPORT.7XF"] == longResult);
    output.clear();
    strcpy(ram + inBuf + 2, "LS -O DIR.7XF /A/MBASIC");
    assert(_ccp_ls() == 0);
    assert(output.find("Usage: LS") != std::string::npos);
    assert(files["A/7/DIR.7XF"].find("MBASIC") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS -L");
    assert(_ccp_ls() == 0);
    assert(output.find("vtests.bas") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS -S");
    assert(_ccp_ls() == 0);
    assert(output.find("Usage: LS") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS -W");
    assert(_ccp_ls() == 0);
    assert(output.find("Usage: LS") != std::string::npos);
    output.clear();
    strcpy(ram + inBuf + 2, "LS /Z");
    assert(_ccp_ls() == 0);
    assert(output.find("Usage: LS") != std::string::npos);
    strcpy(ram + inBuf + 2, "CD");
    assert(_ccp_cd() == 0);
    assert(output.find("A7: /a/mbasic") != std::string::npos);
    currentUser = 0;
    strcpy(ram + inBuf + 2, "CD MBASIC");
    assert(_ccp_cd() == 0);
    assert(currentDrive == 0 && currentUser == 7);
    currentUser = 0;
    strcpy(ram + inBuf + 2, "CD /A/MBASIC");
    assert(_ccp_cd() == 0);
    assert(currentDrive == 0 && currentUser == 7);
    assert(files["A/0/DRV.7XF"].find("/a/mbasic\r\n") != std::string::npos);
    currentUser = 0;
    strcpy(ram + inBuf + 2, "CD /A");
    assert(_ccp_cd() == 0);
    assert(currentDrive == 0 && currentUser == 7);
    strcpy(ram + inBuf + 2, "CD /A/UNKNOWN");
    assert(_ccp_cd() == 0);
    assert(currentDrive == 0 && currentUser == 7);
    strcpy(ram + inBuf + 2, "CD A8:");
    assert(_ccp_cd() == 0);
    assert(currentDrive == 0 && currentUser == 7);
    files["A/9/DIR.7XF"] = "MBASIC       Duplicate alias\r\n\x1a";
    assert(_ccp_catTarget("/A/MBASIC", &drive, &user) == -2);

    // '..' lists the current drive's areas without changing the selected DU.
    for (const char *verb : {"LS", "LL"}) {
        for (uint8 selectedDrive : {uint8(0), uint8(1)}) {
            currentDrive = selectedDrive;
            currentUser = 0;
            output.clear();
            snprintf(ram + inBuf + 2, 100, "%s ..", verb);
            assert(_ccp_ls() == 0);
            const std::string parentListing = output;
            output.clear();
            snprintf(ram + inBuf + 2, 100, "%s /%c", verb, 'A' + selectedDrive);
            assert(_ccp_ls() == 0);
            assert(output == parentListing);
            assert(currentDrive == selectedDrive && currentUser == 0);
        }
    }

    // File names remain NAME.EXT; sort by uppercase extension, then filename.
    currentDrive = 0;
    currentUser = 7;
    pageSize = 0;
    files["A/7/ZZNONE"] = "";
    files["A/7/ZZ.ASM"] = "";
    files["A/7/AB.COM"] = "";
    files["A/7/aa.com"] = "";
    files["A/7/AA.Z80"] = "";
    files["A/7/TOOLONG99.COM"] = "ignored invalid guest name";
    for (const char *command : {"LS", "LL", "LS -L", "LS A7:*.*", "LL /A/7"}) {
        output.clear();
        strcpy(ram + inBuf + 2, command);
        assert(_ccp_ls() == 0);
        size_t previous = 0;
        for (const char *name : {"zznone", "dir.7xf", "report.7xf", "zz.asm",
                 "vtests.bas", "vtpal.bas", "aa.com", "ab.com", "aa.z80"}) {
            size_t position = output.find(name);
            assert(position != std::string::npos && position > previous);
            previous = position;
        }
        assert(output.find("TOOLONG99") == std::string::npos);
    }
    output.clear();
    strcpy(ram + inBuf + 2, "LL A7:*.COM -O SORTED.7XF");
    assert(_ccp_ls() == 0 && output.empty());
    assert(files["A/7/SORTED.7XF"].find("aa.com") <
           files["A/7/SORTED.7XF"].find("ab.com"));
    assert(files["A/7/SORTED.7XF"].find("zz.asm") == std::string::npos);

    // Growing the compact index must not truncate a larger directory.
    for (unsigned i = 0; i < 70; ++i) {
        char path[32];
        snprintf(path, sizeof path, "A/7/VT%02u.COM", i);
        files[path] = "x";
    }
    output.clear();
    _ccp_catListFiles(0, 7, "VT*.COM", true);
    assert(output.find("vt00.com") < output.find("vt69.com"));
    failAllocation = true;
    output.clear();
    _ccp_catListFiles(0, 7);
    assert(output.find("Not enough memory to sort listing") != std::string::npos);
    assert(output.find("vt00.com") == std::string::npos);
    failAllocation = false;

    // Width clips, never selects a second-line description layout.
    for (uint16 columns : {40, 80, 100}) {
        mockLogicalColumns = columns;
        output.clear();
        const std::string detail(80, 'x');
        _ccp_catCatalogRow(largeTotals, "/a/mbasic", detail.c_str(), nullptr, true);
        assert(output.find("/a/mbasic     x") != std::string::npos);
        assert(output.find("\r\n") == size_t(columns - 1));
        assert(output.find("\r\n") == output.rfind("\r\n"));
        assert(output.find('\t') == std::string::npos);
    }
    puts("7XF catalog: sorting, same-line descriptions, parent-drive, width, capture and paging PASS");
}
