#pragma once

// Product terminal persistence. Include after SD, File, and RunCPM globals are
// declared in the integrated sketch; the RunCPM/storage owner calls these.

// Human-editable terminal history, separate from CP/M files but kept beside
// 7XTGVTRM.INI. A validated .NEW file is rotated into place while .BAK retains
// the prior generation, so an interrupted write never destroys the last copy.
static constexpr const char* TerminalHistoryPath = "A/0/7XTGVTRM.BUF";
static constexpr const char* TerminalHistoryNewPath = "A/0/7XTGVTRM.NEW";
static constexpr const char* TerminalHistoryBackupPath = "A/0/7XTGVTRM.BAK";
// Do not use 7XTGVTRM.NEW/.BAK here: FAT's 8.3 names would collide with the
// history transaction above. These belong solely to the INI transaction.
static constexpr const char* TerminalConfigPath = "A/0/7XTGVTRM.INI";
static constexpr const char* TerminalConfigNewPath = "A/0/7XTCFG.NEW";
static constexpr const char* TerminalConfigBackupPath = "A/0/7XTCFG.BAK";
static constexpr const char* TerminalConfigErrorPath = "A/0/7XTGVTRM.ERR";
static constexpr const char* TerminalHistoryV1Header = "# 7xtgv terminal history v1\n";
static constexpr const char* TerminalHistoryV2Header = "# 7xtgv terminal history v2\n";
static constexpr size_t TerminalHistoryV2FooterLength = 17; // # crc32=XXXXXXXX\n

static uint32_t terminalHistoryCRC32Byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320UL & uint32_t(-int32_t(crc & 1)));
    return crc;
}

static int terminalHistoryHex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static bool terminalHistoryFileValid(File& file)
{
    const size_t headerLength = strlen(TerminalHistoryV2Header);
    const size_t size = file.size();
    if (size < headerLength)
        return false;
    char header[32] = {};
    if (!file.seek(0) || file.read(reinterpret_cast<uint8_t*>(header), headerLength) != headerLength)
        return false;
    // Existing v1 history remains readable. Every newly written v2 file must
    // have its completion marker, so a torn write is rejected.
    if (!memcmp(header, TerminalHistoryV1Header, headerLength))
        return file.seek(0);
    if (memcmp(header, TerminalHistoryV2Header, headerLength) || size < headerLength + TerminalHistoryV2FooterLength)
        return false;
    char footer[TerminalHistoryV2FooterLength + 1] = {};
    if (!file.seek(size - TerminalHistoryV2FooterLength) ||
        file.read(reinterpret_cast<uint8_t*>(footer), TerminalHistoryV2FooterLength) != TerminalHistoryV2FooterLength ||
        memcmp(footer, "# crc32=", 8) || footer[16] != '\n')
        return false;
    uint32_t expected = 0;
    for (uint8_t i = 8; i < 16; ++i)
    {
        const int digit = terminalHistoryHex(footer[i]);
        if (digit < 0)
            return false;
        expected = (expected << 4) | uint32_t(digit);
    }
    if (!file.seek(0))
        return false;
    uint32_t crc = 0xffffffffUL;
    for (size_t remaining = size - TerminalHistoryV2FooterLength; remaining; --remaining)
    {
        const int byte = file.read();
        if (byte < 0)
            return false;
        crc = terminalHistoryCRC32Byte(crc, uint8_t(byte));
    }
    if ((crc ^ 0xffffffffUL) != expected)
        return false;
    return file.seek(0);
}

static File openTerminalHistory()
{
    const char* paths[] = {TerminalHistoryPath, TerminalHistoryNewPath,
                           TerminalHistoryBackupPath};
    for (const char* path : paths)
    {
        if (!SD.exists(path))
            continue;
        File file = SD.open(path, FILE_READ);
        if (file && terminalHistoryFileValid(file))
            return file;
        if (file)
            file.close();
    }
    return File();
}

static bool writeTerminalHistoryByte(void* context, uint8_t byte)
{
    return static_cast<File*>(context)->write(byte) == 1;
}

bool terminalHistoryStoragePoll()
{
    if (!terminalHistorySaveDue())
        return true;
    // Stop history mutation while this owner core serialises it. Filesystem
    // operations themselves never leave the RunCPM core.
    terminalPause(true);
    terminalDiskActivity(true);
    if (SD.exists(TerminalHistoryNewPath))
        SD.remove(TerminalHistoryNewPath);
    File file = SD.open(TerminalHistoryNewPath, O_CREAT | O_WRITE);
    bool saved = bool(file) && terminalHistoryWrite(writeTerminalHistoryByte, &file);
    if (file)
    {
        file.flush();
        file.close();
    }
    if (saved)
    {
        File verification = SD.open(TerminalHistoryNewPath, FILE_READ);
        saved = bool(verification) && terminalHistoryFileValid(verification);
        if (verification)
            verification.close();
    }
    if (saved)
    {
        if (SD.exists(TerminalHistoryBackupPath))
            SD.remove(TerminalHistoryBackupPath);
        const bool hadPrevious = SD.exists(TerminalHistoryPath);
        const bool backedUp = !hadPrevious || SD.rename(TerminalHistoryPath, TerminalHistoryBackupPath);
        saved = backedUp && SD.rename(TerminalHistoryNewPath, TerminalHistoryPath);
        if (saved && SD.exists(TerminalHistoryBackupPath))
            SD.remove(TerminalHistoryBackupPath);
        else if (hadPrevious && backedUp && !SD.exists(TerminalHistoryPath))
            SD.rename(TerminalHistoryBackupPath, TerminalHistoryPath);
    }
    terminalPause(false);
    // Refresh the visible pulse after the paused display task resumes. A slow
    // verified write can otherwise consume the entire 75 ms latch unseen.
    terminalDiskActivity(true);
    terminalHistorySaveComplete(saved);
    return saved;
}

// Accessed only by the RunCPM/storage owner. Runtime keeps the small parsed
// settings snapshot; this buffer preserves the complete human-authored file.
static vt::IniText terminalConfigText;
static bool terminalConfigGuestWriting = false;
static const char* terminalConfigCacheFailure = "file unavailable";
static size_t terminalConfigCacheRequestedBytes = 0;

static bool readTerminalConfigText(File& source)
{
    const size_t size = source ? source.size() : 0;
    terminalConfigCacheRequestedBytes = size;
    uint8_t* data = terminalConfigText.beginLoad(size);
    if (!data)
    {
        terminalConfigCacheFailure = size > vt::IniText::MaxBytes ? "file exceeds 64 KiB limit" : "allocation failed";
        return false;
    }
    for (size_t offset = 0; offset < size;)
    {
        const size_t count = size - offset < 512 ? size - offset : 512;
        if (source.read(data + offset, count) != int(count))
        {
            terminalConfigCacheFailure = "file read incomplete";
            return false;
        }
        offset += count;
    }
    terminalConfigText.finishLoad();
    terminalConfigCacheFailure = nullptr;
    return true;
}

static bool loadTerminalConfigText()
{
    terminalConfigCacheFailure = "file unavailable";
    File source = SD.open(TerminalConfigPath, FILE_READ);
    const bool loaded = (bool(source) || !SD.exists(TerminalConfigPath)) && readTerminalConfigText(source);
    if (source)
        source.close();
    if (!loaded)
        terminalConfigText.invalidate();
    return loaded;
}

static bool updateConfigSettings(vt::IniText& text, uint8_t level, uint8_t zoom, const char* displayId, uint8_t palette,
                                 uint8_t dirty)
{
    char display[24], value[4];
    if (dirty & ConfigZoomDirty)
    {
        snprintf(display, sizeof(display), "display:%s", displayId);
        snprintf(value, sizeof(value), "%u", unsigned(zoom));
        if (!text.set(display, "view_zoom", value))
            return false;
    }
    if (dirty & ConfigBacklightDirty)
    {
        snprintf(value, sizeof(value), "%u", unsigned(level));
        if (!text.set("global", "backlight_level", value))
            return false;
    }
    if (dirty & ConfigPaletteDirty)
    {
        snprintf(value, sizeof(value), "%u", unsigned(palette));
        if (!text.set("global", "mode_palette", value))
            return false;
    }
    return true;
}

static bool writeTerminalConfigText(File& target)
{
    for (size_t offset = 0; offset < terminalConfigText.size();)
    {
        const size_t remaining = terminalConfigText.size() - offset;
        const size_t count = remaining < 512 ? remaining : 512;
        if (target.write(terminalConfigText.data() + offset, count) != count)
            return false;
        offset += count;
    }
    return target.write(uint8_t(0x1a)) == 1;
}

static bool verifyTerminalConfigText(File& source)
{
    if (!source || source.size() != terminalConfigText.size() + 1)
        return false;
    uint8_t block[512];
    for (size_t offset = 0; offset < terminalConfigText.size();)
    {
        const size_t remaining = terminalConfigText.size() - offset;
        const size_t count = remaining < sizeof(block) ? remaining : sizeof(block);
        if (source.read(block, count) != int(count) || memcmp(block, terminalConfigText.data() + offset, count))
            return false;
        offset += count;
    }
    return source.read() == 0x1a;
}

// Called before guest access, including VTPUT/VTGET's ordinary BDOS calls.
// Flush before the first read/mutation; never save over an open guest writer.
static bool terminalConfigFileAccess(const char* path, bool writing)
{
    if (!vt::IniText::samePath(path, TerminalConfigPath))
        return true;
    if (!terminalConfigGuestWriting)
    {
        terminalConfigForceSave();
        if (!terminalConfigStoragePoll())
            return false;
    }
    if (writing)
    {
        terminalConfigText.invalidate();
        terminalConfigGuestWriting = true;
    }
    return true;
}

static void terminalConfigFileClosed(const char* path)
{
    if (!vt::IniText::samePath(path, TerminalConfigPath) || !terminalConfigGuestWriting)
        return;
    terminalConfigGuestWriting = false;
    loadTerminalConfigText(); // text only; live profiles still reload on reboot
}

static void terminalConfigGuestIdle()
{
    // Also covers ERA, rename-based editors and programs which warm-boot
    // without closing their FCB. Never reload a partial transfer at a key wait.
    terminalConfigGuestWriting = false;
    if (!terminalConfigText.valid())
        loadTerminalConfigText();
}

static bool writeTerminalConfigErrorReport()
{
    // A stale report is worse than none: remove it after a clean parse too.
    if (SD.exists(TerminalConfigErrorPath))
        SD.remove(TerminalConfigErrorPath);
    const unsigned total = terminalConfigErrorCount();
    const uint8_t count = terminalConfigErrorLineCount();
    if (!total)
        return true;
    File report = SD.open(TerminalConfigErrorPath, O_CREAT | O_WRITE);
    if (!report)
        return false;
    char line[256];
    int length = total == count ? snprintf(line, sizeof(line), "7XTGVTRM.INI parse errors (%u)\r\n", total)
                                : snprintf(line, sizeof(line), "7XTGVTRM.INI parse errors (%u; first %u shown)\r\n",
                                           total, unsigned(count));
    bool written = length > 0 && size_t(length) < sizeof(line) &&
                   report.write(reinterpret_cast<const uint8_t*>(line), size_t(length)) == size_t(length);
    for (uint8_t index = 0; written && index < count; ++index)
    {
        uint16_t number = 0;
        const char *section = nullptr, *text = nullptr;
        if (!terminalConfigErrorLine(index, number, section, text))
        {
            written = false;
            break;
        }
        length = snprintf(line, sizeof(line), "%4u: %s: %s\r\n", unsigned(number), section, text);
        written = length > 0 && size_t(length) < sizeof(line) &&
                  report.write(reinterpret_cast<const uint8_t*>(line), size_t(length)) == size_t(length);
    }
    written = written && report.write(uint8_t(0x1a)) == 1;
    report.flush();
    report.close();
    return written;
}

bool terminalConfigStoragePoll()
{
    uint8_t level = 0, zoom = 0, palette = 0, dirty = 0;
    if (!terminalConfigSaveDue(level, zoom, &palette, &dirty))
        return true;
    if (terminalConfigGuestWriting)
        return false; // defer even explicit SYNC until guest close/CCP
    terminalPause(true);
    // A last key could have arrived between the due check and pause. Recheck
    // the settled snapshot now that its producing core cannot mutate it.
    if (!terminalConfigSaveDue(level, zoom, &palette, &dirty))
    {
        terminalPause(false);
        return true;
    }
    terminalDiskActivity(true);
    bool saved = (terminalConfigText.valid() || loadTerminalConfigText()) &&
                 updateConfigSettings(terminalConfigText, level, zoom, vt::displayHalDefaults().id, palette, dirty);
    // Do not open/truncate any destination until the complete RAM edit exists.
    if (saved && SD.exists(TerminalConfigNewPath))
        saved = SD.remove(TerminalConfigNewPath);
    File target;
    if (saved)
        target = SD.open(TerminalConfigNewPath, O_CREAT | O_WRITE);
    saved = saved && bool(target) && writeTerminalConfigText(target);
    if (target)
    {
        target.flush();
        target.close();
    }
    if (saved)
    {
        File verification = SD.open(TerminalConfigNewPath, FILE_READ);
        saved = verifyTerminalConfigText(verification);
        if (verification)
            verification.close();
    }
    if (saved)
    {
        if (SD.exists(TerminalConfigBackupPath))
            SD.remove(TerminalConfigBackupPath);
        const bool hadPrevious = SD.exists(TerminalConfigPath);
        const bool backedUp = !hadPrevious || SD.rename(TerminalConfigPath, TerminalConfigBackupPath);
        saved = backedUp && SD.rename(TerminalConfigNewPath, TerminalConfigPath);
        if (saved && SD.exists(TerminalConfigBackupPath))
            SD.remove(TerminalConfigBackupPath);
        else if (hadPrevious && backedUp && !SD.exists(TerminalConfigPath))
            SD.rename(TerminalConfigBackupPath, TerminalConfigPath);
    }
    terminalConfigSaveComplete(saved);
    terminalPause(false);
    terminalDiskActivity(true);
    return saved;
}
