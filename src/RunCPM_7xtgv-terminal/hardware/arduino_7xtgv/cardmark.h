#pragma once

// Native raw-read diagnostic for our direct-SdFat/SPI board builds.
// Called synchronously by the sole SD owner through a guest I/O seam.
// No filesystem mutation, remount, INI save or second SD object is used.
#include <cstdio>
#include <cstring>

static bool xtgvCardmarkArguments(const char* text, uint32_t (&args)[3], bool& verboseCsd)
{
    unsigned count = 0;
    while (*text)
    {
        while (*text == ' ' || *text == '\t')
            ++text;
        if (!*text)
            break;
        if ((text[0] == 'c' || text[0] == 'C') && (text[1] == 's' || text[1] == 'S') &&
            (text[2] == 'd' || text[2] == 'D') && (!text[3] || text[3] == ' ' || text[3] == '\t'))
        {
            if (verboseCsd)
                return false;
            verboseCsd = true;
            text += 3;
            continue;
        }
        if (count == 3 || *text < '0' || *text > '9')
            return false;
        uint32_t value = 0;
        while (*text >= '0' && *text <= '9')
        {
            const unsigned digit = unsigned(*text++ - '0');
            if (value > (UINT32_MAX - digit) / 10)
                return false;
            value = value * 10 + digit;
        }
        if (*text && *text != ' ' && *text != '\t')
            return false;
        args[count++] = value;
    }
    return args[0] >= 1 && args[0] <= 1048576 && args[1] >= 1 && args[1] <= 60;
}

// All current SPI board headers use our external SdFat clock-control driver.
#ifndef XTGV_STORAGE_HAS_RAW_CARD
#define XTGV_STORAGE_HAS_RAW_CARD 1
#endif
#if XTGV_STORAGE_HAS_RAW_CARD
#define XTGV_CARDMARK_AVAILABLE 1

// Wire registers are big-endian bit strings, independent of CPU struct layout.
static uint32_t xtgvCardmarkBits(const uint8_t* raw, unsigned high, unsigned low)
{
    uint32_t value = 0;
    for (unsigned bit = high + 1; bit-- > low;)
        value = (value << 1) | ((raw[(127 - bit) / 8] >> (bit % 8)) & 1);
    return value;
}

static void xtgvCardmarkRaw(const char* name, const uint8_t* raw, void (*print)(const char*))
{
    print(name);
    char byte[4];
    for (unsigned i = 0; i < 16; ++i)
    {
        snprintf(byte, sizeof(byte), " %02X", unsigned(raw[i]));
        print(byte);
    }
    print("\r\n");
}

// Output buffer needs 27 bytes for any uint64_t, including comma separators.
static void xtgvCardmarkNumber(uint64_t value, char* out)
{
    char reversed[27];
    unsigned n = 0, digits = 0;
    do
    {
        if (digits && digits % 3 == 0)
            reversed[n++] = ',';
        reversed[n++] = char('0' + value % 10);
        value /= 10;
        ++digits;
    } while (value);
    for (unsigned i = 0; i < n; ++i)
        out[i] = reversed[n - i - 1];
    out[n] = 0;
}

static uint32_t xtgvCardmarkSpeedHz(uint8_t code)
{
    static constexpr uint8_t tenths[] = {0, 10, 12, 13, 15, 20, 25, 30,
                                         35, 40, 45, 50, 55, 60, 70, 80};
    static constexpr uint32_t units[] = {100000, 1000000, 10000000, 100000000};
    if ((code & 0x80) || (code & 7) > 3)
        return 0;
    return units[code & 7] / 10 * tenths[(code >> 3) & 15];
}

static void xtgvCardmarkCsd(const uint8_t* raw, void (*print)(const char*))
{
    const unsigned version = raw[0] >> 6;
    char line[160];
    snprintf(line, sizeof(line), " CSD_STRUCTURE=%u: %s\r\n", version,
             version == 0   ? "v1.0 SDSC"
             : version == 1 ? "v2.0 SDHC/SDXC"
             : version == 2 ? "v3.0 SDUC (unsupported by this SdFat backend)"
                            : "reserved/unknown (not decoded)");
    print(line);
    if (version > 1)
        return;

    struct Field
    {
        const char* name;
        uint8_t high, low;
        const char* meaning;
    };

    static constexpr Field common[] = {
        {"TAAC",119,112,"read access time; fixed in v2"},
        {"NSAC",111,104,"access clocks x100; fixed in v2"},
        {"TRAN_SPEED",103,96,"encoded rate; see Speed summary"},
        {"CCC",95,84,"supported command-class bitmap"},
        {"READ_BL_LEN",83,80,"read block bytes = 2^value"},
        {"READ_BL_PARTIAL",79,79,"partial-block reads allowed"},
        {"WRITE_BLK_MISALIGN",78,78,"misaligned writes allowed"},
        {"READ_BLK_MISALIGN",77,77,"misaligned reads allowed"},
        {"DSR_IMP",76,76,"driver-stage register implemented"},
        {"ERASE_BLK_EN",46,46,"single-block erase allowed"},
        {"SECTOR_SIZE",45,39,"erase size code; fixed/unused in v2"},
        {"WP_GRP_SIZE",38,32,"protection group code; unused in v2"},
        {"WP_GRP_ENABLE",31,31,"group write protection enabled"},
        {"R2W_FACTOR",28,26,"write/read factor 2^value; fixed in v2"},
        {"WRITE_BL_LEN",25,22,"write block bytes = 2^value"},
        {"WRITE_BL_PARTIAL",21,21,"partial-block writes allowed"},
        {"FILE_FORMAT_GRP",15,15,"format group; unused in v2"},
        {"COPY",14,14,"copy flag"},
        {"PERM_WRITE_PROTECT",13,13,"permanent write protection"},
        {"TMP_WRITE_PROTECT",12,12,"temporary write protection"},
        {"FILE_FORMAT",11,10,"format code; unused in v2"},
        {"CRC7",7,1,"raw checksum field; not verified"},
        {"END_BIT",0,0,"must be 1"},
    };
    for (const auto& field : common)
    {
        const uint32_t value = xtgvCardmarkBits(raw, field.high, field.low);
        snprintf(line, sizeof(line), " %-20s = %lu (0x%lX) %s\r\n", field.name, (unsigned long)value,
                 (unsigned long)value, field.meaning);
        print(line);
    }
    const uint32_t size = xtgvCardmarkBits(raw, version ? 69 : 73, version ? 48 : 62);
    snprintf(line, sizeof(line), " %-20s = %lu (0x%lX) %s\r\n", "C_SIZE", (unsigned long)size, (unsigned long)size,
             version ? "(value+1) x 512 KiB" : "uses C_SIZE_MULT and READ_BL_LEN");
    print(line);
    if (!version)
    {
        static constexpr Field legacy[] = {
            {"VDD_R_CURR_MIN",61,59,"encoded minimum read current"},
            {"VDD_R_CURR_MAX",58,56,"encoded maximum read current"},
            {"VDD_W_CURR_MIN",55,53,"encoded minimum write current"},
            {"VDD_W_CURR_MAX",52,50,"encoded maximum write current"},
            {"C_SIZE_MULT",49,47,"capacity multiplier = 2^(value+2)"},
        };
        for (const auto& field : legacy)
        {
            snprintf(line, sizeof(line), " %-20s = %lu %s\r\n", field.name,
                     (unsigned long)xtgvCardmarkBits(raw, field.high, field.low), field.meaning);
            print(line);
        }
    }
    else
    {
        print(" v2 timing/erase/format compatibility fields are not NAND geometry.\r\n");
    }
}

// Narrow read-only access for either storage build. Never expose mutation
// methods through this facade; the caller is the existing sole SD owner.
struct XtgvCardmarkAccess
{
    bool checkHighSpeed(uint8_t* status)
    {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
        return SD.checkHighSpeed(status);
#else
        // Mode 0, all groups 0xF: query only; no function selection changes.
        return SD.card()->cardCMD6(0x00FFFFFFUL, status);
#endif
    }

    bool readIdentity(cid_t* cid, csd_t* csd)
    {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
        return SD.readIdentity(cid, csd);
#else
        return SD.card()->readCID(cid) && SD.card()->readCSD(csd);
#endif
    }

    uint32_t cardSectors()
    {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
        return SD.cardSectors();
#else
        return SD.card()->sectorCount();
#endif
    }

    uint8_t cardError()
    {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
        return SD.cardError();
#else
        return SD.card()->errorCode();
#endif
    }

    bool readRaw(uint32_t sector, uint8_t* data, size_t count)
    {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
        return SD.readRaw(sector, data, count);
#else
        return SD.card()->readSectors(sector, data, count);
#endif
    }
};

static void xtgvCardmarkRun(const char* arguments, void (*print)(const char*), bool (*cancelled)())
{
    const uint8_t originalMHz = uint8_t(xtgvStorageSpi.requestedHz() / 1000000UL);
    XtgvCardmarkAccess card;
    uint32_t args[3] = {1024, originalMHz, 0};
    bool verboseCsd = false;
    if (!xtgvCardmarkArguments(arguments, args, verboseCsd))
    {
        print(" CARDMARK [CSD] [sectors [MHz [start-sector]]]\r\n"
              " sectors: 1..1048576, requested MHz: 1..60; READ ONLY\r\n");
        return;
    }
    print(" CARDMARK: read-only RAW Arduino SdFat/SPI SD test; Ctrl+Alt+C cancels\r\n"
          " -------------------------------------------------------------------------\r\n");
    char line[160];
    bool passed = false, cancelledTest = false, progressShown = false;
    uint64_t bytes = 0, trialUs = 0;
    // Static bounded buffers avoid consuming the SD owner's task stack.
    static uint8_t reference[8 * 512], actual[8 * 512];
    do
    {
        if (!xtgvBoardStorageSetMHz(20))
        {
            print(" Cannot select baseline clock.\r\n");
            break;
        }
        cid_t cid = {};
        csd_t csd = {};
        if (!card.readIdentity(&cid, &csd))
        {
            print(" Cannot read CID/CSD.\r\n");
            break;
        }
        // Print identifiers as bounded printable fields, never raw control bytes.
        xtgvCardmarkRaw(" CID:", reinterpret_cast<const uint8_t*>(&cid), print);
        char oem[3] = {}, product[6] = {};
        for (unsigned i = 0; i < 2; ++i)
            oem[i] = cid.oid[i] >= 32 && cid.oid[i] <= 126 ? cid.oid[i] : '.';
        for (unsigned i = 0; i < 5; ++i)
            product[i] = cid.pnm[i] >= 32 && cid.pnm[i] <= 126 ? cid.pnm[i] : '.';
        snprintf(line, sizeof(line), " MID=%02X OEM=%s Product=%s Revision=%u.%u Serial=%08lX Date=%u-%02u\r\n",
                 unsigned(cid.mid), oem, product, unsigned(cid.prvN()), unsigned(cid.prvM()), (unsigned long)cid.psn(),
                 unsigned(cid.mdtYear()), unsigned(cid.mdtMonth()));
        print(line);
        const uint32_t capacity = card.cardSectors();
        // TRAN_SPEED is the encoded advertised CSD rate, not measured SCK.
        const uint8_t* rawCsd = reinterpret_cast<const uint8_t*>(&csd);
        xtgvCardmarkRaw(" CSD:", rawCsd, print);
        char mib[27];
        xtgvCardmarkNumber(capacity / 2048, mib);
        snprintf(line, sizeof(line), " Capacity=%s MiB (%lu LBA 512B sectors)\r\n", mib, (unsigned long)capacity);
        print(line);
        const uint32_t speedHz = xtgvCardmarkSpeedHz(rawCsd[3]);
        if (speedHz)
        {
            snprintf(line, sizeof(line), " Speed: CSD TRAN_SPEED=0x%02X -> %lu.%02lu MHz (encoded)\r\n",
                     unsigned(rawCsd[3]), (unsigned long)(speedHz / 1000000),
                     (unsigned long)((speedHz % 1000000) / 10000));
        }
        else
        {
            snprintf(line, sizeof(line), " Speed: CSD TRAN_SPEED=0x%02X (reserved/unknown)\r\n", unsigned(rawCsd[3]));
        }
        print(line);
        if (xtgvCardmarkBits(rawCsd, 95, 84) & (1U << 10))
        {
            uint8_t status[64] = {};
            if (card.checkHighSpeed(status))
            {
                print(status[13] & 2 ? " CMD6: High Speed 50 MHz supported (check only; not enabled)\r\n"
                                     : " CMD6: High Speed not advertised (check only)\r\n");
            }
            else
            {
                print(" CMD6: check failed; High Speed support unknown\r\n");
            }
        }
        else
        {
            print(" CMD6: not advertised by CCC; check skipped\r\n");
        }
        if (verboseCsd)
            xtgvCardmarkCsd(rawCsd, print);
        print(" -------------------------------------------------------------------------\r\n");
        // Arduino request, not an independently measured physical SCK.
        snprintf(line, sizeof(line), " Arduino SPI CLK [MHz, approx]: %lu (restore %u, baseline 20)\r\n",
                 (unsigned long)args[1], unsigned(originalMHz));
        print(line);
        if (!capacity || args[2] >= capacity || args[0] > capacity - args[2])
        {
            print(" Range exceeds card capacity.\r\n");
            break;
        }
        bool failed = false;
        print(" ... "); // Emit before any raw test reads, not with the result.
        progressShown = true;
        for (uint32_t offset = 0; offset < args[0] && !failed;)
        {
            if (cancelled())
            {
                cancelledTest = true;
                break;
            }
            const uint32_t count = args[0] - offset < 8 ? args[0] - offset : 8;
            const uint32_t sector = args[2] + offset;
            if (!xtgvBoardStorageSetMHz(20) || !card.readRaw(sector, reference, count) ||
                !xtgvBoardStorageSetMHz(uint8_t(args[1])))
            {
                snprintf(line, sizeof(line), "Baseline/clock error at sector %lu, SD error %u\r\n",
                         (unsigned long)sector, unsigned(card.cardError()));
                print(line);
                failed = true;
                break;
            }
            for (unsigned pass = 1; pass <= 3; ++pass)
            {
                if (cancelled())
                {
                    cancelledTest = true;
                    break;
                }
                memset(actual, 0xa5, sizeof(actual));
                const uint32_t started = micros();
                const bool read = card.readRaw(sector, actual, count);
                trialUs += uint32_t(micros() - started);
                if (!read)
                {
                    snprintf(line, sizeof(line), "Read error sector %lu pass %u, SD error %u\r\n",
                             (unsigned long)sector, pass, unsigned(card.cardError()));
                    print(line);
                    failed = true;
                    break;
                }
                for (uint32_t i = 0; i < count * 512; ++i)
                {
                    if (actual[i] == reference[i])
                        continue;
                    snprintf(line, sizeof(line), "MISMATCH sector %lu byte %u pass %u: baseline %02X trial %02X\r\n",
                             (unsigned long)(sector + i / 512), unsigned(i % 512), pass, unsigned(reference[i]),
                             unsigned(actual[i]));
                    print(line);
                    failed = true;
                    break;
                }
                if (failed)
                    break;
                bytes += count * 512;
                delay(0);
            }
            if (cancelledTest)
                break;
            offset += count;
        }
        passed = !failed && !cancelledTest;
    } while (false);
    if (!xtgvBoardStorageSetMHz(originalMHz))
    {
        print(" CLOCK RESTORE FAILED: storage stopped; reset/power-cycle required.\r\n");
        // Do not return to CCP with card/transaction state uncertain.
        while (true)
            delay(100);
    }
    const uint64_t rate100 = trialUs ? bytes * 100000000ULL / trialUs / 1024 : 0;
    snprintf(line, sizeof(line), "%s%s: %llu bytes compared; raw reads %llu.%02llu KiB/s\r\n",
             progressShown && (passed || cancelledTest) ? "" : " ",
             cancelledTest ? "CANCELLED"
             : passed      ? "PASS"
                           : "FAIL",
             (unsigned long long)bytes, (unsigned long long)(rate100 / 100), (unsigned long long)(rate100 % 100));
    print(line);
}
#else
#define XTGV_CARDMARK_AVAILABLE 0
#endif
