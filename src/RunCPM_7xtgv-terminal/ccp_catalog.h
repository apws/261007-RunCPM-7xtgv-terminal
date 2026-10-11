#ifndef CCP_CATALOG_H
#define CCP_CATALOG_H

#include <stdlib.h>

// RunCPM-only catalog over its A/0..F host folders. DIR.7XF
// uses a 12-character key, one space, then a description on every line.
// Neither LS nor alias lookup changes the selected CP/M drive/user.

static char _ccp_catHex(uint8 n) {
    return n < 10 ? '0' + n : 'A' + n - 10;
}

static bool _ccp_catEqual(const char *left, const char *right) {
    while (*left && *right) {
        if (toupper((unsigned char)*left++) !=
            toupper((unsigned char)*right++)) return false;
    }
    return !*left && !*right;
}

static void _ccp_catPath(char *path, uint8 drive, uint8 user,
                         const char *file) {
    path[0] = 'A' + drive;
    path[1] = FOLDERCHAR;
    path[2] = _ccp_catHex(user);
    path[3] = 0;
    if (file) {
        path[3] = FOLDERCHAR;
        path[4] = 0;
        strcat(path, file);
    }
}

static bool _ccp_catFolder(const char *path) {
    File folder = SD.open(path, O_READ);
    if (!folder) return false;
    bool valid = folder.isDirectory();
    folder.close();
    return valid;
}

// Reads CP/M CRLF text, including a final unterminated line.  A too-long
// line is discarded rather than becoming a misleading partial description.
static bool _ccp_catLine(File &file, char *line, uint8 capacity) {
    uint8 length = 0;
    bool overflow = false;
    for (;;) {
        int ch = file.read();
        if (ch < 0 || ch == 0x1a || ch == 0) {
            line[length] = 0;
            return length != 0 && !overflow;
        }
        if (ch == '\n') {
            line[overflow ? 0 : length] = 0;
            return true;
        }
        if (ch == '\r') continue;
        if (length + 1 < capacity) line[length++] = (char)ch;
        else overflow = true;
    }
}

static bool _ccp_catEntry(char *line, char *name, uint8 nameCapacity,
                          char **description) {
    if (strlen(line) < 13 || line[12] != ' ' || line[0] == ';' ||
        line[0] == '#') return false;
    uint8 length = 12;
    while (length && line[length - 1] == ' ') --length;
    if (!length || length >= nameCapacity) return false;
    for (uint8 i = 0; i < length; ++i) {
        if (line[i] == ' ' || line[i] == '\t') return false;
        name[i] = line[i];
    }
    name[length] = 0;
    *description = line + 13;
    return true;
}

static bool _ccp_catAreaName(const char *name) {
    uint8 length = 0;
    while (name[length]) {
        const unsigned char c = (unsigned char)name[length];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) return false;
        if (++length > 8) return false;
    }
    return length != 0;
}

// Only one LS/LL invocation can capture output. No redirection survives the
// command, and ordinary CCP or guest output never uses this sink.
static File _ccp_catCaptureFile;
static uint8_t _ccp_catCaptureBuffer[256];
static uint16 _ccp_catCaptureLength = 0;

static void _ccp_catCaptureFlush() {
    if (_ccp_catCaptureFile && _ccp_catCaptureLength) {
        _ccp_catCaptureFile.write(_ccp_catCaptureBuffer,
                                   _ccp_catCaptureLength);
        _ccp_catCaptureLength = 0;
    }
}

static void _ccp_catPutcon(char ch) {
    if (_ccp_catCaptureFile) {
        _ccp_catCaptureBuffer[_ccp_catCaptureLength++] = (uint8_t)ch;
        if (_ccp_catCaptureLength == sizeof _ccp_catCaptureBuffer)
            _ccp_catCaptureFlush();
    } else _putcon(ch);
}

static void _ccp_catPuts(const char *string) {
    while (*string) _ccp_catPutcon(*string++);
}

struct _CcpCatCapture {
    bool start(const char *name) {
        if (!name) return true;
        char path[32];
        _ccp_catPath(path, currentDrive, currentUser, name);
        if (SD.exists(path) && !SD.remove(path)) return false;
        _ccp_catCaptureLength = 0;
        _ccp_catCaptureFile = SD.open(path, O_CREAT | O_WRITE);
        return bool(_ccp_catCaptureFile);
    }
    ~_CcpCatCapture() {
        if (_ccp_catCaptureFile) {
            _ccp_catCaptureFlush();
            _ccp_catCaptureFile.flush();
            _ccp_catCaptureFile.close();
        }
    }
};

#define _putcon _ccp_catPutcon
#define _puts _ccp_catPuts

static bool _ccp_catPage(uint16 *lines) {
    if (_ccp_catCaptureFile) return true;
    if (!lines || !pageSize || ++*lines < pageSize) return true;
    *lines = 0;
    _ccp_askForKey();
    return HIGH_REGISTER(AF) != 3;
}

static bool _ccp_catEol(uint16 *lines) {
    _puts("\r\n");
    return _ccp_catPage(lines);
}

struct _CcpCatOutputRow {
    char data[160];
    uint8 length = 0;

    void put(char ch) {
        if (length < uint8(sizeof data - 1)) data[length++] = ch;
    }
    void append(const char *text) {
        while (*text) put(*text++);
    }
    void number(uint64_t value, uint8 width = 0) {
        char digits[21];
        uint8 count = 0;
        do {
            digits[count++] = '0' + value % 10;
            value /= 10;
        } while (value);
        for (uint8 i = count; i < width; ++i) put(' ');
        while (count) put(digits[--count]);
    }
    void grouped(uint64_t value, uint8 width = 0) {
        char digits[21];
        uint8 count = 0;
        do {
            digits[count++] = '0' + value % 10;
            value /= 10;
        } while (value);
        const uint8 printed = count + (count - 1) / 3;
        for (uint8 i = printed; i < width; ++i) put(' ');
        while (count) {
            put(digits[--count]);
            if (count && count % 3 == 0) put(',');
        }
    }
    bool emit(uint16 *lines) {
#if XTGV_TERMINAL
        const uint16 columns = terminalLogicalColumns();
#else
        const uint16 columns = 80;
#endif
        // Stop before the final column so terminal autowrap cannot move a
        // clipped description to the next physical line before CR/LF.
        const uint16 limit = columns > 1 ? columns - 1 : 79;
        uint16 column = 0;
        for (uint16 i = 0; i < length; ++i) {
            const uint16 next = data[i] == '\t' ? (column + 8) & ~uint16(7)
                                                   : column + 1;
            if (next > limit) break;
            _putcon(data[i]);
            column = next;
        }
        return _ccp_catEol(lines);
    }
};

static bool _ccp_catRow(const char *name, const char *description,
                        uint16 *lines = NULL) {
    _CcpCatOutputRow row;
    row.put(' ');
    row.append(name);
    if (description[0]) {
        if (name[0]) row.append("  ");
        row.append(description);
    }
    return row.emit(lines);
}

static bool _ccp_catArea(uint8 drive, uint8 user, char *alias,
                         char *description) {
    char path[20];
    char line[128];
    char *detail;
    _ccp_catPath(path, drive, user, "DIR.7XF");
    File file = SD.open(path, O_READ);
    if (!file) return false;
    bool found = _ccp_catLine(file, line, sizeof line) &&
                 _ccp_catEntry(line, alias, 13, &detail) &&
                 _ccp_catAreaName(alias);
    if (found) {
        strncpy(description, detail, 80);
        description[80] = 0;
    }
    file.close();
    return found;
}

static bool _ccp_catDescription(uint8 drive, uint8 user, const char *filename,
                                char *description) {
    char path[20];
    char line[128];
    char name[13];
    char *detail;
    _ccp_catPath(path, drive, user, "DIR.7XF");
    File file = SD.open(path, O_READ);
    if (!file) return false;
    // The first line describes the area, not a file.
    _ccp_catLine(file, line, sizeof line);
    bool found = false;
    while (_ccp_catLine(file, line, sizeof line)) {
        if (_ccp_catEntry(line, name, sizeof name, &detail) &&
            _ccp_catEqual(name, filename)) {
            strncpy(description, detail, 80);
            description[80] = 0;
            found = true;
            break;
        }
    }
    file.close();
    return found;
}

static int8_t _ccp_catResolveAlias(uint8 drive, const char *alias) {
    int8_t result = -1;
    char foundAlias[13];
    char description[81];
    for (uint8 user = 0; user < 16; ++user) {
        if (_ccp_catArea(drive, user, foundAlias, description) &&
            _ccp_catEqual(foundAlias, alias)) {
            if (result >= 0) return -2; // Duplicate aliases are unsafe for CD.
            result = user;
        }
    }
    return result;
}

static bool _ccp_catFilename(char *name) {
    uint8 stem = 0, ext = 0;
    bool dot = false;
    for (char *p = name; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '.') {
            if (dot || !stem) return false;
            dot = true;
        } else {
            if (ch < 33 || ch > 126 || ch == '/' || ch == '\\' ||
                ch == ':' || ch == '*' || ch == '?')
                return false;
            if (dot) ++ext;
            else ++stem;
        }
        *p = toupper(ch);
    }
    return stem && stem <= 8 && ext <= 3 && (!dot || ext);
}

// Expand each field like RunCPM's FCB parser: * fills the rest with ?, and
// ? consumes one character.  Compare space-padded 8.3 fields, not host globs.
static bool _ccp_catPatternField(const char *source, uint8 length,
                                 char *field, uint8 width) {
    uint8 i = 0;
    for (; i < width; ++i) field[i] = ' ';
    i = 0;
    for (uint8 p = 0; p < length; ++p) {
        char ch = toupper((unsigned char)source[p]);
        if (ch == '*') {
            while (i < width) field[i++] = '?';
            return true;
        }
        if (i >= width || ch < 33 || ch > 126 || ch == '/' || ch == ':' ||
            ch == '\\') return false;
        field[i++] = ch;
    }
    return i != 0;
}

static bool _ccp_catMatch(const char *filename, const char *pattern) {
    const char *nameDot = strchr(filename, '.');
    const char *patternDot = strchr(pattern, '.');
    if (strchr(pattern, '/') || strchr(pattern, ':') ||
        (patternDot && strchr(patternDot + 1, '.'))) return false;
    char nameField[11], patternField[11];
    const uint8 nameStem = nameDot ? nameDot - filename : strlen(filename);
    const uint8 patternStem = patternDot ? patternDot - pattern : strlen(pattern);
    if (!_ccp_catPatternField(filename, nameStem, nameField, 8) ||
        !_ccp_catPatternField(pattern, patternStem, patternField, 8)) return false;
    const char *nameExt = nameDot ? nameDot + 1 : "";
    // A stem wildcard without a dot also spans any extension: VT* matches
    // VTESTS.BAS as well as an extensionless VT file.
    const char *patternExt = patternDot ? patternDot + 1 :
                             strchr(pattern, '*') ? "*" : "";
    const uint8 nameExtLength = strlen(nameExt);
    const uint8 patternExtLength = strlen(patternExt);
    if (nameExtLength) {
        if (!_ccp_catPatternField(nameExt, nameExtLength, nameField + 8, 3))
            return false;
    } else memset(nameField + 8, ' ', 3);
    if (patternExtLength) {
        if (!_ccp_catPatternField(patternExt, patternExtLength,
                                  patternField + 8, 3)) return false;
    } else memset(patternField + 8, ' ', 3);
    for (uint8 i = 0; i < 11; ++i)
        if (patternField[i] != '?' && patternField[i] != nameField[i])
            return false;
    return true;
}

static bool _ccp_catFileRow(const char *name, const char *description,
                            uint32 bytes, bool longListing, uint16 *lines) {
    _CcpCatOutputRow row;
    row.put(' ');
    if (longListing) {
        row.grouped(bytes, 9);
        row.put(' ');
        const uint32 records = bytes / 128 + (bytes % 128 != 0);
        row.number(records, 5);
        row.put(' ');
        row.number(records / 256 + (records % 256 != 0), 4);
        row.append("  ");
    } else {
        row.grouped(bytes / 1024 + (bytes % 1024 != 0), 6);
        row.append("k  ");
    }
    uint8 length = 0;
    while (name[length]) row.put(tolower((unsigned char)name[length++]));
    if (longListing && description[0]) {
        while (length++ < 12) row.put(' ');
        row.append("  ");
        row.append(description);
    }
    return row.emit(lines);
}

static void _ccp_catDisplayPath(char *path, uint8 drive, uint8 user,
                                const char *alias) {
    path[0] = '/';
    path[1] = 'a' + drive;
    path[2] = '/';
    if (alias) {
        strcpy(path + 3, alias);
    } else if (user >= 10) {
        path[3] = '1';
        path[4] = '0' + user - 10;
        path[5] = 0;
    } else {
        path[3] = '0' + user;
        path[4] = 0;
    }
}

// Store only 8.3 names/sizes, not descriptions or open File objects. Sorting
// must not depend on FAT directory insertion order or the desktop filesystem.
struct _CcpCatFileEntry {
    char name[13];
    uint32 bytes;
};

static int _ccp_catFileCompare(const void *left, const void *right) {
    const char *a = static_cast<const _CcpCatFileEntry *>(left)->name;
    const char *b = static_cast<const _CcpCatFileEntry *>(right)->name;
    const char *aExt = strchr(a, '.');
    const char *bExt = strchr(b, '.');
    const int extension = strcmp(aExt ? aExt + 1 : "", bExt ? bExt + 1 : "");
    return extension ? extension : strcmp(a, b);
}

struct _CcpCatFileList {
    _CcpCatFileEntry *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;

    ~_CcpCatFileList() { free(entries); }
    bool add(const char *name, uint32 bytes) {
        if (count == capacity) {
            const size_t maximum = size_t(-1) / sizeof *entries;
            if (capacity > maximum / 2) return false;
            const size_t next = capacity ? capacity * 2 : 16;
            void *grown = realloc(entries, next * sizeof *entries);
            if (!grown) return false;
            entries = static_cast<_CcpCatFileEntry *>(grown);
            capacity = next;
        }
        strcpy(entries[count].name, name); // Validated, uppercased 8.3 only.
        entries[count++].bytes = bytes;
        return true;
    }
};

static void _ccp_catListFiles(uint8 drive, uint8 user,
                              const char *pattern = "*.*",
                              bool longListing = false) {
    char path[20];
    char name[64];
    char description[81];
    char alias[13];
    _ccp_catPath(path, drive, user, NULL);
    if (!_ccp_catFolder(path)) {
        _puts("\r\n No such area");
        return;
    }
    uint16 lines = 0;
    if (!_ccp_catEol(&lines)) return;
    char heading[17];
    const bool currentArea = drive == currentDrive && user == currentUser;
    if (_ccp_catArea(drive, user, alias, description)) {
        _ccp_catDisplayPath(heading, drive, user, alias);
        if (!_ccp_catRow(currentArea ? "" : heading, description, &lines))
            return;
    } else {
        _ccp_catDisplayPath(heading, drive, user, NULL);
        if (!currentArea && !_ccp_catRow(heading, "", &lines)) return;
    }
    if (!_ccp_catEol(&lines)) return;
    File folder = SD.open(path, O_READ);
    if (!folder || !folder.isDirectory()) {
        _puts(" No such area\r\n");
        if (folder) folder.close();
        return;
    }
    _CcpCatFileList listing;
    File file;
    while ((file = folder.openNextFile())) {
        bool regular = !file.isDirectory();
        if (regular) file.getName(name, sizeof name);
        uint32 bytes = regular ? file.size() : 0;
        file.close();
        if (!regular || !_ccp_catFilename(name) ||
            !_ccp_catMatch(name, pattern)) continue;
        if (!listing.add(name, bytes)) {
            folder.close();
            _puts(" Not enough memory to sort listing\r\n");
            return;
        }
    }
    folder.close();
    if (listing.count > 1)
        qsort(listing.entries, listing.count, sizeof *listing.entries,
              _ccp_catFileCompare);
    for (size_t i = 0; i < listing.count; ++i) {
        const _CcpCatFileEntry &entry = listing.entries[i];
        description[0] = 0;
        if (longListing) _ccp_catDescription(drive, user, entry.name, description);
        if (!_ccp_catFileRow(entry.name, description, entry.bytes,
                            longListing, &lines)) return;
    }
    if (!listing.count) _puts(" No file\r\n");
}

// Sum record and DEXT estimates per file: rounding only the total bytes
// would undercount small files. These are not physical directory entries.
struct _CcpCatTotals {
    uint64_t bytes = 0;
    uint64_t records = 0;
    uint64_t dext = 0;
    bool hasContent = false;

    void addFile(uint32 size) {
        const uint32 fileRecords = size / 128 + (size % 128 != 0);
        bytes += size;
        records += fileRecords;
        dext += fileRecords / 256 + (fileRecords % 256 != 0);
    }
    void add(const _CcpCatTotals &other) {
        bytes += other.bytes;
        records += other.records;
        dext += other.dext;
        hasContent = hasContent || other.hasContent;
    }
};

static _CcpCatTotals _ccp_catAreaTotals(uint8 drive, uint8 user) {
    _CcpCatTotals total;
    char path[20];
    _ccp_catPath(path, drive, user, NULL);
    File folder = SD.open(path, O_READ);
    if (!folder) return total;
    if (folder.isDirectory()) {
        File entry;
        while ((entry = folder.openNextFile())) {
            if (!entry.isDirectory()) {
                total.addFile(entry.size());
                char name[64];
                entry.getName(name, sizeof name);
                // Catalog-only/empty areas and host dotfile placeholders are
                // not useful in an area overview. Zero-byte guest files count.
                if (name[0] && name[0] != '.' &&
                    !_ccp_catEqual(name, "DIR.7XF")) total.hasContent = true;
            }
            entry.close();
        }
    }
    folder.close();
    return total;
}

static _CcpCatTotals _ccp_catDriveTotals(uint8 drive) {
    _CcpCatTotals total;
    for (uint8 user = 0; user < 16; ++user)
        total.add(_ccp_catAreaTotals(drive, user));
    return total;
}

static bool _ccp_catCatalogRow(const _CcpCatTotals &totals, const char *path,
                               const char *description, uint16 *lines,
                               bool longListing = false) {
    _CcpCatOutputRow row;
    row.put(' ');
    row.grouped(totals.bytes / 1024 + (totals.bytes % 1024 != 0), 5);
    row.put('k');
    row.put(' ');
    if (longListing) {
        row.number(totals.records, 5);
        row.put(' ');
        row.number(totals.dext, 4);
        row.put(' ');
    }
    row.append(path);
    if (description[0]) {
        for (size_t length = strlen(path); length < 12; ++length) row.put(' ');
        row.append("  ");
        row.append(description);
    }
    return row.emit(lines);
}

static bool _ccp_catDriveDescription(uint8 drive, char *description) {
    description[0] = 0;
    char line[128];
    File file = SD.open("A/0/VOL.7XF", O_READ);
    if (!file) return false;
    bool found = false;
    while (_ccp_catLine(file, line, sizeof line)) {
        if (toupper((unsigned char)line[0]) == 'A' + drive && line[1] == ' ') {
            strncpy(description, line + 2, 80);
            description[80] = 0;
            found = true;
            break;
        }
    }
    file.close();
    return found;
}

static void _ccp_catListAreas(uint8 drive, bool longListing = false) {
    char folder[20];
    char alias[13];
    char description[81];
    char heading[17];
    uint16 lines = 0;
    if (!_ccp_catEol(&lines)) return;
    if (_ccp_catDriveDescription(drive, description) && description[0]) {
        if (!_ccp_catRow("", description, &lines)) return;
        if (!_ccp_catEol(&lines)) return;
    }
    for (uint8 user = 0; user < 16; ++user) {
        _ccp_catPath(folder, drive, user, NULL);
        if (!_ccp_catFolder(folder)) continue;
        const _CcpCatTotals totals = _ccp_catAreaTotals(drive, user);
        if (!totals.hasContent) continue;
        if (_ccp_catArea(drive, user, alias, description)) {
            _ccp_catDisplayPath(heading, drive, user, alias);
        } else {
            _ccp_catDisplayPath(heading, drive, user, NULL);
            description[0] = 0;
        }
        if (!_ccp_catCatalogRow(totals, heading,
                                longListing ? description : "",
                                &lines, longListing)) return;
    }
}

static void _ccp_catListDrives(bool longListing = false) {
    uint16 lines = 0;
    if (!_ccp_catEol(&lines)) return;
    for (uint8 drive = 0; drive < 16; ++drive) {
        const _CcpCatTotals totals = _ccp_catDriveTotals(drive);
        if (!totals.bytes) continue;
        char description[81];
        if (!_ccp_catDriveDescription(drive, description)) strcpy(description, "--");
        char path[] = "/a";
        path[1] += drive;
        if (!_ccp_catCatalogRow(totals, path, description, &lines,
                                longListing)) break;
    }
}

static bool _ccp_catArgument(char *arg, uint8 capacity) {
    uint16 pos = inBuf + 2;
    while (_RamRead(pos) && _RamRead(pos) != ' ') ++pos;
    while (_RamRead(pos) == ' ') ++pos;
    uint8 length = 0;
    while (_RamRead(pos)) {
        if (length + 1 >= capacity) return false;
        arg[length++] = toupper(_RamRead(pos++));
    }
    while (length && arg[length - 1] == ' ') --length;
    arg[length] = 0;
    return true;
}

// Parse the two intentionally small forms: A7: or /A/MBASIC.  The caller
// handles LS / and LS /A separately.  Numeric areas are decimal 0..15.
static int8_t _ccp_catTarget(const char *arg, uint8 *drive, uint8 *user) {
    if (!arg[0]) {
        *drive = currentDrive;
        *user = currentUser;
        return 0;
    }
    if (arg[0] == '/') {
        if (arg[1] < 'A' || arg[1] > 'P' || arg[2] != '/' || !arg[3])
            return -3;
        *drive = arg[1] - 'A';
        const char *alias = arg + 3;
        if (strlen(alias) > 12 || strchr(alias, ' ')) return -3;
        if (alias[0] >= '0' && alias[0] <= '9') {
            uint8 number = 0;
            const char *p = alias;
            while (*p >= '0' && *p <= '9') {
                number = number * 10 + (*p++ - '0');
                if (number > 15) return -3;
            }
            if (!*p) { *user = number; return 0; }
        }
        int8_t resolved = _ccp_catResolveAlias(*drive, alias);
        if (resolved < 0) return resolved;
        *user = resolved;
        return 0;
    }
    if (arg[0] < 'A' || arg[0] > 'P') return -3;
    *drive = arg[0] - 'A';
    uint8 position = 1;
    uint8 number = 0;
    if (arg[position] < '0' || arg[position] > '9') return -3;
    while (arg[position] >= '0' && arg[position] <= '9') {
        number = number * 10 + (arg[position++] - '0');
        if (number > 15) return -3;
    }
    if (arg[position] != ':' || arg[position + 1]) return -3;
    *user = number;
    return 0;
}

static bool _ccp_catFileSpec(char *path, char **pattern,
                             char *patternStorage) {
    *pattern = NULL;
    if (path[0] == '/') {
        if (!path[1] || !path[2] || path[2] != '/') return false;
        char *slash = strchr(path + 3, '/');
        if (!slash) return true;
        *slash++ = 0;
        *pattern = slash;
    } else {
        char *colon = strchr(path, ':');
        if (colon && colon[1]) {
            if (strlen(colon + 1) > 12) return false;
            strcpy(patternStorage, colon + 1);
            *pattern = patternStorage;
            colon[1] = 0;
        } else if (!colon && (strchr(path, '*') || strchr(path, '?') ||
                              strchr(path, '.'))) {
            if (strlen(path) > 12) return false;
            strncpy(patternStorage, path, 12);
            patternStorage[12] = 0;
            *pattern = patternStorage;
            path[0] = 0;
        }
    }
    if (!*pattern) return true;
    if (!**pattern || strchr(*pattern, '/') || strchr(*pattern, ':'))
        return false;
    // Ensure the pattern is representable by two CP/M FCB fields.
    const char *dot = strchr(*pattern, '.');
    if (dot && strchr(dot + 1, '.')) return false;
    const size_t stem = dot ? size_t(dot - *pattern) : strlen(*pattern);
    return stem && stem <= 8 && (!dot || strlen(dot + 1) <= 3);
}

static uint8 _ccp_ls(void) {
    char arg[128];
    if (!_ccp_catArgument(arg, sizeof arg)) return 1;
    uint16 command = inBuf + 2;
    while (_RamRead(command) == ' ') ++command;
    bool longListing = toupper(_RamRead(command)) == 'L' &&
                       toupper(_RamRead(command + 1)) == 'L' &&
                       (_RamRead(command + 2) == ' ' ||
                        !_RamRead(command + 2));
    char emptySubject[] = "";
    char *subject = NULL;
    char *captureName = NULL;
    const char *usage = "\r\n Usage: LS [-L] [-O FILE.7XF] [path]";
    for (char *p = arg; *p;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char *token = p;
        while (*p && *p != ' ') ++p;
        if (*p) *p++ = 0;
        if (_ccp_strEqual(token, "-L")) longListing = true;
        else if (_ccp_strEqual(token, "-O")) {
            while (*p == ' ') ++p;
            if (!*p || captureName) {
                _puts(usage);
                return 0;
            }
            captureName = p;
            while (*p && *p != ' ') ++p;
            if (*p) *p++ = 0;
            const size_t length = strlen(captureName);
            if (!_ccp_catFilename(captureName) || length < 5 ||
                strcmp(captureName + length - 4, ".7XF") ||
                !strcmp(captureName, "DIR.7XF") ||
                !strcmp(captureName, "DRV.7XF") ||
                !strcmp(captureName, "VOL.7XF")) {
                _puts(usage);
                return 0;
            }
        }
        else if (subject) {
            _puts(usage);
            return 0;
        } else subject = token;
    }
    if (!subject) subject = emptySubject;
    char parentSubject[] = {'/', char('A' + currentDrive), 0};
    if (_ccp_strEqual(subject, "..")) subject = parentSubject;
    _CcpCatCapture capture;
    if (_ccp_strEqual(subject, "/")) {
        if (!capture.start(captureName)) {
            _puts("\r\n LS output file unavailable");
            return 0;
        }
        _ccp_catListDrives(longListing);
        return 0;
    }
    if (subject[0] == '/' && subject[1] >= 'A' &&
        subject[1] <= 'P' && !subject[2]) {
        if (!capture.start(captureName)) {
            _puts("\r\n LS output file unavailable");
            return 0;
        }
        _ccp_catListAreas(subject[1] - 'A', longListing);
        return 0;
    }
    char *pattern;
    char patternStorage[13];
    if (!_ccp_catFileSpec(subject, &pattern, patternStorage)) {
        _puts(usage);
        return 0;
    }
    uint8 drive, user;
    int8_t result = _ccp_catTarget(subject, &drive, &user);
    if (result == -2) _puts("\r\n Ambiguous area alias");
    else if (result == -1) _puts("\r\n Unknown area alias");
    else if (result) _puts(usage);
    else {
        if (!capture.start(captureName)) {
            _puts("\r\n LS output file unavailable");
            return 0;
        }
        _ccp_catListFiles(drive, user, pattern ? pattern : "*.*",
                          longListing);
    }
    return 0;
}

// DRV.7XF records one selected area per drive. Reload on each CD so a guest
// editor's changes are visible; the small in-memory copy is used for resolution
// and for writing the next complete generation.
static char _ccp_catDriveArea[16][9];

static void _ccp_catLoadDriveAreas() {
    for (uint8 drive = 0; drive < 16; ++drive)
        strcpy(_ccp_catDriveArea[drive], "0");
    File file = SD.open("A/0/DRV.7XF", O_READ);
    if (!file) return;
    char line[32];
    while (_ccp_catLine(file, line, sizeof line)) {
        if (strlen(line) < 4) continue;
        const char drive = toupper((unsigned char)line[1]);
        if (line[0] != '/' || drive < 'A' || drive > 'P' ||
            line[2] != '/' || !line[3]) continue;
        char name[9];
        uint8 length = 0;
        for (const char *p = line + 3; *p && length < 8; ++p)
            name[length++] = *p;
        name[length] = 0;
        if (_ccp_catAreaName(name) && !line[3 + length])
            strcpy(_ccp_catDriveArea[drive - 'A'], name);
    }
    file.close();
}

static bool _ccp_catSaveDriveAreas() {
    const char *path = "A/0/DRV.7XF";
    const char *next = "A/0/DRV7XF.NEW";
    const char *backup = "A/0/DRV7XF.BAK";
    SD.remove(next);
    File file = SD.open(next, O_CREAT | O_WRITE);
    if (!file) return false;
    bool written = true;
    for (uint8 drive = 0; drive < 16; ++drive) {
        char line[16];
        line[0] = '/';
        line[1] = 'a' + drive;
        line[2] = '/';
        strcpy(line + 3, _ccp_catDriveArea[drive]);
        const uint8 length = 3 + strlen(_ccp_catDriveArea[drive]);
        line[length] = '\r';
        line[length + 1] = '\n';
        if (file.write((const uint8_t *)line, length + 2) !=
            size_t(length + 2))
            written = false;
    }
    const uint8_t eof = 0x1a;
    if (file.write(&eof, 1) != 1) written = false;
    file.flush();
    file.close();
    if (!written) { SD.remove(next); return false; }
    const bool hadPrevious = SD.exists(path);
    if (hadPrevious) {
        SD.remove(backup);
        if (!SD.rename(path, backup)) { SD.remove(next); return false; }
    }
    if (!SD.rename(next, path)) {
        if (hadPrevious) SD.rename(backup, path);
        SD.remove(next);
        return false;
    }
    if (hadPrevious) SD.remove(backup);
    return true;
}

static uint8 _ccp_cd(void) {
    char arg[128];
    if (!_ccp_catArgument(arg, sizeof arg)) return 1;
    _ccp_catLoadDriveAreas();
    if (!arg[0]) {
        _puts("\r\n ");
        _putcon('A' + currentDrive);
        if (currentUser >= 10) _putcon('1');
        _putcon('0' + currentUser % 10);
        _puts(": /");
        _putcon('a' + currentDrive);
        _putcon('/');
        char alias[13], description[81];
        if (_ccp_catArea(currentDrive, currentUser, alias, description)) {
            for (uint8 i = 0; alias[i]; ++i) _putcon(alias[i]);
        } else {
            if (currentUser >= 10) _putcon('1');
            _putcon('0' + currentUser % 10);
        }
        return 0;
    }
    uint8 drive, user;
    bool selectsSaved = false;
    char target[16];
    if (arg[0] == '/' && arg[1] >= 'A' && arg[1] <= 'P' && !arg[2]) {
        drive = arg[1] - 'A';
        snprintf(target, sizeof target, "/%c/%s", arg[1],
                 _ccp_catDriveArea[drive]);
        selectsSaved = true;
    } else if (arg[0] == '/' && arg[1] >= 'A' && arg[1] <= 'P' &&
               arg[2] == '/' && arg[3] && !strchr(arg + 3, ' ')) {
        strncpy(target, arg, sizeof target - 1);
        target[sizeof target - 1] = 0;
    } else if (arg[0] != '/' && !strchr(arg, ' ') && !strchr(arg, ':') &&
               _ccp_catAreaName(arg)) {
        target[0] = '/';
        target[1] = 'A' + currentDrive;
        target[2] = '/';
        strcpy(target + 3, arg);
    } else {
        _puts("\r\n Usage: CD [NAME | /D | /D/NAME]");
        return 0;
    }
    int8_t result = _ccp_catTarget(target, &drive, &user);
    if (result == -2) _puts("\r\n Ambiguous area alias");
    else if (result == -1) _puts("\r\n Unknown area alias");
    else if (result) _puts("\r\n Usage: CD [NAME | /D | /D/NAME]");
    else {
        char folder[20];
        _ccp_catPath(folder, drive, user, NULL);
        if (!_ccp_catFolder(folder)) {
            _puts("\r\n No such area");
        } else if (_ccp_bdos(DRV_SET, drive)) {
            _puts("\r\n Drive not available");
        } else {
            _ccp_bdos(F_USERNUM, user);
            currentDrive = drive;
            currentUser = user;
            if (!selectsSaved) {
                char alias[13], description[81];
                if (_ccp_catArea(drive, user, alias, description))
                    strcpy(_ccp_catDriveArea[drive], alias);
                else strcpy(_ccp_catDriveArea[drive], target + 3);
                if (!_ccp_catSaveDriveAreas())
                    _puts("\r\n DRV.7XF save failed");
            }
        }
    }
    return 0;
}

#undef _putcon
#undef _puts

#endif // CCP_CATALOG_H
