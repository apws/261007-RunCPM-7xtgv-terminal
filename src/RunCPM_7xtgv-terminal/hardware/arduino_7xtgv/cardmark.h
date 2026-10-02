#pragma once

// Native raw-read diagnostic, initially implemented only for the P4
// build. Called synchronously by the sole SD owner through a guest I/O seam.
// No filesystem mutation, remount, INI save or second SD object is used.
#include <cstdio>
#include <cstring>

static bool xtgvCardmarkArguments(const char *text, uint32_t (&args)[3]) {
    unsigned count = 0;
    while (*text) {
        while (*text == ' ' || *text == '\t') ++text;
        if (!*text) break;
        if (count == 3 || *text < '0' || *text > '9') return false;
        uint32_t value = 0;
        while (*text >= '0' && *text <= '9') {
            const unsigned digit = unsigned(*text++ - '0');
            if (value > (UINT32_MAX - digit) / 10) return false;
            value = value * 10 + digit;
        }
        if (*text && *text != ' ' && *text != '\t') return false;
        args[count++] = value;
    }
    return args[0] >= 1 && args[0] <= 1048576 && args[1] >= 1 && args[1] <= 60;
}

#if defined(RUNCPM_TARGET_JC8012P4A1C)
#define XTGV_CARDMARK_AVAILABLE 1

// Narrow read-only access for either storage build. Never expose mutation
// methods through this facade; the caller is the existing sole SD owner.
struct XtgvCardmarkAccess {
    bool readIdentity(cid_t *cid, csd_t *csd) {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
        return SD.readIdentity(cid, csd);
#else
        return SD.card()->readCID(cid) && SD.card()->readCSD(csd);
#endif
    }
    uint32_t cardSectors() {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
        return SD.cardSectors();
#else
        return SD.card()->sectorCount();
#endif
    }
    uint8_t cardError() {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
        return SD.cardError();
#else
        return SD.card()->errorCode();
#endif
    }
    bool readRaw(uint32_t sector, uint8_t *data, size_t count) {
#if defined(XTGV_STORAGE_READ_ONLY) && XTGV_STORAGE_READ_ONLY
        return SD.readRaw(sector, data, count);
#else
        return SD.card()->readSectors(sector, data, count);
#endif
    }
};

static void xtgvCardmarkRun(const char *arguments, void (*print)(const char *),
                           bool (*cancelled)()) {
    const uint8_t originalMHz = uint8_t(xtgvStorageSpi.requestedHz() / 1000000UL);
    XtgvCardmarkAccess card;
    uint32_t args[3] = {1024, originalMHz, 0};
    if (!xtgvCardmarkArguments(arguments, args)) {
        print(" CARDMARK [sectors [MHz [start-sector]]]\r\n"
              " sectors: 1..1048576, requested MHz: 1..60; READ ONLY\r\n");
        return;
    }
    print(" CARDMARK: read-only raw SD test; Ctrl+Alt+C cancels\r\n");
    char line[160];
    bool passed = false, cancelledTest = false;
    uint64_t bytes = 0, trialUs = 0;
    // Static bounded buffers avoid consuming the SD owner's task stack.
    static uint8_t reference[8 * 512], actual[8 * 512];
    do {
        if (!xtgvBoardStorageSetMHz(20)) {
            print(" Cannot select baseline clock.\r\n"); break;
        }
        cid_t cid = {};
        csd_t csd = {};
        if (!card.readIdentity(&cid, &csd)) {
            print(" Cannot read CID/CSD.\r\n"); break;
        }
        // Print identifiers as bounded printable fields, never raw control bytes.
        char oem[3] = {}, product[6] = {};
        for (unsigned i = 0; i < 2; ++i)
            oem[i] = cid.oid[i] >= 32 && cid.oid[i] <= 126 ? cid.oid[i] : '.';
        for (unsigned i = 0; i < 5; ++i)
            product[i] = cid.pnm[i] >= 32 && cid.pnm[i] <= 126 ? cid.pnm[i] : '.';
        snprintf(line, sizeof(line),
                 " MID=%02X OEM=%s Product=%s Revision=%u.%u Serial=%08lX Date=%u-%02u\r\n",
                 unsigned(cid.mid), oem, product, unsigned(cid.prvN()), unsigned(cid.prvM()),
                 (unsigned long)cid.psn(), unsigned(cid.mdtYear()), unsigned(cid.mdtMonth()));
        print(line);
        for (unsigned reg = 0; reg < 2; ++reg) {
            print(reg ? " CSD:" : " CID:");
            const uint8_t *raw = reg ? reinterpret_cast<const uint8_t *>(&csd)
                                     : reinterpret_cast<const uint8_t *>(&cid);
            for (unsigned i = 0; i < 16; ++i) {
                snprintf(line, sizeof(line), " %02X", unsigned(raw[i])); print(line);
            }
            print("\r\n");
        }
        const uint32_t capacity = card.cardSectors();
        // TRAN_SPEED is the encoded advertised CSD rate, not measured SCK.
        const uint8_t *rawCsd = reinterpret_cast<const uint8_t *>(&csd);
        snprintf(line, sizeof(line),
                 " Capacity=%lu sectors (512 B), CSD TRAN_SPEED=0x%02X\r\n"
                 " Baseline=20, trial=%lu, restore=%u MHz REQUESTED (not measured)\r\n",
                 (unsigned long)capacity, unsigned(rawCsd[3]),
                 (unsigned long)args[1], unsigned(originalMHz)); print(line);
        if (!capacity || args[2] >= capacity || args[0] > capacity - args[2]) {
            print(" Range exceeds card capacity.\r\n"); break;
        }
        bool failed = false;
        for (uint32_t offset = 0; offset < args[0] && !failed; ) {
            if (cancelled()) { cancelledTest = true; break; }
            const uint32_t count = args[0] - offset < 8 ? args[0] - offset : 8;
            const uint32_t sector = args[2] + offset;
            if (!xtgvBoardStorageSetMHz(20) || !card.readRaw(sector, reference, count) ||
                !xtgvBoardStorageSetMHz(uint8_t(args[1]))) {
                snprintf(line, sizeof(line), " Baseline/clock error at sector %lu, SD error %u\r\n",
                         (unsigned long)sector, unsigned(card.cardError())); print(line);
                failed = true; break;
            }
            for (unsigned pass = 1; pass <= 3; ++pass) {
                if (cancelled()) { cancelledTest = true; break; }
                memset(actual, 0xa5, sizeof(actual));
                const uint32_t started = micros();
                const bool read = card.readRaw(sector, actual, count);
                trialUs += uint32_t(micros() - started);
                if (!read) {
                    snprintf(line, sizeof(line), " Read error sector %lu pass %u, SD error %u\r\n",
                             (unsigned long)sector, pass, unsigned(card.cardError()));
                    print(line); failed = true; break;
                }
                for (uint32_t i = 0; i < count * 512; ++i) {
                    if (actual[i] == reference[i]) continue;
                    snprintf(line, sizeof(line),
                             " MISMATCH sector %lu byte %u pass %u: baseline %02X trial %02X\r\n",
                             (unsigned long)(sector + i / 512), unsigned(i % 512), pass,
                             unsigned(reference[i]), unsigned(actual[i]));
                    print(line); failed = true; break;
                }
                if (failed) break;
                bytes += count * 512;
                delay(0);
            }
            if (cancelledTest) break;
            offset += count;
        }
        passed = !failed && !cancelledTest;
    } while (false);
    if (!xtgvBoardStorageSetMHz(originalMHz)) {
        print(" CLOCK RESTORE FAILED: storage stopped; reset/power-cycle required.\r\n");
        // Do not return to CCP with card/transaction state uncertain.
        while (true) delay(100);
    }
    const uint64_t rate100 = trialUs ? bytes * 100000000ULL / trialUs / 1024 : 0;
    snprintf(line, sizeof(line), " %s: %llu bytes compared; raw reads %llu.%02llu KiB/s\r\n",
             cancelledTest ? "CANCELLED" : passed ? "PASS" : "FAIL",
             (unsigned long long)bytes, (unsigned long long)(rate100 / 100),
             (unsigned long long)(rate100 % 100)); print(line);
    print(" Operating clock restored. Read agreement is not write qualification.\r\n");
}
#else
#define XTGV_CARDMARK_AVAILABLE 0
#endif
