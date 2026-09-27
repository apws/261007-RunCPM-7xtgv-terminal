#include "../../runcpm-config.h"
#if !defined(RUNCPM_HEADLESS)
#include "terminal_runtime.h"
#include "7xtgv-QL-font.h"
#include "7xtgv-gfxsetup.h"
#include "byte_queue.h"
#include "history_profiles.h"
#include "line_history.h"
#include "terminal.h"
#include "../../hardware/arduino_7xtgv/backlight.h"
#include "../../hardware/arduino_7xtgv/target_identity.h"
#include "../../hardware/arduino_7xtgv/reboot.h"
#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include <new>
#include <stdio.h>
#include <string.h>

static ByteQueue<4096> input;
static ByteQueue<16384> output;
// Replies cannot block rendering if a program emits queries but never reads input.
static ByteQueue<1024> replies;
static vt::LineHistory *history = nullptr;
static vt::HistoryProfiles profiles;
// The parsed INI is deliberately immutable while the terminal runs.  Keep the
// user-selected view separately so repeated zoom requests advance from the
// live zoom level instead of repeatedly starting from the INI default.
static vt::DisplayProfile activeDisplayProfile;
static vt::DisplayProfile pendingDisplayProfile;
static vt::DisplayProfile bootDisplayProfile;
static vt::Geometry bootGeometry{};
static std::atomic<bool> displayProfilePending{false};
static std::atomic<bool> displayProfileApplied{true};
static bool zoomRequestPending = false;
static uint32_t zoomRequestedAt = 0;
static std::atomic<bool> ready{false};
static std::atomic<uint16_t> logicalColumns{80};
static std::atomic<bool> bootConfigComplete{false};
static bool bootFrameReady = false;
static uint32_t bootFrameFlushedAt = 0;
static std::atomic<bool> pauseRequested{false}, pauseAcknowledged{false};
static std::atomic<bool> textBenchmarkRequested{false}, textBenchmarkFinished{false};
static std::atomic<uint32_t> rebootScheduledAt{0};
static std::atomic<uint32_t> diskActivityUntil{0};
static std::atomic<uint32_t> textBenchmarkElapsed{0}, textBenchmarkByteCount{0}, textBenchmarkCharCount{0};
static uint32_t replyOverruns = 0;
static bool rawInputMode = false;
static bool lineSessionActive = true;
static bool lineModelAuthoritative = false;
static bool localComposeActive = false;
static bool localComposeRedrawPending = false;
static uint32_t localComposeRedrawAt = 0;
static uint8_t localComposeSuppressBackspaces = 0;
static char localComposeOriginal[vt::LineHistory::MaxLine] = {};
static uint8_t localComposeOriginalLength = 0, localComposeOriginalCursor = 0;
enum MbasicEditBridge : uint8_t { BridgeNone, BridgeIssueX, BridgeCapture };
static MbasicEditBridge mbasicEditBridge = BridgeNone;
static uint32_t mbasicEditBridgeAt = 0;
// BASIC-80's EDIT command has its own small modal editor.  This state is
// intentionally enabled only by the MBASIC profile, never guessed globally.
static bool mbasicEditMode = false;
static bool mbasicEditStartPending = false;
static uint32_t mbasicEditStartAt = 0;
enum MbasicStartKind : uint8_t { MbasicStartNone, MbasicStartList, MbasicStartCurrent };
static MbasicStartKind mbasicEditStartKind = MbasicStartNone;
static uint8_t mbasicEditStartCursor = 0, mbasicEditStartSuffix[2] = {};
static uint8_t mbasicEditStartSuffixLength = 0;
static uint8_t selectedBank = 0;
// A prompt already painted on the screen is not proof that the CCP is still
// reading input: an app can start before the cursor leaves its launcher row.
static bool ccpPromptLive = false;
// The SD file is deliberately ordinary text: one section per history bank,
// and each retained line starts with >.  Keep I/O board-specific and let the
// runtime only own parsing, serialisation, and a low-write-rate dirty flag.
static uint32_t historyObservedGeneration = 0;
static uint32_t historyChangedAt = 0;
static bool historySavePending = false;
static std::atomic<bool> historyForceSaveRequested{false};
// The RunCPM/storage core owns INI writes. Publish only dirty persistent
// fields after brightness or palette changes; view zoom is temporary.
static std::atomic<bool> configSavePending{false};
static std::atomic<uint8_t> configSaveDirty{0};
static std::atomic<bool> configForceSaveRequested{false};
static std::atomic<uint8_t> configSaveBacklight{16};
static std::atomic<uint8_t> configSaveZoom{1};
static std::atomic<uint8_t> configSavePalette{0};
static int requestedPalette = -1;
static uint8_t activePalette = 0;
static std::atomic<bool> configPalettePending{false};
static std::atomic<uint32_t> configChangedAt{0};
static uint8_t historyLoadBank = 0, historyLoadLength = 0;
static char historyLoadText[vt::LineHistory::MaxLine] = {};
// Screen inspection is wider than editable command history on the W550.
// Keep this independent scratch storage at the compiled terminal maximum.
static char screenRowCapture[vt::MaxCols] = {};
static bool historyNoticeActive = false;
static bool historyNoticeSendOnSelect = false;
static bool historyNoticeRaw = false;
static uint8_t historyNoticeBank = 0, historyNoticeRow = 0;
static uint32_t historyNoticeUntil = 0;
static uint8_t historyNoticePaper[vt::MaxCols], historyNoticeChars[vt::MaxCols];
static uint8_t historyNoticeAttrs[vt::MaxCols], historyNoticeFlags[vt::MaxCols];
static void showHistoryBank(uint8_t bank, bool peek = false);
static void restoreHistoryNotice();
static void setRawInputMode(bool enabled);
static void redrawLocalCompose();
static void finishLocalCompose(bool submit);
static void mbasicEditBridgePoll();
static void terminalReply(void *, const uint8_t *data, size_t n) {
    if (replies.free() < n) {
        replyOverruns++;
        return;
    }
    for (size_t i = 0; i < n; i++)
        replies.push(data[i]);
}
static vt::Terminal terminal(terminalReply);
static vt::LineHistory::Recall recallSent{}, recallPending{};
static bool recallInFlight = false, recallPendingValid = false;
static uint32_t recallSentAt = 0;
static vt::LineHistory::Recall recallStaged{};
static bool recallStagedValid = false;
static uint32_t recallStagedAt = 0;
static uint8_t profileCommandStaged[vt::HistoryProfile::MaxCommandBytes] = {};
static uint8_t profileCommandStagedLength = 0;
static uint32_t profileCommandStagedAt = 0;
static void resetRecallTransport() {
    recallInFlight = recallPendingValid = recallStagedValid = false;
}
static void resetLineSession() {
    lineModelAuthoritative = false;
    mbasicEditMode = false;
    mbasicEditStartPending = false;
    mbasicEditStartKind = MbasicStartNone;
    localComposeActive = false;
    resetRecallTransport();
    if (!history) {
        lineSessionActive = false;
        return;
    }
    lineSessionActive = profiles.get(history->activeBank()).activatePrefixLength == 0;
}
static int rowPrefixEndColumn(const uint8_t *prefix, uint8_t length) {
    if (!length)
        return -1;
    const int first = terminal.index(0, terminal.y);
    int col = 0;
    while (col < terminal.x && col < 8 && terminal.charRAM[first + col] == ' ')
        ++col;
    if (col + length > terminal.x || col + length > 8 ||
        memcmp(terminal.charRAM + first + col, prefix, length))
        return -1;
    return col + length;
}
static void observeLineSession() {
    if (!history || lineSessionActive)
        return;
    const vt::HistoryProfile &profile = profiles.get(history->activeBank());
    const int first = terminal.index(0, terminal.y);
    bool activated = false;
    // A prompt must be at the start after optional display spaces. Searching
    // anywhere near column zero can mistake a source operator for a prompt.
    activated = rowPrefixEndColumn(profile.activatePrefixBytes,
                                   profile.activatePrefixLength) >= 0;
    if (!activated && profile.activateNumberedLine) {
        // Numbered source editors can display an editable row without their
        // ordinary immediate-input prompt. The profile opts into this shape.
        int col = 0;
        while (col < terminal.x && col < 4 && terminal.charRAM[first + col] == ' ')
            ++col;
        const int digits = col;
        while (col < terminal.x && col - digits < 5 &&
               terminal.charRAM[first + col] >= '0' && terminal.charRAM[first + col] <= '9')
            ++col;
        activated = col > digits && col < terminal.x && terminal.charRAM[first + col] == ' ';
    }
    if (activated) {
        history->discardCurrent();
        lineSessionActive = true;
        lineModelAuthoritative = false;
    }
}
static int cpmPromptEndColumn() {
    const int first = terminal.index(0, terminal.y);
    int start = 0;
    while (start < terminal.x && start < 4 && terminal.charRAM[first + start] == ' ')
        ++start;
    if (start < terminal.x && terminal.charRAM[first + start] == '/') {
        if (start + 4 >= terminal.x) return -1;
        const uint8_t drive = terminal.charRAM[first + start + 1];
        if (!((drive >= 'a' && drive <= 'p') ||
              (drive >= 'A' && drive <= 'P')) ||
            terminal.charRAM[first + start + 2] != '/') return -1;
        int col = start + 3, length = 0;
        while (col < terminal.x && length < 8) {
            const uint8_t c = terminal.charRAM[first + col];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-')) break;
            ++col; ++length;
        }
        return length && col < terminal.x &&
               terminal.charRAM[first + col] == '>' ? col : -1;
    }
    const uint8_t drive = terminal.charRAM[first + start];
    if (drive < 'A' || drive > 'P')
        return -1;
    int col = start + 1, digits = 0;
    while (digits < 2 && col < terminal.x) {
        const uint8_t c = terminal.charRAM[first + col];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
            break;
        ++col; ++digits;
    }
    return col < terminal.x && terminal.charRAM[first + col] == '>' ? col : -1;
}
// Bank 0 is temporarily effective while an app profile's on_select command
// reaches its CP/M launcher prompt (for example A7>).  That is not a general
// CCP editing session: the next program can interpret Ctrl-A/Ctrl-G as data.
// Reserve RunCPM's special in-place recall transport for an explicitly
// selected CP/M/GENERAL session.
static bool useCpmRecallTransport() {
    return history && history->activeBank() == 0 && selectedBank == 0 &&
           ccpPromptLive && cpmPromptEndColumn() >= 0;
}
static void selectEffectiveBank(uint8_t bank) {
    if (!history || history->activeBank() == bank)
        return;
    history->discardCurrent();
    history->select(bank);
    resetLineSession();
}
static void updateEffectiveBank() {
    if (!history || historyNoticeActive)
        return;
    selectEffectiveBank(ccpPromptLive && cpmPromptEndColumn() >= 0 ? 0 : selectedBank);
    observeLineSession();
}
static uint8_t readCursorRow(char *text, size_t capacity, uint8_t &cursor) {
    if (!history)
        return 0;
    const vt::HistoryProfile &profile = profiles.get(history->activeBank());
    const int first = terminal.index(0, terminal.y);
    int start = 0;
    const int promptEnd = rowPrefixEndColumn(profile.linePrefixBytes,
                                             profile.linePrefixLength);
    if (promptEnd >= 0)
        start = promptEnd;
    if (profile.trimLeft)
        while (start < terminal.cols && terminal.charRAM[first + start] == ' ')
            ++start;
    int end = terminal.cols;
    while (end > start && terminal.charRAM[first + end - 1] == ' ')
        --end;
    uint8_t length = uint8_t(std::min(end - start, int(capacity)));
    cursor = uint8_t(std::max(0, std::min(terminal.x - start, int(length))));
    memcpy(text, terminal.charRAM + first + start, length);
    return length;
}
static void syncHistoryFromScreen() {
    if (!history)
        return;
    const vt::HistoryProfile &profile = profiles.get(history->activeBank());
    if (!profile.screenLine || !lineSessionActive)
        return;
    // An app's first output can leave its submitted launcher row under the
    // cursor briefly; never import that old CCP command into app history.
    if (history->activeBank() != 0 && !ccpPromptLive && cpmPromptEndColumn() >= 0)
        return;
    uint8_t cursor;
    uint8_t length = readCursorRow(screenRowCapture, sizeof(screenRowCapture), cursor);
    history->replaceCurrent(screenRowCapture, length, cursor);
    lineModelAuthoritative = true;
}
static void keyboardSend(void *, const uint8_t *data, size_t n) {
    if (historyNoticeActive)
        return;
    if (n)
        resetRecallTransport();
    // The event loop reserves enough slots for a recalled line replacement.
    for (size_t i = 0; i < n; i++) {
        const vt::HistoryProfile *profile = history ? &profiles.get(history->activeBank()) : nullptr;
        if (localComposeActive) {
            if (data[i] == 13) finishLocalCompose(true);
            else if (data[i] == 27) finishLocalCompose(false);
            else if (data[i] == 8 || data[i] == 127 || (data[i] >= 0x20 && data[i] <= 0x7e)) {
                history->input(data[i]);
                redrawLocalCompose();
            }
            continue;
        }
        if (history && profile->inputAdapter == vt::InputAdapterMbasicNative && mbasicEditMode && data[i] == 27) {
            // In BASIC-80 EDIT mode Q discards the line.  Make physical Esc
            // perform that familiar cancellation without changing ordinary
            // MBASIC immediate input.
            input.push('Q');
            history->discardCurrent();
            mbasicEditMode = false;
            mbasicEditStartPending = false;
            mbasicEditStartKind = MbasicStartNone;
            lineModelAuthoritative = false;
            continue;
        }
        if (history && profile->screenLine && !lineModelAuthoritative &&
            (data[i] == 13 || data[i] == 8 || data[i] == 127 ||
             (data[i] >= 0x20 && data[i] <= 0x7e && !history->currentLength())))
            syncHistoryFromScreen();
        const bool capture = history && (!profile->screenLine || lineSessionActive);
        const bool beginMbasicEdit = capture && profile->inputAdapter == vt::InputAdapterMbasicNative && data[i] == 13 &&
                                     history->currentStartsWithIgnoreCase("edit ");
        const bool beginMbasicComposeEdit = capture &&
            profile->inputAdapter == vt::InputAdapterLocalCompose && data[i] == 13 &&
            history->currentStartsWithIgnoreCase("edit ");
        if (beginMbasicEdit) {
            // Do not retain the EDIT command itself.  MBASIC's L command then
            // prints the actual source line, which becomes the history entry.
            history->discardCurrent();
            input.push(13);
            // MBASIC changes to its modal editor only after consuming CR.
            // Queueing L immediately can make it race the command parser,
            // leaving only the line number visible.  Give it a short, local
            // settle interval, then ask the editor to list the whole line.
            mbasicEditStartPending = true;
            mbasicEditStartAt = millis();
            mbasicEditStartKind = MbasicStartList;
            mbasicEditMode = true;
            lineModelAuthoritative = false;
            continue;
        }
        if (beginMbasicComposeEdit) {
            history->discardCurrent();
            input.push(13);
            mbasicEditBridge = BridgeIssueX;
            mbasicEditBridgeAt = millis();
            lineModelAuthoritative = false;
            continue;
        }
        if (capture)
            history->input(data[i]);
        input.push(data[i]);
        if (data[i] == 13)
            ccpPromptLive = false;
        // Internal CCP's BDOS read returns as soon as the 125th printable
        // character arrives, without a CR.  Close the local history model at
        // the same boundary so subsequent input is not appended to that
        // already-dispatched command.  The saved bank-0 entry is capped to
        // 124 characters by LineHistory for safe recall.
        const bool ccpAutoSubmitted = capture && history->activeBank() == 0 &&
            data[i] >= 0x20 && data[i] <= 0x7e && history->currentLength() == 125;
        if (ccpAutoSubmitted)
            history->input(13);
        if (mbasicEditMode && (data[i] == 13 || data[i] == 27)) {
            mbasicEditMode = false;
            mbasicEditStartPending = false;
            mbasicEditStartKind = MbasicStartNone;
        }
        if (capture && (ccpAutoSubmitted || data[i] == 13 ||
                        (profile->escapeCancels && data[i] == 27))) {
            lineModelAuthoritative = false;
        } else if (capture && (data[i] == 8 || data[i] == 127 ||
                               (data[i] >= 0x20 && data[i] <= 0x7e))) {
            lineModelAuthoritative = true;
        }
        if (capture && profile->escapeCancels && data[i] == 27)
            history->discardCurrent();
    }
}
static void beginLocalCompose() {
    if (!history)
        return;
    if (!lineModelAuthoritative)
        syncHistoryFromScreen();
    // The guest already owns the current typed/recalled input.  Remove it
    // before the terminal takes over the local edit buffer.
    localComposeSuppressBackspaces = history->currentLength();
    history->copyCurrent(localComposeOriginal, localComposeOriginalLength, localComposeOriginalCursor);
    for (uint8_t i = 0; i < localComposeSuppressBackspaces; ++i)
        input.push(8);
    localComposeActive = true;
    localComposeRedrawPending = true;
    localComposeRedrawAt = millis();
}
static void redrawLocalCompose() {
    if (!history)
        return;
    char text[vt::LineHistory::MaxLine]; uint8_t length, cursor;
    history->copyCurrent(text, length, cursor);
    terminal.feed('\r'); terminal.feed(27); terminal.feed('['); terminal.feed('2'); terminal.feed('K');
    for (uint8_t i = 0; i < length; ++i) terminal.feed(uint8_t(text[i]));
    for (uint8_t i = cursor; i < length; ++i) terminal.feed(8);
}
static void finishLocalCompose(bool submit) {
    if (!history || !localComposeActive)
        return;
    char text[vt::LineHistory::MaxLine]; uint8_t length, cursor;
    history->copyCurrent(text, length, cursor);
    terminal.feed('\r'); terminal.feed(27); terminal.feed('['); terminal.feed('2'); terminal.feed('K'); terminal.feed('\r');
    localComposeActive = false;
    localComposeRedrawPending = false;
    if (submit) {
        for (uint8_t i = 0; i < length; ++i) input.push(uint8_t(text[i]));
        input.push(13);
        history->input(13);
    } else {
        // Restore the original pending guest line; cancel must not execute or
        // silently discard it merely because editing moved local.
        for (uint8_t i = 0; i < localComposeOriginalLength; ++i)
            input.push(uint8_t(localComposeOriginal[i]));
        history->replaceCurrent(localComposeOriginal, localComposeOriginalLength, localComposeOriginalCursor);
    }
    lineModelAuthoritative = false;
}
static void mbasicEditBridgePoll() {
    if (mbasicEditBridge == BridgeNone || uint32_t(millis() - mbasicEditBridgeAt) < 100)
        return;
    if (mbasicEditBridge == BridgeIssueX) {
        input.push('X');
        mbasicEditBridge = BridgeCapture;
        mbasicEditBridgeAt = millis();
        return;
    }
    // X has rendered the complete numbered source line.  Capture the visible
    // row, leave MBASIC's native modal editor, then continue with the generic
    // terminal-owned composer.  Escape ends X insertion; Q returns to OK.
    uint8_t cursor;
    uint8_t length = readCursorRow(screenRowCapture, sizeof(screenRowCapture), cursor);
    const uint8_t historyLength = std::min<uint8_t>(length, vt::LineHistory::MaxLine);
    if (history && length) {
        history->replaceCurrent(screenRowCapture, historyLength, historyLength);
        input.push(27); input.push('Q');
        localComposeActive = true;
        localComposeOriginalLength = historyLength;
        localComposeOriginalCursor = historyLength;
        memcpy(localComposeOriginal, screenRowCapture, historyLength);
        localComposeRedrawPending = true;
        localComposeRedrawAt = millis();
    }
    mbasicEditBridge = BridgeNone;
}
static void beginMbasicCurrentEdit(vt::AppKey key) {
    if (!history)
        return;
    if (!lineModelAuthoritative)
        syncHistoryFromScreen();
    uint8_t suffixLength = 0;
    uint8_t suffix[2] = {};
    switch (key) {
    case vt::AppLeft:   history->left(); break;
    case vt::AppRight:  history->right(); break;
    case vt::AppHome:   history->home(); break;
    case vt::AppEnd:    history->end(); break;
    case vt::AppDelete:
        history->eraseAt(); suffix[0] = '1'; suffix[1] = 'D'; suffixLength = 2; break;
    default: return;
    }
    // Ctrl+A enters BASIC-80 Edit Mode at the beginning of the currently
    // typed/recalled line.  Position after its modal transition, then carry
    // out a requested deletion.
    input.push(1);
    mbasicEditMode = true;
    mbasicEditStartPending = true;
    mbasicEditStartAt = millis();
    mbasicEditStartKind = MbasicStartCurrent;
    mbasicEditStartCursor = history->currentCursor();
    mbasicEditStartSuffixLength = suffixLength;
    memcpy(mbasicEditStartSuffix, suffix, suffixLength);
    lineModelAuthoritative = true;
}
static void mbasicEditKey(vt::AppKey key) {
    if (!history || !mbasicEditMode)
        return;
    if (!lineModelAuthoritative)
        syncHistoryFromScreen();
    switch (key) {
    case vt::AppLeft:
        history->left(); input.push(8); break;
    case vt::AppRight:
        history->right(); input.push(' '); break;
    case vt::AppHome:
        history->home(); input.push('L'); break;
    case vt::AppEnd:
        history->end(); input.push('X'); input.push(27); break;
    case vt::AppDelete:
        history->eraseAt(); input.push('1'); input.push('D'); break;
    case vt::AppPageUp:
    case vt::AppPageDown:
        input.push('Q'); history->discardCurrent(); mbasicEditMode = false; break;
    default:
        return;
    }
    lineModelAuthoritative = true;
}
static void mbasicEditBackspace() {
    if (!history || !mbasicEditMode)
        return;
    if (!lineModelAuthoritative)
        syncHistoryFromScreen();
    history->left();
    history->eraseAt();
    // BASIC-80 rubout only moves left; follow it with its explicit one-byte
    // delete command to give the PC Backspace its usual meaning.
    input.push(8); input.push('1'); input.push('D');
    lineModelAuthoritative = true;
}
static void sendRecall(const vt::LineHistory::Recall &recalled) {
    const vt::HistoryProfile &profile = profiles.get(history->activeBank());
    if (useCpmRecallTransport()) {
        // RunCPM's ^U redraws the line on the next row, so it cannot be used
        // for a seamless recall replacement. Move to the tracked line start
        // and delete every original character in place instead.
        for (uint8_t i = 0; i < recalled.cursor; ++i)
            input.push(0x01); // ^A: one character left
        for (uint8_t i = 0; i < recalled.erase; ++i)
            input.push(0x07); // ^G: delete character under cursor
    } else if (profile.historyClearLength) {
        for (uint8_t i = 0; i < profile.historyClearLength; ++i)
            input.push(profile.historyClearBytes[i]);
    } else {
        for (uint8_t i = 0; i < recalled.erase; ++i)
            input.push(8);
    }
    recallInFlight = profile.screenLine;
    if (profile.screenLine && profile.historyClearLength && profile.historyRetypeDelayMs) {
        recallStaged = recalled;
        recallStagedValid = true;
        recallStagedAt = millis();
        return;
    }
    for (uint8_t i = 0; i < recalled.length; ++i)
        input.push(uint8_t(recalled.text[i]));
    recallSent = recalled;
    recallSentAt = millis();
}
static void historyRecall(bool previous) {
    vt::LineHistory::Recall recalled;
    if (!lineModelAuthoritative)
        syncHistoryFromScreen();
    if (!history || (profiles.get(history->activeBank()).screenLine && !lineSessionActive) ||
        !history->recall(previous, recalled))
        return;
    if (localComposeActive) {
        if (!localComposeRedrawPending) redrawLocalCompose();
        return;
    }
    const vt::HistoryProfile &profile = profiles.get(history->activeBank());
    if (profile.screenLine && recallInFlight) {
        recallPending = recalled;
        recallPendingValid = true;
    } else {
        sendRecall(recalled);
    }
    lineModelAuthoritative = true;
}
static void recallPoll() {
    if (!history || !recallInFlight)
        return;
    if (recallStagedValid) {
        const vt::HistoryProfile &profile = profiles.get(history->activeBank());
        if (uint32_t(millis() - recallStagedAt) < profile.historyRetypeDelayMs)
            return;
        recallStagedValid = false;
        for (uint8_t i = 0; i < recallStaged.length; ++i)
            input.push(uint8_t(recallStaged.text[i]));
        recallSent = recallStaged;
        recallSentAt = millis();
        return;
    }
    uint8_t cursor;
    uint8_t length = readCursorRow(screenRowCapture, sizeof(screenRowCapture), cursor);
    (void)cursor;
    const bool matched = length == recallSent.length &&
                         !memcmp(screenRowCapture, recallSent.text, length);
    if (!matched && uint32_t(millis() - recallSentAt) < 1000)
        return;
    recallInFlight = false;
    if (!recallPendingValid)
        return;
    recallPendingValid = false;
    if (recallPending.length == recallSent.length &&
        !memcmp(recallPending.text, recallSent.text, recallPending.length))
        return;
    sendRecall(recallPending);
}
static void profileKey(vt::AppKey key) {
    if (!history)
        return;
    const vt::KeyBinding *resolved = profiles.bindingFor(history->activeBank(),
                                                         vt::chordForAppKey(key));
    if (!resolved) return;
    const vt::KeyBinding &binding = *resolved;
    const vt::HistoryProfile &profile = profiles.get(history->activeBank());
    const bool mbasicEditKeyAction = key == vt::AppLeft || key == vt::AppRight ||
                                     key == vt::AppHome || key == vt::AppEnd ||
                                     key == vt::AppDelete || key == vt::AppPageUp ||
                                     key == vt::AppPageDown;
    if (profile.inputAdapter == vt::InputAdapterLocalCompose &&
        (key == vt::AppLeft || key == vt::AppRight || key == vt::AppHome ||
         key == vt::AppEnd || key == vt::AppDelete || key == vt::AppInsert ||
         key == vt::AppPageUp || key == vt::AppPageDown)) {
        if ((key == vt::AppPageUp || key == vt::AppPageDown) && localComposeActive) {
            finishLocalCompose(false);
            return;
        }
        if (key == vt::AppPageUp || key == vt::AppPageDown)
            return;
        if (!localComposeActive)
            beginLocalCompose();
        if (key == vt::AppLeft) history->left();
        else if (key == vt::AppRight) history->right();
        else if (key == vt::AppHome) history->home();
        else if (key == vt::AppEnd) history->end();
        else if (key == vt::AppDelete) history->eraseAt();
        else history->toggleInsert();
                if (!localComposeRedrawPending) redrawLocalCompose();
        return;
    }
    if (profile.inputAdapter == vt::InputAdapterMbasicNative && mbasicEditKeyAction) {
        if (mbasicEditMode)
            mbasicEditKey(key);
        else if (key != vt::AppPageUp && key != vt::AppPageDown)
            beginMbasicCurrentEdit(key);
        return; // never emit edit commands during ordinary MBASIC input
    }
    // RunCPM overloads ^B and ^W: on an empty CCP line they toggle/recall
    // rather than simply moving to its start/end.  At the effective CP/M
    // prompt, make physical Home/End deterministic from our tracked line by
    // using only the unambiguous one-cell editor motions.  This also prevents
    // a native recalled line being silently prepended to 7xtgv history.
    if (useCpmRecallTransport() && (key == vt::AppHome || key == vt::AppEnd)) {
        const uint8_t cursor = history->currentCursor();
        const uint8_t length = history->currentLength();
        const uint8_t count = key == vt::AppHome ? cursor : uint8_t(length - cursor);
        const uint8_t motion = key == vt::AppHome ? 0x01 : 0x06; // ^A / ^F
        for (uint8_t i = 0; i < count; ++i)
            input.push(motion);
        if (key == vt::AppHome)
            history->home();
        else
            history->end();
        lineModelAuthoritative = true;
        return;
    }
    if (binding.kind == vt::BindingHistoryPrevious)
        historyRecall(true);
    else if (binding.kind == vt::BindingHistoryNext)
        historyRecall(false);
    else if (binding.kind == vt::BindingSend || binding.kind == vt::BindingSendCancel) {
        resetRecallTransport();
        if (!lineModelAuthoritative)
            syncHistoryFromScreen();
        if (key == vt::AppLeft) history->left();
        else if (key == vt::AppRight) history->right();
        else if (key == vt::AppHome) history->home();
        else if (key == vt::AppEnd) history->end();
        else if (key == vt::AppDelete) history->eraseAt();
        else if (key == vt::AppInsert) history->toggleInsert();
        for (uint8_t i = 0; i < binding.length; ++i)
            input.push(binding.bytes[i]);
        lineModelAuthoritative = true;
        if (binding.kind == vt::BindingSendCancel) {
            history->discardCurrent();
            lineModelAuthoritative = false;
        }
    }
}
static void profileCommand(bool exitCommand) {
    if (!history)
        return;
    const vt::HistoryProfile &profile = profiles.get(history->activeBank());
    const uint8_t *command = exitCommand ? profile.exitBytes : profile.clearBytes;
    const uint8_t length = exitCommand ? profile.exitLength : profile.clearLength;
    if (!length)
        return;
    if (!lineModelAuthoritative)
        syncHistoryFromScreen();
    const uint8_t erase = history->discardCurrent();
    if (profile.historyClearLength) {
        for (uint8_t i = 0; i < profile.historyClearLength; ++i)
            input.push(profile.historyClearBytes[i]);
    } else {
        // Profiles such as MBASIC do not have a native clear-line sequence.
        // Remove a recalled/partially typed visible line before injecting a
        // terminal-generated exit or clear command.
        for (uint8_t i = 0; i < erase; ++i)
            input.push(8);
    }
    if (profile.commandNewLine)
        input.push(13);
    // Generated exit/clear commands are terminal controls, not user input.
    // Send them to the application but never add them to its history bank.
    if (profile.historyClearLength && profile.historyRetypeDelayMs) {
        memcpy(profileCommandStaged, command, length);
        profileCommandStagedLength = length;
        profileCommandStagedAt = millis();
    } else {
        for (uint8_t i = 0; i < length; ++i)
            input.push(command[i]);
    }
    lineModelAuthoritative = false;
    if (exitCommand)
        lineSessionActive = false;
    resetRecallTransport();
}
static void profileCommandPoll() {
    if (!profileCommandStagedLength)
        return;
    const vt::HistoryProfile &profile = profiles.get(history ? history->activeBank() : 0);
    if (uint32_t(millis() - profileCommandStagedAt) < profile.historyRetypeDelayMs)
        return;
    for (uint8_t i = 0; i < profileCommandStagedLength; ++i)
        input.push(profileCommandStaged[i]);
    profileCommandStagedLength = 0;
}
static void mbasicEditPoll() {
    if (!mbasicEditStartPending || uint32_t(millis() - mbasicEditStartAt) < 100)
        return;
    mbasicEditStartPending = false;
    if (!mbasicEditMode)
        return;
    if (mbasicEditStartKind == MbasicStartList) {
        input.push('L');
    } else if (mbasicEditStartKind == MbasicStartCurrent) {
        for (uint8_t i = 0; i < mbasicEditStartCursor; ++i)
            input.push(' ');
        for (uint8_t i = 0; i < mbasicEditStartSuffixLength; ++i)
            input.push(mbasicEditStartSuffix[i]);
    }
    mbasicEditStartKind = MbasicStartNone;
}
static void requestMirrorSnapshot();
static void requestHostIdentity();
static void requestZoomLevel(uint8_t zoom);
static void applyDisplayProfile(const vt::DisplayProfile &profile,
                                const vt::Geometry *overrideGeometry = nullptr);
static void applyViewZoom(uint8_t zoom);
static void replyDisplayInfo();
static void terminalBacklight(uint8_t value) {
    const uint8_t before = xtgvBacklightGetRemembered();
    if (value <= 32) // old protocol full-on 32 aliases canonical level 31
        xtgvBacklightSet(value);
    else if (value == 33)
        xtgvBacklightAdjust(1);
    else if (value == 34)
        xtgvBacklightAdjust(-1);
    const uint8_t remembered = xtgvBacklightGetRemembered();
    if (remembered != before) {
        configSaveBacklight.store(remembered, std::memory_order_release);
        configChangedAt.store(millis(), std::memory_order_release);
        configSaveDirty.fetch_or(ConfigBacklightDirty, std::memory_order_acq_rel);
        configSavePending.store(true, std::memory_order_release);
    }
}
static void terminalControl(void *, vt::Terminal::ControlEvent event, uint8_t value,
                            uint16_t cols = 0, uint16_t rows = 0) {
    if (event == vt::Terminal::Backlight)
        terminalBacklight(value);
    else if (event == vt::Terminal::Zoom)
        requestZoomLevel(value);
    else if (event == vt::Terminal::Palette)
        requestedPalette = value;
    else if (event == vt::Terminal::QueryInfo)
        replyDisplayInfo();
    else if (event == vt::Terminal::LoadPreset) {
        zoomRequestPending = false;
        displayProfilePending.store(false, std::memory_order_release);
        if (!value)
            applyDisplayProfile(bootDisplayProfile, &bootGeometry);
        else {
            vt::DisplayProfile selected = activeDisplayProfile;
            selected.viewZoom = value;
            applyDisplayProfile(selected);
        }
    } else if (event == vt::Terminal::SetGuestGeometry) {
        zoomRequestPending = false;
        displayProfilePending.store(false, std::memory_order_release);
        vt::DisplayProfile selected = activeDisplayProfile;
        selected.viewZoom = value;
        const vt::Geometry geometry{cols, rows};
        applyDisplayProfile(selected, &geometry);
    }
    else if (event == vt::Terminal::FullClear)
        terminal.compositionDirty = true;
}
static void requestZoom(int delta) {
    // A burst of keys should select successive levels but cause only one
    // expensive full-panel/mirror reconstruction after the keys settle.
    vt::DisplayProfile profile = zoomRequestPending ? pendingDisplayProfile : activeDisplayProfile;
    int zoom = std::max(1, std::min(4, int(profile.viewZoom) + delta));
    if (zoom == profile.viewZoom)
        return;
    profile.viewZoom = uint8_t(zoom);
    pendingDisplayProfile = profile;
    zoomRequestPending = true;
    zoomRequestedAt = millis();
    displayProfilePending.store(true, std::memory_order_release);
}
static void requestZoomLevel(uint8_t zoom) {
    if (zoom < 1 || zoom > 4)
        return;
    vt::DisplayProfile profile = zoomRequestPending ? pendingDisplayProfile : activeDisplayProfile;
    if (profile.viewZoom == zoom)
        return;
    profile.viewZoom = zoom;
    pendingDisplayProfile = profile;
    zoomRequestPending = true;
    zoomRequestedAt = millis();
    displayProfilePending.store(true, std::memory_order_release);
}
static void applySelectedProfile() {
    // This is an explicit user action, unlike the temporary effective-bank
    // change inferred from a CCP prompt. In-band mode commands stay transient.
    restoreHistoryNotice();
    terminal.setMode(profiles.modeFor(selectedBank) == vt::OutputAnsi);
    const vt::Geometry geometry = profiles.geometryFor(selectedBank, activeDisplayProfile.viewZoom);
    if (terminal.cols != geometry.cols || terminal.rows != geometry.rows)
        applyDisplayProfile(activeDisplayProfile);
}
static void discardCurrentForProfileSwitch() {
    // The history model can contain text from a screen editor. Only the CCP
    // owns an editable command line that we may erase with guest backspaces.
    restoreHistoryNotice();
    const bool atCcp = ccpPromptLive && cpmPromptEndColumn() >= 0;
    uint8_t erase = history->discardCurrent();
    if (atCcp)
        while (erase--) input.push(8);
}
static void keyboardAction(void *, vt::Keyboard::Action action) {
    // Recall waits until the transient label is gone, but bank changes remain
    // live so Alt+PgUp/PgDn can browse banks and redraw #n immediately.
    if (historyNoticeActive &&
        (action == vt::Keyboard::HistoryPrevious || action == vt::Keyboard::HistoryNext))
        return;
    if (action == vt::Keyboard::MirrorSnapshot) {
        requestHostIdentity();
        requestMirrorSnapshot();
        return;
    }
    if (action == vt::Keyboard::ZoomIn) {
        requestZoom(1);
        return;
    }
    if (action == vt::Keyboard::ZoomOut) {
        requestZoom(-1);
        return;
    }
    if (action == vt::Keyboard::HistoryDelete) {
        if (history) {
            const uint8_t cursor = history->currentCursor();
            const uint8_t length = history->currentLength();
            if (history->deleteRecalled()) {
                const vt::HistoryProfile &profile = profiles.get(history->activeBank());
                resetRecallTransport();
                if (localComposeActive && profile.inputAdapter == vt::InputAdapterLocalCompose) {
                    // The generic composer owns this displayed line. Do not
                    // send rubouts into MBASIC/BBC; redraw its now-empty
                    // model directly, so deletion works inside applications.
                    redrawLocalCompose();
                } else if (useCpmRecallTransport()) {
                    for (uint8_t i = 0; i < cursor; ++i) input.push(1);
                    for (uint8_t i = 0; i < length; ++i) input.push(7);
                } else if (profile.historyClearLength) {
                    // BBC BASIC has a real whole-line clear sequence. Rubout
                    // from the current cursor misses the suffix of EDIT rows.
                    for (uint8_t i = 0; i < profile.historyClearLength; ++i)
                        input.push(profile.historyClearBytes[i]);
                } else {
                    for (uint8_t i = 0; i < length; ++i) input.push(8);
                }
                // The queued erase motions are authoritative even before
                // their echo appears on screen.  This lets another Up reuse
                // the retained traversal/filter state immediately.
                lineModelAuthoritative = true;
            }
        }
        return;
    }
    if (action >= vt::Keyboard::Backlight0 && action <= vt::Keyboard::Backlight34) {
        terminalBacklight(uint8_t(action - vt::Keyboard::Backlight0));
        return;
    }
    if (action == vt::Keyboard::HistoryBankPrevious) {
        if (history) {
            discardCurrentForProfileSwitch();
            selectedBank = uint8_t((int(selectedBank) + vt::HistoryProfiles::Banks - 1) %
                                   vt::HistoryProfiles::Banks);
            history->select(selectedBank);
            resetLineSession();
            applySelectedProfile();
            showHistoryBank(history->activeBank());
        }
        return;
    }
    if (action == vt::Keyboard::HistoryBankNext) {
        if (history) {
            discardCurrentForProfileSwitch();
            selectedBank = uint8_t((selectedBank + 1) % vt::HistoryProfiles::Banks);
            history->select(selectedBank);
            resetLineSession();
            applySelectedProfile();
            showHistoryBank(history->activeBank());
        }
        return;
    }
    if (action == vt::Keyboard::HistoryBankShow) {
        if (history)
            showHistoryBank(selectedBank, true);
        return;
    }
    if (action == vt::Keyboard::RawToggle || action == vt::Keyboard::RawOn ||
        action == vt::Keyboard::RawOff) {
        bool enabled = action == vt::Keyboard::RawOn ||
                       (action == vt::Keyboard::RawToggle && !rawInputMode);
        setRawInputMode(enabled);
        return;
    }
    if (action == vt::Keyboard::ProfileExit || action == vt::Keyboard::ProfileClear) {
        const vt::GlobalKey physical = action == vt::Keyboard::ProfileExit
                                          ? vt::GlobalKeyDualAltEscape
                                          : vt::GlobalKeyDualAltDelete;
        if (profiles.global().exitKey == physical)
            profileCommand(true);
        else if (profiles.global().clearKey == physical)
            profileCommand(false);
        return;
    }
    if (action == vt::Keyboard::ProfileBackspace) {
        if (history && profiles.get(history->activeBank()).inputAdapter == vt::InputAdapterLocalCompose && localComposeActive) {
            history->input(8);
            if (!localComposeRedrawPending) redrawLocalCompose();
        }
        else
        if (history && profiles.get(history->activeBank()).inputAdapter == vt::InputAdapterMbasicNative && mbasicEditMode)
            mbasicEditBackspace();
        else {
            const uint8_t backspace = 8;
            keyboardSend(nullptr, &backspace, 1);
        }
        return;
    }
    if (action >= vt::Keyboard::HistoryBank0 && action <= vt::Keyboard::HistoryBank9) {
        if (history) {
            rawInputMode = false;
            discardCurrentForProfileSwitch();
            selectedBank = uint8_t(action - vt::Keyboard::HistoryBank0);
            history->select(selectedBank);
            resetLineSession();
            applySelectedProfile();
            showHistoryBank(history->activeBank());
        }
        return;
    }
    if (action >= vt::Keyboard::ProfileKey0 && action <= vt::Keyboard::ProfileKey29) {
        if (!historyNoticeActive)
            profileKey(vt::AppKey(action - vt::Keyboard::ProfileKey0));
        return;
    }
    historyRecall(action == vt::Keyboard::HistoryPrevious);
}
static void keyboardKeyEvent(void *, vt::KeyChord chord, const uint8_t *fallback,
                             size_t fallbackLength) {
    if (chord.modifiers == (vt::KeyCtrl | vt::KeyShift)) {
        switch (chord.base) {
        case vt::KeyLeft: terminal.panHorizontal(-1); return;
        case vt::KeyRight: terminal.panHorizontal(1); return;
        case vt::KeyUp: terminal.panVertical(-1); return;
        case vt::KeyDown: terminal.panVertical(1); return;
        case vt::KeyHome: terminal.panOverview(-1); return;
        case vt::KeyEnd: terminal.panOverview(1); return;
        default: break;
        }
    }
    if (historyNoticeActive) return;
    const int legacy = vt::appKeyForChord(chord);
    if (legacy >= 0) {
        profileKey(vt::AppKey(legacy));
        return;
    }
    const uint8_t bank = history ? history->activeBank() : 0;
    const vt::KeyBinding *binding = profiles.bindingFor(bank, chord);
    if (binding) {
        if (binding->kind == vt::BindingHistoryPrevious) historyRecall(true);
        else if (binding->kind == vt::BindingHistoryNext) historyRecall(false);
        else if (binding->kind == vt::BindingSend ||
                 binding->kind == vt::BindingSendCancel) {
            keyboardSend(nullptr, binding->bytes, binding->length);
            if (binding->kind == vt::BindingSendCancel && history) {
                history->discardCurrent();
                lineModelAuthoritative = false;
            }
        }
        return; // BindingNone explicitly suppresses the host fallback.
    }
    if (chord.base == vt::KeyBackspace && chord.modifiers == 0) {
        keyboardAction(nullptr, vt::Keyboard::ProfileBackspace);
        return;
    }
    if (chord.base == vt::KeyKpDelete && chord.modifiers == 0) {
        profileKey(vt::AppDelete);
        return;
    }
    if (chord.modifiers == 0 || chord.modifiers == vt::KeyShift) {
        if (chord.base >= vt::KeyNum0 && chord.base <= vt::KeyNum9) {
            const uint8_t digit = uint8_t('0' + chord.base - vt::KeyNum0);
            keyboardSend(nullptr, &digit, 1);
            return;
        }
    }
    if (chord.base == vt::KeyKpDecimal && chord.modifiers == 0) {
        const uint8_t dot = '.';
        keyboardSend(nullptr, &dot, 1);
        return;
    }
    keyboardSend(nullptr, fallback, fallbackLength);
}
static vt::Keyboard keyboard(terminal, keyboardSend, nullptr, keyboardAction,
                             keyboardKeyEvent);
static uint8_t rawControl[64];
static uint8_t rawControlLength = 0;
static uint32_t rawControlAt = 0;

static void rawFlush() {
    for (uint8_t i = 0; i < rawControlLength; ++i)
        input.push(rawControl[i]);
    rawControlLength = 0;
}
static bool rawControlPrefix() {
    if (rawControlLength >= 1 && rawControl[0] != 0x1b) return false;
    if (rawControlLength >= 2 && rawControl[1] != '[') return false;
    if (rawControlLength >= 3 && rawControl[2] != '>') return false;
    if (rawControlLength >= 4 && rawControl[3] != '1' && rawControl[3] != '2' &&
        rawControl[3] != '6' && rawControl[3] != '7') return false;
    if (rawControlLength >= 4 && rawControl[3] == '7') {
        if (rawControlLength >= 5 && rawControl[4] != '0') return false;
        if (rawControlLength >= 6 && rawControl[5] != ';') return false;
        for (uint8_t i = 6; i < rawControlLength; ++i) {
            const uint8_t c = rawControl[i];
            if (c == 'v') return i == rawControlLength - 1;
            if (c != ';' && (c < '0' || c > '9')) return false;
        }
        return rawControlLength < sizeof(rawControl);
    }
    if (rawControlLength >= 5) {
        if (rawControl[3] == '1' && (rawControl[4] < '4' || rawControl[4] > '7')) return false;
        if (rawControl[3] == '2' && (rawControl[4] < '0' || rawControl[4] > '9')) return false;
        if (rawControl[3] == '6' && rawControl[4] != '2' && rawControl[4] != '3' &&
            rawControl[4] != '6' && rawControl[4] != '7') return false;
    }
    if (rawControl[3] != '6' || rawControlLength < 5 || rawControl[4] != '3')
        return rawControlLength < 6 || rawControl[5] == 'v';
    if (rawControlLength >= 6 && rawControl[5] != ';') return false;
    if (rawControlLength >= 7 && (rawControl[6] < '0' || rawControl[6] > '9')) return false;
    if (rawControlLength == 8)
        return rawControl[7] == 'v' || (rawControl[6] >= '1' && rawControl[6] <= '3' &&
                                        rawControl[7] >= '0' && rawControl[7] <= '9');
    if (rawControlLength == 9)
        return rawControl[6] >= '1' && rawControl[6] <= '3' && rawControl[7] >= '0' &&
               rawControl[7] <= '9' && rawControl[8] == 'v';
    return rawControlLength < 8;
}
static int rawBacklightValue() {
    if (rawControlLength == 8 && rawControl[7] == 'v' && rawControl[6] >= '0' && rawControl[6] <= '9')
        return rawControl[6] - '0';
    if (rawControlLength == 9 && rawControl[8] == 'v' && rawControl[6] >= '1' &&
        rawControl[6] <= '3' && rawControl[7] >= '0' && rawControl[7] <= '9')
        return (rawControl[6] - '0') * 10 + rawControl[7] - '0';
    return -1;
}
static void rawFeed(uint8_t c, uint32_t now) {
    if (!rawControlLength && c != 0x1b) {
        input.push(c);
        if (c == 13)
            ccpPromptLive = false;
        return;
    }
    rawControl[rawControlLength++] = c;
    rawControlAt = now;
    if (!rawControlPrefix()) {
        rawFlush();
        return;
    }
    if (rawControlLength >= 6 && rawControl[3] == '7') {
        if (c != 'v') return;
        vt::KeyChord chord;
        uint8_t fallback[16];
        size_t fallbackLength = 0;
        if (vt::decodePrivateKeyEvent(rawControl, rawControlLength, chord,
                                      fallback, fallbackLength)) {
            rawControlLength = 0;
            for (size_t i = 0; i < fallbackLength; ++i) {
                input.push(fallback[i]);
                if (fallback[i] == 13) ccpPromptLive = false;
            }
        } else {
            rawFlush();
        }
        return;
    }
    int brightness = rawBacklightValue();
    if (brightness >= 0) {
        rawControlLength = 0;
        terminalBacklight(uint8_t(brightness));
        return;
    }
    // All established private controls are six bytes. Brightness uses the
    // longer 63;n form and was consumed above when its final v arrived.
    if (rawControlLength != 6)
        return;
    uint8_t group = rawControl[3], command = rawControl[4];
    rawControlLength = 0;
    if (group == '2') {
        rawInputMode = false;
        if (history) {
            selectedBank = uint8_t(command - '0');
            history->select(selectedBank);
            resetLineSession();
            applySelectedProfile();
            showHistoryBank(history->activeBank());
        }
    } else if (command == '4') {
        if (history) showHistoryBank(history->activeBank(), true);
    } else if (command == '5' || command == '7') {
        setRawInputMode(false);
    } else if (command == '6') {
        setRawInputMode(true);
    } else if (group == '6' && command == '2') {
        requestHostIdentity();
        requestMirrorSnapshot();
    } else if (group == '6' && command == '6') {
        requestZoom(1);
    } else if (group == '6' && command == '7') {
        requestZoom(-1);
    }
}
static void rawTick(uint32_t now) {
    if (rawControlLength && uint32_t(now - rawControlAt) >= 50)
        rawFlush();
}
static bool displayOK = false, cursorPhase = true, connected = false;
static uint32_t blinkAt = 0, bellUntil = 0, oldBells = 0;
#if defined(GFX_BUFFERED_CANVAS) || defined(GFX_DEFERRED_FLUSH)
static uint32_t displayFlushAt = 0;
static bool displayFlushPending = false;
#endif
static uint16_t pixels[6 * 10];
static uint16_t scaledPixels[24 * 40];
// xterm-256 plotting uses only a transient logical scanline.  The actual
// backing is whichever Arduino_GFX surface this target selected: the S3 canvas
// today, or a direct controller path on the RP2350 target.
static uint16_t *plotScanline = nullptr;
static int plotWidth = 0, plotX = 0, plotY = 0;
// GCLS x/y parameters are absolute physical panel pixels. Their origin does
// not move with terminal zoom, gutter width, or viewport panning. `plotX` and
// `plotY` remain logical sample positions; only samples are scaled.
static int plotZoom = 1, plotOriginX = 0, plotOriginY = 0;
static int plotPaintLeft = 0, plotPaintRight = 0;
static bool plotCapture = false;
// Remains true after CSI 0m: the TFT still contains a completed temporary
// plot even though the parser has resumed ordinary ANSI text.
static bool plotCanvasValid = false;
static int scanCell = 0, mirrorScan = 0;
static char mirrorBuffer[8192];
static size_t mirrorLength = 0, mirrorSent = 0;
static bool mirrorReset = false;
// A guest program may request an explicitly configured host action.  This
// event crosses the reconstructed mirror separately, because ordinary CP/M
// ANSI output is consumed into terminal cells before that mirror is built.
static constexpr uint8_t hostReflashEvent[] = {27, '[', '>', '6', '4', 'v'};
static uint8_t hostEventCandidate[sizeof(hostReflashEvent)];
static uint8_t hostEventCandidateLength = 0;
static bool hostReflashPending = false;
static uint8_t hostReflashSent = 0;
static char hostIdentity[96];
static size_t hostIdentityLength = 0, hostIdentitySent = 0;
static void requestHostIdentity() {
    // Coalesce repeated queries; never replace a partly transmitted frame.
    if (hostIdentityLength) return;
    const int length = snprintf(hostIdentity, sizeof(hostIdentity),
        "\033P7xtgv-id;1;%s;%s\033\\", xtgvFirmwareTarget(), xtgvFirmwareBuild());
    hostIdentityLength = length > 0 && size_t(length) < sizeof(hostIdentity) ? size_t(length) : 0;
    hostIdentitySent = 0;
}
static int activeRow = -1;
static uint8_t activeRowBank = 255;
static bool plotActiveRows[vt::MaxRows] = {};
static bool plotEndCleanupPending = false;
static uint32_t plotEndActivityAt = 0;
static bool diskIndicatorVisible = false;
static bool diskIndicatorDirty = false;
static int presentationGutter = 0;
static uint32_t displayFlushIntervalMs = 33;

static bool plotOwnsPixels() {
    // Arming GCLS does not change the display. The plot takes ownership only
    // when its first sample arrives; until then normal text and the SD LED
    // must continue to render.
    return plotCapture || plotEndCleanupPending || plotCanvasValid;
}

// A plot may hold back normal opaque redraws for a long time.  When it ends,
// retire the old highlight explicitly before allowing the next prompt row to
// become active.  This is independent of whether GCLS was typed or read from
// a CP/M SUB file.
static void retirePlotActiveRows();
static void rememberPlotActiveRow(int row);
static void plotEndCleanupPoll(uint32_t now);
static uint16_t rgb565(vt::RGB color);

static void requestMirrorSnapshot() {
    terminal.invalidateMirror();
    mirrorReset = true;
}

static void requestDisplayFlush();
static void retireDiskIndicatorForPlot();
static void applyDisplayProfile(const vt::DisplayProfile &profile,
                                const vt::Geometry *overrideGeometry) {
    const vt::DisplayHalDefaults &hal = vt::displayHalDefaults();
    int zoom = profile.viewZoom;
    if (zoom < 1 || zoom > 4) zoom = hal.defaultZoom;
    vt::Geometry geometry = overrideGeometry ? *overrideGeometry :
        profiles.geometryFor(selectedBank, uint8_t(zoom), profile.geometry[zoom - 1]);
    const int width = displayOK ? gfx->width() : hal.physicalWidth;
    const int height = displayOK ? gfx->height() : hal.physicalHeight;
    const int fitCols = vt::evenVisibleCols(width, zoom);
    const int fitRows = vt::visibleRows(height, zoom);
    if (geometry.cols < 2 || geometry.cols > vt::MaxCols || geometry.rows < 1 ||
        geometry.rows > vt::MaxRows)
        geometry = hal.zoomGeometry[zoom - 1];
    geometry.cols &= ~1u;
    const int visible = std::min<int>(geometry.cols, fitCols);
    const int visibleRowCount = std::min<int>(geometry.rows, fitRows);
    if (visible < 1 || visibleRowCount < 1 ||
        !terminal.setGeometry(geometry.cols, geometry.rows, visible,
                              visibleRowCount, zoom))
        return;
    logicalColumns.store(uint16_t(terminal.cols), std::memory_order_release);
    plotCapture = plotCanvasValid = plotEndCleanupPending = false;
    activeDisplayProfile = profile;
    activeDisplayProfile.viewZoom = uint8_t(zoom);
    // The gutter is only the panel's residual edge strip. A deliberately
    // narrower logical grid leaves ordinary default-paper pixels, not a
    // massive grey gutter occupying the unused portion of the TFT.
    presentationGutter = width - fitCols * 6 * zoom;
    displayFlushIntervalMs = profile.renderFlushHz ? 1000u / profile.renderFlushHz : 0;
    scanCell = mirrorScan = 0;
    activeRow = -1;
    activeRowBank = 255;
    std::fill(plotActiveRows, plotActiveRows + vt::MaxRows, false);
    terminal.compositionDirty = true;
    requestDisplayFlush();
    requestMirrorSnapshot();
}
static void replyDisplayInfo() {
    const vt::DisplayHalDefaults &hal = vt::displayHalDefaults();
    char response[320];
    int length = snprintf(response, sizeof(response),
        "\033P7xtgv-info;1;%s;%s;%s;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u",
        hal.id, xtgvFirmwareTarget(), xtgvFirmwareBuild(),
        unsigned(hal.physicalWidth), unsigned(hal.physicalHeight),
        unsigned(terminal.zoom), unsigned(terminal.cols), unsigned(terminal.rows),
        unsigned(terminal.visibleCols), unsigned(terminal.visibleRows),
        unsigned(terminal.viewport), unsigned(terminal.rowViewport), unsigned(selectedBank));
    if (length < 0 || size_t(length) >= sizeof(response)) return;
    for (uint8_t zoom = 1; zoom <= 4; ++zoom) {
        const vt::Geometry geometry = profiles.geometryFor(selectedBank, zoom);
        const int added = snprintf(response + length, sizeof(response) - length,
                                   ";%u;%u", unsigned(geometry.cols), unsigned(geometry.rows));
        if (added < 0 || size_t(added) >= sizeof(response) - length) return;
        length += added;
    }
    if (size_t(length) + 2 >= sizeof(response)) return;
    response[length++] = '\033';
    response[length++] = '\\';
    terminalReply(nullptr, reinterpret_cast<const uint8_t *>(response), size_t(length));
}
static void applyViewZoom(uint8_t zoom) {
    if (zoom < 1 || zoom > 4) return;
    const vt::DisplayHalDefaults &hal = vt::displayHalDefaults();
    const int width = displayOK ? gfx->width() : hal.physicalWidth;
    const int height = displayOK ? gfx->height() : hal.physicalHeight;
    const int fitCols = vt::evenVisibleCols(width, zoom);
    const int visible = std::min(terminal.cols, fitCols);
    const int visibleRows = std::min(terminal.rows, vt::visibleRows(height, zoom));
    if (!terminal.setView(visible, visibleRows, zoom)) return;
    plotCapture = plotCanvasValid = plotEndCleanupPending = false;
    activeDisplayProfile.viewZoom = zoom;
    presentationGutter = width - fitCols * 6 * zoom;
    scanCell = 0;
    activeRow = -1;
    activeRowBank = 255;
    std::fill(plotActiveRows, plotActiveRows + vt::MaxRows, false);
    requestDisplayFlush();
}

static void requestDisplayFlush() {
#if defined(GFX_BUFFERED_CANVAS) || defined(GFX_DEFERRED_FLUSH)
    displayFlushPending = true;
#endif
}
void terminalDiskActivity(bool active) {
    // Latch even a sub-millisecond filesystem operation long enough for the
    // display core to show it. The SD-owner core never calls Arduino_GFX.
    if (active)
        diskActivityUntil.store(millis() + 75, std::memory_order_release);
}
static void diskActivityIndicatorPoll() {
    uint32_t until = diskActivityUntil.load(std::memory_order_acquire);
    const uint32_t now = millis();
    // Consume an expired pulse exactly once. A concurrent SD-owner update is
    // preserved by compare/exchange and will create its own fresh pulse.
    if (until && int32_t(until - now) <= 0) {
        uint32_t expected = until;
        diskActivityUntil.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
        until = diskActivityUntil.load(std::memory_order_acquire);
    }
    const bool plotOwns = plotOwnsPixels();
    const bool visible = !plotOwns && until && int32_t(until - now) > 0;
    if (visible == diskIndicatorVisible)
        return;
    diskIndicatorVisible = visible;
    diskIndicatorDirty = true;
    if (!visible && !plotOwns) {
        terminal.indicatorDirty = true;
    }
}
static void flushCanvasIfDue() {
#if defined(GFX_BUFFERED_CANVAS) || defined(GFX_DEFERRED_FLUSH)
    const uint32_t now = millis();
    if (displayFlushPending && int32_t(now - displayFlushAt) >= 0) {
        gfx->flush();
        displayFlushPending = false;
        displayFlushAt = now + displayFlushIntervalMs;
    }
#endif
}
static void flushPlotRow() {
    if (!displayOK || !plotScanline || !plotX)
        return;
    const int width = gfx->width(), height = gfx->height();
    const int left = std::max(0, plotPaintLeft);
    const int right = std::min(width, plotPaintRight);
    const int physicalY = plotOriginY + plotY * plotZoom;
    if (left < right)
        for (int dy = 0; dy < plotZoom; ++dy)
            if (physicalY + dy >= 0 && physicalY + dy < height)
                gfx->draw16bitRGBBitmap(left, physicalY + dy, plotScanline + left,
                                         right - left, 1);
    requestDisplayFlush();
}
static void terminalPlot(void *, vt::Terminal::PlotEvent event, uint16_t color) {
    switch (event) {
    case vt::Terminal::PlotConfigZoom:
        plotZoom = std::max(1, std::min(4, int(color)));
        break;
    case vt::Terminal::PlotConfigX:
        plotOriginX = color;
        break;
    case vt::Terminal::PlotConfigY:
        plotOriginY = color;
        break;
    case vt::Terminal::PlotBegin:
        // Only arm the graphics parser. Leave the TFT, gutter, pending text
        // redraws, and PC mirror untouched until actual samples arrive.
        plotCapture = false;
        plotCanvasValid = false;
        std::fill(plotActiveRows, plotActiveRows + vt::MaxRows, false);
        plotEndCleanupPending = false;
        plotX = plotY = 0;
        plotPaintLeft = plotWidth;
        plotPaintRight = 0;
        break;
    case vt::Terminal::PlotSample: {
        if (!plotCapture)
            retireDiskIndicatorForPlot();
        // A CLS or geometry redraw can still be pending when a queued GCLS
        // stream reaches its first pixel. Consume that clear *before* the
        // pixel canvas takes ownership; otherwise renderBatch() would clear
        // the completed image after SGR 0 ends the transaction.
        if (!plotCapture && terminal.compositionDirty && displayOK) {
            gfx->fillScreen(rgb565(terminal.palette.paper[0]));
            terminal.compositionDirty = false;
            terminal.indicatorDirty = true;
            requestDisplayFlush();
        }
        plotCapture = true;
        plotCanvasValid = true;
        rememberPlotActiveRow(activeRow);
        const int physicalX = plotOriginX + plotX * plotZoom;
        if (plotScanline && physicalX < plotWidth && physicalX + plotZoom > 0) {
            const int left = std::max(0, physicalX);
            const int right = std::min(plotWidth, physicalX + plotZoom);
            for (int x = left; x < right; ++x)
                plotScanline[x] = color;
            plotPaintLeft = std::min(plotPaintLeft, left);
            plotPaintRight = std::max(plotPaintRight, right);
        }
        ++plotX;
        break;
    }
    case vt::Terminal::PlotRow:
        flushPlotRow();
        plotX = 0;
        ++plotY;
        plotPaintLeft = plotWidth;
        plotPaintRight = 0;
        break;
    case vt::Terminal::PlotEnd:
        flushPlotRow();
        plotX = 0;
        // Discard terminal cells accumulated while plotting was deliberately
        // suppressing opaque TFT writes. SGR 0 arrives before the benchmark's
        // completion message, so subsequent normal output is still drawn.
        // Without this cleanup the delayed cells erase the completed plot in
        // one full repaint as soon as capture ends.
        std::fill(terminal.dirty, terminal.dirty + vt::MaxCells, false);
        plotCapture = false;
        // The benchmark writes its completion text and CP/M prompt just after
        // SGR 0.  Defer the terminal-plane repaint until that short burst has
        // settled, otherwise an intermediate blank active row and cursor can
        // be flushed permanently over the retained plot.
        rememberPlotActiveRow(terminal.y - 1);
        rememberPlotActiveRow(terminal.y);
        rememberPlotActiveRow(activeRow);
        plotEndCleanupPending = true;
        plotEndActivityAt = millis();
        break;
    case vt::Terminal::PlotAbort:
        plotX = plotY = 0;
        plotPaintLeft = plotWidth;
        plotPaintRight = 0;
        plotCapture = false;
        plotCanvasValid = false;
        std::fill(plotActiveRows, plotActiveRows + vt::MaxRows, false);
        plotEndCleanupPending = false;
        break;
    case vt::Terminal::PlotScroll:
        if (plotCanvasValid && !plotCapture) {
            // Scrolling already dirtied the terminal cells which replace the
            // plot. Retire it and redraw only the gutter, not the full panel.
            plotCanvasValid = false;
            plotEndCleanupPending = false;
            std::fill(plotActiveRows, plotActiveRows + vt::MaxRows, false);
            terminal.indicatorDirty = true;
        }
        break;
    }
}

static vt::RGB profileRGB(const uint8_t rgb[3]) { return {rgb[0], rgb[1], rgb[2]}; }
static void dirtyRow(int row) {
    if (row < 0 || row >= terminal.rows)
        return;
    std::fill(terminal.dirty + terminal.index(0, row),
              terminal.dirty + terminal.index(0, row) + terminal.cols, true);
    terminal.mirrorDirty[row] = true;
}
static void rememberPlotActiveRow(int row) {
    if (row >= 0 && row < terminal.rows)
        plotActiveRows[row] = true;
}
static void updateActiveRow() {
    const uint8_t bank = history ? history->activeBank() : 0;
    const int next = profiles.get(bank).activeLine ? terminal.y : -1;
    if (plotCapture || plotEndCleanupPending) {
        rememberPlotActiveRow(activeRow);
        rememberPlotActiveRow(next);
    }
    if (next == activeRow && bank == activeRowBank)
        return;
    dirtyRow(activeRow);
    activeRow = next;
    activeRowBank = bank;
    dirtyRow(activeRow);
}
static void retirePlotActiveRows() {
    // Terminal::feed advances the logical cursor once, on the first plot-row
    // LF.  The row immediately above it therefore contains any status text
    // printed before the first xterm sample (BROT's "Generating ..." line).
    // Include both explicitly: output can outrun updateActiveRow() in the same
    // runtime batch, especially when the command came from SUBMIT.
    rememberPlotActiveRow(terminal.y - 1);
    rememberPlotActiveRow(terminal.y);
    rememberPlotActiveRow(activeRow);
    bool rows[vt::MaxRows];
    memcpy(rows, plotActiveRows, sizeof(rows));
    std::fill(plotActiveRows, plotActiveRows + vt::MaxRows, false);
    activeRow = -1;
    activeRowBank = 255;
    for (int row = 0; row < terminal.rows; ++row)
        if (rows[row])
            dirtyRow(row);
}
static void plotEndCleanupPoll(uint32_t now) {
    if (!plotEndCleanupPending || output.available() ||
        uint32_t(now - plotEndActivityAt) < 100)
        return;
    plotEndCleanupPending = false;
    retirePlotActiveRows();
}

static uint16_t rgb565(vt::RGB c) { return ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3); }
// DEC special graphics masks. The original QL font banks remain byte-identical.
static uint8_t graphicsRow(uint8_t c, int y) {
    const uint8_t full = 0xfc, center = 0x20;
    switch (c) {
    case '_':
        return 0;
    case 'j':
        return y < 4 ? center : y == 4 ? 0xe0 : 0; // lower right
    case 'k':
        return y > 4 ? center : y == 4 ? 0xe0 : 0; // upper right
    case 'l':
        return y > 4 ? center : y == 4 ? 0x3c : 0;
    case 'm':
        return y < 4 ? center : y == 4 ? 0x3c : 0;
    case 'n':
        return y == 4 ? full : center;
    case 'q':
        return y == 4 ? full : 0;
    case 't':
        return y == 4 ? 0x3c : center;
    case 'u':
        return y == 4 ? 0xe0 : center;
    case 'v':
        return y < 4 ? center : y == 4 ? full : 0;
    case 'w':
        return y > 4 ? center : y == 4 ? full : 0;
    case 'x':
        return center;
    case 'o':
        return y == 0 ? full : 0;
    case 'p':
        return y == 2 ? full : 0;
    case 'r':
        return y == 6 ? full : 0;
    case 's':
        return y == 8 ? full : 0;
    case '`': {
        const uint8_t diamond[10] = {0, 0, 0x20, 0x70, 0xf8, 0x70, 0x20, 0, 0, 0};
        return diamond[y];
    }
    case 'a':
        return y & 1 ? 0xa8 : 0x54;
    case 'f':
        return y == 1 || y == 3 ? 0x60 : y == 2 ? 0x90 : 0;
    case 'g':
        return y == 3 ? 0x20 : y == 4 ? 0x70 : y == 5 ? 0x20 : y == 7 ? 0x70 : 0;
    case '~':
        return y == 4 || y == 5 ? 0x60 : 0;
    case 'y': {
        const uint8_t le[10] = {0, 0x10, 0x20, 0x40, 0x20, 0x10, 0, 0x70, 0, 0};
        return le[y];
    }
    case 'z': {
        const uint8_t ge[10] = {0, 0x40, 0x20, 0x10, 0x20, 0x40, 0, 0x70, 0, 0};
        return ge[y];
    }
    case '|':
        return (y == 3 || y == 6 ? 0xf8 : 0) | (y > 0 && y < 9 ? uint8_t(0x08 << (y / 2)) : 0);
    case '{':
        return font1bpp[0]['p'][y]; // pi approximation at 6x10
    case '}':
        return font1bpp[0]['L'][y]; // pound approximation
    default:
        return font1bpp[0]['?'][y];
    }
}
static void drawCell(int i) {
    int sourceColumn = i % vt::MaxCols, row = i / vt::MaxCols;
    int column = sourceColumn - terminal.viewport;
    const int displayRow = row - terminal.rowViewport;
    uint8_t bank = (terminal.attrRAM[i] >> 6) & 3, ch = terminal.charRAM[i];
    vt::RGB foreground = terminal.foreground(i), background = terminal.background(i);
    const vt::HistoryProfile &profile = profiles.get(history ? history->activeBank() : 0);
    if (profile.activeLine && row == activeRow)
        background = profile.activeLineOverride ? profileRGB(profile.activeLineRGB) : terminal.palette.activeLine;
    bool cursor =
        terminal.cursorVisible && cursorPhase && terminal.x == sourceColumn && terminal.y == row;
    if (cursor) {
        background = profile.cursorOverride ? profileRGB(profile.cursorRGB) : terminal.palette.cursor;
        // The cursor is dark paper, but preserve the active-line colour as
        // its ink so a character under the blinking block stays readable.
        foreground = profile.activeLine ? (profile.activeLineOverride ? profileRGB(profile.activeLineRGB)
                                              : terminal.palette.activeLine) : terminal.foreground(i);
    }
    uint16_t fg = rgb565(foreground), bg = rgb565(background);
    bool blinkOff = (terminal.flags[i] & vt::Blink) && !cursorPhase;
    for (int y = 0; y < 10; y++) {
        uint8_t inkBits = (terminal.flags[i] & vt::Graphics)
                              ? graphicsRow(ch, y)
                              : font1bpp[bank][ch < 128 ? ch : '?'][y];
        if ((terminal.flags[i] & vt::Underline) && y == 8)
            inkBits = 0xfc;
        for (int x = 0; x < 6; x++) {
            uint8_t mask = 0x80 >> x;
            uint16_t color = bg;
            if (!blinkOff && (inkBits & mask))
                color = fg;
            pixels[y * 6 + x] = color;
        }
    }
    const int scale = terminal.zoom;
    const int width = 6 * scale, height = 10 * scale;
    for (int sy = 0; sy < 10; ++sy)
        for (int sx = 0; sx < 6; ++sx) {
            const uint16_t color = pixels[sy * 6 + sx];
            for (int yy = 0; yy < scale; ++yy)
                for (int xx = 0; xx < scale; ++xx)
                    scaledPixels[(sy * scale + yy) * width + sx * scale + xx] = color;
        }
    // Keep the gutter away from the panel frame: at the default left edge it
    // precedes the text, while at the right edge it follows the text.
    const int origin = terminal.viewport ? 0 : presentationGutter;
    gfx->draw16bitRGBBitmap(origin + column * width + GFX_OFS_X,
                             displayRow * height + GFX_OFS_Y,
                             scaledPixels, width, height);
}
static void writeHistoryNotice() {
    int first = terminal.index(0, historyNoticeRow);
    for (int col = 0; col < terminal.cols; ++col) {
        terminal.paperRAM[first + col] = ' ';
        terminal.charRAM[first + col] = ' ';
        terminal.attrRAM[first + col] = 8;
        terminal.flags[first + col] = 0;
        terminal.dirty[first + col] = true;
    }
    char text[vt::HistoryProfile::MaxName + 16];
    if (historyNoticeRaw)
        snprintf(text, sizeof(text), "[## RAW / #%u]", unsigned(historyNoticeBank));
    else
        snprintf(text, sizeof(text), "[#%u %s]", unsigned(historyNoticeBank),
                 profiles.get(historyNoticeBank).name);
    for (int col = 0; text[col] && col < terminal.cols; ++col)
        terminal.charRAM[first + col] = uint8_t(text[col]);
    terminal.mirrorDirty[historyNoticeRow] = true;
}
static void restoreHistoryNotice() {
    if (!historyNoticeActive)
        return;
    int first = terminal.index(0, historyNoticeRow);
    memcpy(terminal.paperRAM + first, historyNoticePaper, terminal.cols);
    memcpy(terminal.charRAM + first, historyNoticeChars, terminal.cols);
    memcpy(terminal.attrRAM + first, historyNoticeAttrs, terminal.cols);
    memcpy(terminal.flags + first, historyNoticeFlags, terminal.cols);
    std::fill(terminal.dirty + first, terminal.dirty + first + terminal.cols, true);
    terminal.mirrorDirty[historyNoticeRow] = true;
    terminal.mirrorCursorDirty = true;
    historyNoticeActive = false;
    historyNoticeSendOnSelect = false;
}
static void showHistoryBank(uint8_t bank, bool peek) {
    // Repeated Alt+Space refreshes the same overlay from its original row.
    // A profile change first restores the previous label and cursor row.
    restoreHistoryNotice();
    historyNoticeBank = bank;
    historyNoticeRow = uint8_t(terminal.y);
    // Never inject on_select into an application. A painted but stale CCP
    // prompt is insufficient; the prompt must still be accepting input.
    historyNoticeSendOnSelect = !peek && profiles.get(bank).selectLength &&
                                ccpPromptLive && cpmPromptEndColumn() >= 0;
    historyNoticeRaw = rawInputMode;
    int first = terminal.index(0, historyNoticeRow);
    memcpy(historyNoticePaper, terminal.paperRAM + first, terminal.cols);
    memcpy(historyNoticeChars, terminal.charRAM + first, terminal.cols);
    memcpy(historyNoticeAttrs, terminal.attrRAM + first, terminal.cols);
    memcpy(historyNoticeFlags, terminal.flags + first, terminal.cols);
    historyNoticeUntil = millis() + 1000;
    historyNoticeActive = true;
    writeHistoryNotice();
}
static void setRawInputMode(bool enabled) {
    // The host enters RAW before VTPUT/VTGET. Queue sync on the storage
    // owner; never touch SD from this display/input core. Direct INI access
    // additionally forces a synchronous save in the filesystem abstraction.
    if (enabled) {
        terminalConfigForceSave();
        terminalHistoryForceSave();
    }
    rawControlLength = 0;
    rawInputMode = enabled;
    if (history)
        showHistoryBank(selectedBank, true);
}
static void historyNoticePoll() {
    if (!historyNoticeActive)
        return;
    if (int32_t(millis() - historyNoticeUntil) >= 0) {
        const bool sendOnSelect = historyNoticeSendOnSelect;
        restoreHistoryNotice();
        if (!sendOnSelect)
            return;
        const vt::HistoryProfile &profile = profiles.get(historyNoticeBank);
        for (uint8_t i = 0; i < profile.selectLength; ++i) {
            input.push(profile.selectBytes[i]);
            if (profile.selectBytes[i] == 13)
                ccpPromptLive = false;
        }
        return;
    }
    int first = terminal.index(0, historyNoticeRow);
    bool intact = terminal.charRAM[first] == '[' && terminal.charRAM[first + 1] == '#' &&
                  terminal.charRAM[first + 2] ==
                      uint8_t(historyNoticeRaw ? '#' : '0' + historyNoticeBank);
    if (!intact)
        writeHistoryNotice();
}
static void drawIndicator() {
    // A GCLS/xterm pixel canvas owns the entire physical surface. The gutter
    // is a terminal viewport cue and must not cover its leftmost samples.
    if (!displayOK || plotOwnsPixels() || !presentationGutter)
        return;
    const int x = terminal.viewport ? GFX_OFS_X + terminal.visibleCols * 6 * terminal.zoom
                                    : GFX_OFS_X;
    const vt::GlobalProfile &global = profiles.global();
    const vt::HistoryProfile &profile = profiles.get(history ? history->activeBank() : 0);
    const bool followsLine = global.activeGutterOverride ? global.viewportActiveGutterFollowsLine
                                                         : terminal.palette.activeGutterFollowsLine;
    const vt::RGB color = terminal.viewportLimit()
        ? (followsLine ? (profile.activeLineOverride ? profileRGB(profile.activeLineRGB) : terminal.palette.activeLine)
                       : (global.activeGutterOverride ? profileRGB(global.viewportActiveGutterRGB) : terminal.palette.activeGutter))
        : (global.gutterOverride ? profileRGB(global.viewportGutterRGB) : terminal.palette.gutter);
    gfx->fillRect(x, GFX_OFS_Y, presentationGutter, gfx->height(), rgb565(color));
}
static void restoreDiskIndicatorBacking() {
    // Restore all cells touched by the fixed-pixel 6x6 overlay, including
    // row/column boundaries at any zoom. Its HAL offset is not a text row.
    const int y = 1 + vt::displayHalDefaults().diskIndicatorYOffset;
    const int origin = terminal.viewport ? 0 : presentationGutter;
    gfx->fillRect(GFX_OFS_X + 1, GFX_OFS_Y + y, 6, 6,
                  rgb565(terminal.palette.paper[0]));
    if (presentationGutter && !terminal.viewport)
        drawIndicator();
    const int width = 6 * terminal.zoom, height = 10 * terminal.zoom;
    const int left = std::max(0, 1 - origin);
    const int right = std::min(7 - origin, terminal.visibleCols * width);
    for (int displayRow = y / height;
         displayRow <= (y + 5) / height && displayRow < terminal.visibleRows;
         ++displayRow)
        for (int col = left / width; left < right && col <= (right - 1) / width; ++col) {
            const int covered = terminal.index(terminal.viewport + col,
                                               terminal.rowViewport + displayRow);
            drawCell(covered);
            terminal.dirty[covered] = false;
        }
}
static void retireDiskIndicatorForPlot() {
    if (!diskIndicatorVisible)
        return;
    // The first sample starts a retained image. Remove any active SD pulse
    // now, while the ordinary terminal/gutter backing is still available.
    diskIndicatorVisible = false;
    diskIndicatorDirty = false;
    if (!displayOK)
        return;
    restoreDiskIndicatorBacking();
    requestDisplayFlush();
}
static void renderBatch() {
    if (!displayOK)
        return;
    // Plot samples paint directly into the graphics surface. Keep terminal
    // cells out until SGR 0 ends that transaction, but still flush a buffered
    // canvas so the physical Mandelbrot image becomes visible.
    if (plotCapture || plotEndCleanupPending) {
        flushCanvasIfDue();
        return;
    }
    if (terminal.compositionDirty) {
        // A full zoom/pan redraw replaces the retained xterm-256 image.
        // Retire its ownership before repainting the gutter and disk LED.
        plotCanvasValid = false;
        gfx->fillScreen(rgb565(terminal.palette.paper[0]));
        terminal.compositionDirty = false;
        terminal.indicatorDirty = true;
        requestDisplayFlush();
    }
    int drawn = 0, pixelsDrawn = 0;
#if defined(GFX_BUFFERED_CANVAS)
    const int DrawBudgetPixels = terminal.visibleCols * terminal.visibleRows *
                                 6 * 10 * terminal.zoom * terminal.zoom;
#else
    constexpr int DrawBudgetPixels = 12 * 6 * 10;
#endif
    const int visibleCells = terminal.visibleCols * terminal.visibleRows;
    const int cellPixels = 6 * 10 * terminal.zoom * terminal.zoom;
    for (int checked = 0; checked < visibleCells && pixelsDrawn < DrawBudgetPixels; checked++) {
        int view = scanCell++ % visibleCells;
        int i = terminal.index(view % terminal.visibleCols + terminal.viewport,
                               view / terminal.visibleCols + terminal.rowViewport);
        if (terminal.dirty[i]) {
            drawCell(i);
            terminal.dirty[i] = false;
            drawn++;
            pixelsDrawn += cellPixels;
        }
    }
    // Keep the scan index bounded even after months of continuous operation.
    scanCell %= visibleCells;
    if (terminal.indicatorDirty) {
        drawIndicator();
        terminal.indicatorDirty = false;
    }
    if (!diskIndicatorVisible && diskIndicatorDirty && !plotOwnsPixels()) {
        restoreDiskIndicatorBacking();
        requestDisplayFlush();
    }
    // Compact displays have no taskbar, so overlay one plain, bright 6x6
    // HAL-positioned activity LED. Its expiry restores the underlying gutter
    // or text cell immediately; retained graphics plots are never touched.
    if (diskIndicatorVisible && (diskIndicatorDirty || drawn)) {
        gfx->fillRect(GFX_OFS_X + 1,
                      GFX_OFS_Y + 1 + vt::displayHalDefaults().diskIndicatorYOffset,
                      6, 6, 0xf800);
        requestDisplayFlush();
    }
    diskIndicatorDirty = false;
#if defined(GFX_BUFFERED_CANVAS) || defined(GFX_DEFERRED_FLUSH)
    displayFlushPending |= drawn != 0;
    flushCanvasIfDue();
#endif
}
static const char *graphicText(uint8_t ch) {
    switch (ch) {
    case '_':
        return " ";
    case 'j':
        return "┘";
    case 'k':
        return "┐";
    case 'l':
        return "┌";
    case 'm':
        return "└";
    case 'n':
        return "┼";
    case 'q':
        return "─";
    case 't':
        return "├";
    case 'u':
        return "┤";
    case 'v':
        return "┴";
    case 'w':
        return "┬";
    case 'x':
        return "│";
    case 'o':
        return "⎺";
    case 'p':
        return "⎻";
    case 'r':
        return "⎼";
    case 's':
        return "⎽";
    case '`':
        return "◆";
    case 'a':
        return "▒";
    case 'f':
        return "°";
    case 'g':
        return "±";
    case 'y':
        return "≤";
    case 'z':
        return "≥";
    case '{':
        return "π";
    case '|':
        return "≠";
    case '}':
        return "£";
    case '~':
        return "·";
    default:
        return "?";
    }
}
static void mirrorBuild() {
    mirrorLength = mirrorSent = 0;
    if (mirrorReset) {
        const char *reset = "\033[0m\033[?7l\033[2J\033[H";
        strcpy(mirrorBuffer, reset);
        mirrorLength = strlen(reset);
        mirrorReset = false;
        return;
    }
    int row = -1;
    for (int j = 0; j < terminal.rows; j++) {
        int r = (mirrorScan + j) % terminal.rows;
        if (terminal.mirrorDirty[r]) {
            row = r;
            mirrorScan = (r + 1) % terminal.rows;
            break;
        }
    }
    if (row >= 0) {
        mirrorLength = snprintf(mirrorBuffer, sizeof(mirrorBuffer), "\033[?25l\033[%d;1H", row + 1);
        int oldAttr = -1, oldFlags = -1;
        for (int col = 0; col < terminal.cols; col++) {
            int i = terminal.index(col, row);
            if (oldAttr != terminal.attrRAM[i] || oldFlags != terminal.flags[i]) {
                vt::RGB f = terminal.foreground(i), b = terminal.background(i);
                const vt::HistoryProfile &profile = profiles.get(history ? history->activeBank() : 0);
                if (profile.activeLine && row == activeRow)
                    b = profile.activeLineOverride ? profileRGB(profile.activeLineRGB) : terminal.palette.activeLine;
                mirrorLength += snprintf(
                    mirrorBuffer + mirrorLength, sizeof(mirrorBuffer) - mirrorLength,
                    "\033[0;%d;38;2;%u;%u;%u;48;2;%u;%u;%um",
                    (terminal.flags[i] & vt::Underline) ? 4 : 24, f.r, f.g, f.b, b.r, b.g, b.b);
                oldAttr = terminal.attrRAM[i];
                oldFlags = terminal.flags[i];
            }
            if (terminal.graphics(i)) {
                const char *s = graphicText(terminal.charRAM[i]);
                size_t n = strlen(s);
                memcpy(mirrorBuffer + mirrorLength, s, n);
                mirrorLength += n;
            } else
                mirrorBuffer[mirrorLength++] =
                    terminal.charRAM[i] >= 32 && terminal.charRAM[i] < 127 ? terminal.charRAM[i]
                                                                           : '?';
        }
        terminal.mirrorDirty[row] = false;
    }
    if (row >= 0 || terminal.mirrorCursorDirty) {
        mirrorLength += snprintf(mirrorBuffer + mirrorLength, sizeof(mirrorBuffer) - mirrorLength,
                                 "\033[%d;%dH\033[?25%c", terminal.y + 1, terminal.x + 1,
                                 terminal.cursorVisible ? 'h' : 'l');
        terminal.mirrorCursorDirty = false;
    }
}
static void mirrorPump() {
    bool now = bool(Serial);
    if (now != connected) {
        connected = now;
        mirrorLength = mirrorSent = 0;
        if (now) {
            // A USB CDC/DTR reconnect needs a complete PC mirror snapshot.
            // With a retained xterm plot, invalidate only its mirror rows:
            // sending the reset without making those rows dirty would leave
            // the PC permanently blank after a transient DTR drop.
            if (!plotCanvasValid)
                terminal.invalidate();
            else
                terminal.invalidateMirror();
            mirrorReset = true;
        }
    }
    if (!connected)
        return;
    // Reply to the existing safe snapshot query (>62v), including in RAW.
    // Old firmware consumes that query harmlessly but has no identity reply.
    // Host-only DCS framing never enters CP/M or the physical framebuffer.
    if (hostIdentityLength && mirrorSent == mirrorLength && !hostReflashSent) {
        const int available = Serial.availableForWrite();
        if (available > 0) {
            const size_t count = std::min(hostIdentityLength - hostIdentitySent,
                                         std::min(size_t(available), size_t(64)));
            hostIdentitySent += Serial.write(
                reinterpret_cast<const uint8_t *>(hostIdentity + hostIdentitySent), count);
            if (hostIdentitySent == hostIdentityLength)
                hostIdentityLength = hostIdentitySent = 0;
        }
        return;
    }
    // Never inject into a partially written reconstructed ANSI update.  The
    // PC bridge receives this exact private sequence as an out-of-band event
    // and consumes it before displaying anything.
    if (hostReflashPending && mirrorSent == mirrorLength) {
        int available = Serial.availableForWrite();
        if (available > 0) {
            size_t left = sizeof(hostReflashEvent) - hostReflashSent;
            size_t n = std::min(left, std::min(size_t(available), size_t(64)));
            n = Serial.write(hostReflashEvent + hostReflashSent, n);
            hostReflashSent += n;
            if (hostReflashSent == sizeof(hostReflashEvent)) {
                hostReflashPending = false;
                hostReflashSent = 0;
            }
        }
        return;
    }
    if (mirrorSent == mirrorLength)
        mirrorBuild();
    int available = Serial.availableForWrite();
    if (available > 0 && mirrorSent < mirrorLength) {
        size_t n = std::min(size_t(available), std::min(size_t(64), mirrorLength - mirrorSent));
        mirrorSent += Serial.write(reinterpret_cast<const uint8_t *>(mirrorBuffer + mirrorSent), n);
    }
}
static void finishHistoryLoadLine() {
    if (!historyLoadLength)
        return;
    if (historyLoadLength == 3 && historyLoadText[0] == '[' &&
        historyLoadText[1] >= '0' && historyLoadText[1] <= '9' && historyLoadText[2] == ']')
        historyLoadBank = uint8_t(historyLoadText[1] - '0');
    else if (historyLoadText[0] == '>' && historyLoadLength > 1)
        history->appendEntry(historyLoadBank, historyLoadText + 1, historyLoadLength - 1);
    historyLoadLength = 0;
}
void terminalHistoryLoadBegin() {
    historyLoadBank = historyLoadLength = 0;
    historySavePending = false;
}
void terminalHistoryLoadFeed(uint8_t byte) {
    if (!history)
        return;
    if (byte == '\r')
        return;
    if (byte == '\n') {
        finishHistoryLoadLine();
        return;
    }
    if (historyLoadLength < vt::LineHistory::MaxLine)
        historyLoadText[historyLoadLength++] = char(byte);
}
void terminalHistoryLoadEnd() {
    if (!history)
        return;
    finishHistoryLoadLine();
    historyObservedGeneration = history->generation();
    historySavePending = false;
}
static bool writeHistoryText(TerminalHistoryWriteByte writeByte, void *context,
                             const char *text) {
    while (*text)
        if (!writeByte(context, uint8_t(*text++)))
            return false;
    return true;
}
struct HistoryWriteState {
    TerminalHistoryWriteByte writeByte;
    void *context;
    uint32_t crc = 0xffffffffUL;
};
static uint32_t historyCRC32Byte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320UL & uint32_t(-int32_t(crc & 1)));
    return crc;
}
static bool writeHistoryCheckedByte(void *context, uint8_t byte) {
    HistoryWriteState *state = static_cast<HistoryWriteState *>(context);
    if (!state->writeByte(state->context, byte))
        return false;
    state->crc = historyCRC32Byte(state->crc, byte);
    return true;
}
bool terminalHistoryWrite(TerminalHistoryWriteByte writeByte, void *context) {
    HistoryWriteState state{writeByte, context};
    if (!history || !writeHistoryText(writeHistoryCheckedByte, &state,
                                      "# 7xtgv terminal history v2\n"))
        return false;
    char text[vt::LineHistory::MaxLine];
    for (uint8_t bank = 0; bank < vt::LineHistory::Banks; ++bank) {
        char header[8];
        snprintf(header, sizeof(header), "[%u]\n", unsigned(bank));
        if (!writeHistoryText(writeHistoryCheckedByte, &state, header))
            return false;
        for (uint8_t ordinal = 0; ordinal < history->bankCount(bank); ++ordinal) {
            uint8_t length = 0;
            if (!history->copyEntry(bank, ordinal, text, length) ||
                !writeHistoryCheckedByte(&state, '>'))
                return false;
            for (uint8_t i = 0; i < length; ++i)
                if (!writeHistoryCheckedByte(&state, uint8_t(text[i])))
                    return false;
            if (!writeHistoryCheckedByte(&state, '\n'))
                return false;
        }
    }
    char footer[24];
    snprintf(footer, sizeof(footer), "# crc32=%08lX\n",
             static_cast<unsigned long>(state.crc ^ 0xffffffffUL));
    // The checksum footer is deliberately excluded from its own CRC.
    return writeHistoryText(writeByte, context, footer);
}
bool terminalHistorySaveDue() {
    if (!history)
        return false;
    const uint32_t generation = history->generation();
    const bool forced = historyForceSaveRequested.exchange(false, std::memory_order_acq_rel);
    if (generation != historyObservedGeneration) {
        historyObservedGeneration = generation;
        historyChangedAt = millis();
        historySavePending = true;
    }
    if (forced) {
        historyChangedAt = millis();
        historySavePending = true;
    }
    return historySavePending &&
           (forced || uint32_t(millis() - historyChangedAt) >= 2000);
}
void terminalHistoryForceSave() {
    historyForceSaveRequested.store(true, std::memory_order_release);
}
void terminalHistorySaveComplete(bool saved) {
    if (saved)
        historySavePending = false;
    else
        historyChangedAt = millis(); // retry later without a busy SD loop
}

void terminalSetup() {
    // Do this before allocation and display initialization, not after begin().
    xtgvBacklightInit(true);
#if defined(ESP32)
    // Ten 255-entry banks are larger than internal SRAM can safely reserve
    // alongside the RGB scanout task. Keep the in-memory history in PSRAM;
    // the SD file is still only loaded at boot and saved lazily.
    if (void *memory = ps_malloc(sizeof(vt::LineHistory)))
        history = new (memory) vt::LineHistory;
#else
    history = new (std::nothrow) vt::LineHistory;
#endif
    Serial.begin(115200);
    // No USB diagnostics: this port carries only the interpreted screen mirror.
    GFX_DEVICE_INIT();
    displayOK = GFX_BEGIN_OK();
    if (displayOK) {
        gfx->fillScreen(0);
#if defined(GFX_BUFFERED_CANVAS) || defined(GFX_DEFERRED_FLUSH)
        gfx->flush();
        displayFlushAt = millis() + 33;
#endif
        plotWidth = gfx->width();
        if (plotWidth > 0)
            plotScanline = new (std::nothrow) uint16_t[plotWidth];
    }
    terminal.setPlotSink(terminalPlot, nullptr);
    terminal.setControlSink(terminalControl, nullptr);
    terminal.reset();
    // Compiled policy applies before RunCPM mounts the SD card.  A matching
    // shared-card display section is queued later by terminalConfigEnd().
    applyDisplayProfile(profiles.display());
    bootDisplayProfile = profiles.display();
    bootGeometry = profiles.geometryFor(0, bootDisplayProfile.viewZoom);
    configSaveBacklight.store(xtgvBacklightGetRemembered(), std::memory_order_release);
    configSaveZoom.store(activeDisplayProfile.viewZoom, std::memory_order_release);
    if (!displayOK) {
        const char *s = "TFT initialization failed; PC mirror remains available.\r\n";
        while (*s)
            terminal.feed(*s++);
    }
    ready.store(true, std::memory_order_release);
}
void terminalConfigBegin() { profiles.beginLoad(vt::displayHalDefaults()); }
void terminalConfigFeed(uint8_t byte) { profiles.feed(byte); }
unsigned terminalConfigEnd() {
    profiles.finishLoad();
    bootDisplayProfile = profiles.display();
    bootGeometry = profiles.geometryFor(0, bootDisplayProfile.viewZoom);
    terminal.setMode(profiles.global().mode == vt::OutputAnsi);
    pendingDisplayProfile = profiles.display();
    xtgvBacklightSet(profiles.global().backlightLevel);
    configSaveBacklight.store(xtgvBacklightGetRemembered(), std::memory_order_release);
    configSaveZoom.store(profiles.display().viewZoom, std::memory_order_release);
    configSavePalette.store(profiles.global().modePalette, std::memory_order_release);
    configPalettePending.store(true, std::memory_order_release);
    displayProfileApplied.store(false, std::memory_order_release);
    displayProfilePending.store(true, std::memory_order_release);
    return profiles.errors();
}
uint8_t terminalConfiguredConsoleBits() { return profiles.global().consoleBits; }
unsigned terminalConfigErrorCount() { return profiles.errors(); }
uint8_t terminalConfigErrorLineCount() { return profiles.errorLineCount(); }
bool terminalConfigErrorLine(uint8_t index, uint16_t &number, const char *&section, const char *&text) {
    if (index >= profiles.errorLineCount())
        return false;
    const vt::HistoryProfiles::ErrorLine &entry = profiles.errorLine(index);
    number = entry.number;
    section = entry.section;
    text = entry.text;
    return true;
}
bool terminalConfigUsesAnsi() { return profiles.global().mode == vt::OutputAnsi; }
void terminalConfigForceSave() {
    if (configSavePending.load(std::memory_order_acquire))
        configForceSaveRequested.store(true, std::memory_order_release);
}
bool terminalConfigSaveDue(uint8_t &backlightLevel, uint8_t &zoomLevel,
                           uint8_t *paletteIndex, uint8_t *dirtyFields) {
    if (!configSavePending.load(std::memory_order_acquire) ||
        !configSaveDirty.load(std::memory_order_acquire) ||
        (!configForceSaveRequested.load(std::memory_order_acquire) &&
         uint32_t(millis() - configChangedAt.load(std::memory_order_acquire)) < 1500))
        return false;
    backlightLevel = configSaveBacklight.load(std::memory_order_acquire);
    zoomLevel = configSaveZoom.load(std::memory_order_acquire);
    if (paletteIndex) *paletteIndex = configSavePalette.load(std::memory_order_acquire);
    if (dirtyFields) *dirtyFields = configSaveDirty.load(std::memory_order_acquire);
    return true;
}
void terminalConfigSaveComplete(bool saved) {
    configForceSaveRequested.store(false, std::memory_order_release);
    if (saved) {
        configSaveDirty.store(0, std::memory_order_release);
        configSavePending.store(false, std::memory_order_release);
    } else
        configChangedAt.store(millis(), std::memory_order_release);
}
void terminalWaitDisplayProfileApplied() {
    while (!displayProfileApplied.load(std::memory_order_acquire))
        delay(1);
}
uint16_t terminalLogicalColumns() {
    return logicalColumns.load(std::memory_order_acquire);
}
void terminalWaitReady() {
    while (!ready.load(std::memory_order_acquire))
        delay(1);
}
void terminalBootConfigComplete() {
    bootConfigComplete.store(true, std::memory_order_release);
}
static void terminalBootBacklightPoll() {
    if (!xtgvBacklightHeldOff() || !displayOK ||
        !bootConfigComplete.load(std::memory_order_acquire) ||
        displayProfilePending.load(std::memory_order_acquire))
        return;
    if (!bootFrameReady) {
        if (terminal.compositionDirty || terminal.indicatorDirty)
            return;
        // Rendering is budgeted: applying the palette is not proof that every
        // visible cell has actually been repainted yet.
        for (int y = 0; y < terminal.visibleRows; ++y)
            for (int x = 0; x < terminal.visibleCols; ++x)
                if (terminal.dirty[terminal.index(x + terminal.viewport,
                                                  y + terminal.rowViewport)])
                    return;
#if defined(GFX_BUFFERED_CANVAS) || defined(GFX_DEFERRED_FLUSH)
        gfx->flush();
        displayFlushPending = false;
        displayFlushAt = millis() + displayFlushIntervalMs;
#endif
        bootFrameReady = true;
        bootFrameFlushedAt = millis();
        return;
    }
    // Let the W550 RGB panel scan the flushed frame before illuminating it.
    // Non-blocking, one-time guard; ordinary rendering/USB continue normally.
    if (uint32_t(millis() - bootFrameFlushedAt) >= 33)
        xtgvBacklightRelease(profiles.global().crtRampupMs);
}
void terminalPause(bool paused) {
    pauseRequested.store(paused, std::memory_order_release);
    while (pauseAcknowledged.load(std::memory_order_acquire) != paused)
        delay(1);
}
void terminalScheduleReboot() {
    // Store millis()+1 so zero remains the "not scheduled" sentinel.
    rebootScheduledAt.store(millis() + 1, std::memory_order_release);
}
static void terminalRebootPoll() {
    const uint32_t scheduled = rebootScheduledAt.load(std::memory_order_acquire);
    if (!scheduled || uint32_t((millis() + 1) - scheduled) < 3000)
        return;
    rebootScheduledAt.store(0, std::memory_order_release);
    runCPMHardwareReboot();
}
void terminalTextBenchmarkRequest() {
    textBenchmarkFinished.store(false, std::memory_order_release);
    textBenchmarkRequested.store(true, std::memory_order_release);
}
bool terminalTextBenchmarkDone() {
    return textBenchmarkFinished.load(std::memory_order_acquire);
}
uint32_t terminalTextBenchmarkElapsedUs() {
    return textBenchmarkElapsed.load(std::memory_order_acquire);
}
uint32_t terminalTextBenchmarkBytes() {
    return textBenchmarkByteCount.load(std::memory_order_acquire);
}
uint32_t terminalTextBenchmarkChars() {
    return textBenchmarkCharCount.load(std::memory_order_acquire);
}
static void terminalTextBenchmarkPoll() {
    static bool active = false;
    static uint32_t started = 0, bytes = 0, chars = 0, seed = 0;
    if (!active && textBenchmarkRequested.exchange(false, std::memory_order_acq_rel)) {
        terminal.reset();
        started = micros();
        bytes = chars = 0;
        seed = 0x7A17C0DE;
        active = true;
    }
    if (!active)
        return;
    static const char *colors[] = {"31", "32", "33", "34", "35", "36", "91", "94"};
    static const char *fixtures[] = {
        "7xtgv RED", "7xtgv GREEN", "7xtgv YELLOW", "7xtgv BLUE",
        "7xtgv MAGENTA", "7xtgv CYAN", "7xtgv BRIGHT RED", "7xtgv BRIGHT BLUE"
    };
    const uint32_t rows = 29, items = 96;
    // One frame per terminal loop lets the normal renderer show the redraws.
    terminal.feed(0x1b); terminal.feed('['); terminal.feed('2'); terminal.feed('J');
    for (uint32_t i = 0; i < items; ++i) {
        seed = seed * 1664525UL + 1013904223UL;
        const char *fixture = fixtures[(seed >> 24) & 7];
        uint32_t row = 1 + ((seed >> 16) % rows);
        uint32_t col = 1 + ((seed >> 8) % (80 - strlen(fixture)));
        const char *color = colors[(seed >> 28) & 7];
        auto emit = [&](uint8_t c) { terminal.feed(c); ++bytes; };
        emit(0x1b); emit('['); emit(color[0]); emit(color[1]); emit('m');
        emit(0x1b); emit('[');
        if (row >= 10) emit('0' + row / 10);
        emit('0' + row % 10); emit(';');
        if (col >= 10) emit('0' + col / 10);
        emit('0' + col % 10); emit('H');
        for (const char *p = fixture; *p; ++p) { emit(uint8_t(*p)); ++chars; }
        emit(0x1b); emit('['); emit('0'); emit('m');
    }
    uint32_t elapsed = uint32_t(micros() - started);
    if (elapsed >= 3000000UL) {
        // Put the CP/M result on a clean line below the visual benchmark.
        terminal.feed(0x1b); terminal.feed('['); terminal.feed('3'); terminal.feed('1');
        terminal.feed(';'); terminal.feed('1'); terminal.feed('H');
        textBenchmarkByteCount.store(bytes, std::memory_order_relaxed);
        textBenchmarkCharCount.store(chars, std::memory_order_relaxed);
        textBenchmarkElapsed.store(elapsed, std::memory_order_release);
        textBenchmarkFinished.store(true, std::memory_order_release);
        active = false;
    }
}
int virtualAvailable() { return input.available(); }
uint8_t virtualRead() {
    uint8_t c;
    while (!input.pop(c)) {
        // RunCPM and its filesystem live on this core. Lazy history writes
        // must never touch SdFat/SD_MMC from the terminal/display core.
        terminalConfigStoragePoll();
        terminalHistoryStoragePoll();
        // REBOOT.COM returns to the CCP, making this input wait the safe point
        // to reset after the pending history transaction had its opportunity.
        terminalRebootPoll();
        delay(1);
    }
    return c;
}
void virtualWrite(uint8_t c) {
    while (!output.push(c))
        delay(1);
}
static void feedTerminalOutput(uint8_t c) {
    terminal.feed(c);
    // A freshly emitted prompt terminator is evidence of a new CCP input
    // wait. A previously painted launcher row alone is not.
    if (c == '>' && terminal.x == cpmPromptEndColumn() + 1)
        ccpPromptLive = true;
}
static void guestOutputFeed(uint8_t c) {
    // Hold only a possible prefix of ESC[>64v.  On any mismatch, immediately
    // return the held bytes to the normal terminal parser in their original
    // order, so all ordinary ANSI and text behavior stays unchanged.
    if (!hostEventCandidateLength && c != hostReflashEvent[0]) {
        feedTerminalOutput(c);
        return;
    }
    hostEventCandidate[hostEventCandidateLength++] = c;
    if (!memcmp(hostEventCandidate, hostReflashEvent, hostEventCandidateLength)) {
        if (hostEventCandidateLength == sizeof(hostReflashEvent)) {
            hostEventCandidateLength = 0;
            hostReflashPending = true;
        }
        return;
    }
    for (uint8_t i = 0; i < hostEventCandidateLength; ++i)
        feedTerminalOutput(hostEventCandidate[i]);
    hostEventCandidateLength = 0;
}
void terminalLoop() {
    terminalTextBenchmarkPoll();
    if (pauseRequested.load(std::memory_order_acquire)) {
        pauseAcknowledged.store(true, std::memory_order_release);
        delay(1);
        return;
    }
    pauseAcknowledged.store(false, std::memory_order_release);
    if (displayProfilePending.load(std::memory_order_acquire) &&
        (!zoomRequestPending || uint32_t(millis() - zoomRequestedAt) >= 150) &&
        displayProfilePending.exchange(false, std::memory_order_acq_rel)) {
        const bool userZoom = zoomRequestPending;
        zoomRequestPending = false;
        if (configPalettePending.exchange(false, std::memory_order_acq_rel)) {
            activePalette = profiles.global().modePalette;
            terminal.palette = profiles.palette(activePalette);
            requestedPalette = -1;
        }
        if (userZoom) applyViewZoom(pendingDisplayProfile.viewZoom);
        else applyDisplayProfile(pendingDisplayProfile);
        displayProfileApplied.store(true, std::memory_order_release);
    }
    if (requestedPalette >= 0 && !terminal.plotting() && !plotCapture && !plotEndCleanupPending) {
        const uint8_t next = uint8_t(requestedPalette);
        requestedPalette = -1;
        if (next != activePalette) {
            activePalette = next;
            terminal.palette = profiles.palette(next);
            terminal.invalidate();
            terminal.compositionDirty = true;
            configSavePalette.store(next, std::memory_order_release);
            configChangedAt.store(millis(), std::memory_order_release);
            configSaveDirty.fetch_or(ConfigPaletteDirty, std::memory_order_acq_rel);
            configSavePending.store(true, std::memory_order_release);
        }
    }
    uint8_t c;
    while (input.free() && replies.pop(c))
        input.push(c);
    // Reserve room for a timed-out 63-byte sequence plus the next completed key.
    if (input.free() >= 320 && replies.available() == 0) {
        uint32_t keyNow = millis();
        if (rawInputMode)
            rawTick(keyNow);
        else
            keyboard.tick(keyNow);
        for (int n = 0; n < 64 && input.free() >= 128 && Serial.available(); n++) {
            uint8_t keyByte = Serial.read();
            if (rawInputMode)
                rawFeed(keyByte, millis());
            else
                keyboard.feed(keyByte, millis());
        }
    }
    // Do not overwrite new CP/M output when restoring a temporary bank overlay.
    if (historyNoticeActive && output.available())
        restoreHistoryNotice();
    // Always consume CP/M output, even when its input queue is full.
    for (int n = 0; n < 512 && output.pop(c); n++) {
        if (localComposeRedrawPending) {
            continue;
        }
        guestOutputFeed(c);
        if (plotEndCleanupPending)
            plotEndActivityAt = millis();
        // Preserve every active row traversed during a graphics transaction.
        // At PlotEnd only those rows are repainted, leaving the rest of the
        // retained xterm canvas untouched.
        updateActiveRow();
    }
    historyNoticePoll();
    updateEffectiveBank();
    profileCommandPoll();
    mbasicEditPoll();
    mbasicEditBridgePoll();
    if (localComposeRedrawPending && uint32_t(millis() - localComposeRedrawAt) >= 100) {
        localComposeRedrawPending = false;
        if (localComposeActive) redrawLocalCompose();
    }
    recallPoll();
    plotEndCleanupPoll(millis());
    updateActiveRow();
    uint32_t now = millis();
    if (uint32_t(now - blinkAt) >= profiles.global().cursorBlinkMs) {
        blinkAt = now;
        cursorPhase = !cursorPhase;
        // Cursor and SGR-5 text share the phase, so repaint every visible
        // blinking cell when the phase changes.
        for (int i = 0; i < vt::MaxCells; ++i) {
            if (terminal.flags[i] & vt::Blink)
                terminal.dirty[i] = true;
        }
        terminal.dirty[terminal.index(terminal.x, terminal.y)] = true;
    }
    if (terminal.bells != oldBells) {
        oldBells = terminal.bells;
        bellUntil = now + 150;
        terminal.indicatorDirty = true;
    }
    if (bellUntil && int32_t(now - bellUntil) >= 0) {
        bellUntil = 0;
        terminal.indicatorDirty = true;
    }
    static uint32_t observedOverruns = 0;
    if (observedOverruns != replyOverruns) {
        observedOverruns = replyOverruns;
        terminal.indicatorDirty = true;
    }
    diskActivityIndicatorPoll();
    renderBatch();
    terminalBootBacklightPoll();
    xtgvBacklightPoll();
    mirrorPump();
}
#endif
