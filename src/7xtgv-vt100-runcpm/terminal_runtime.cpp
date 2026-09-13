#include "runcpm-config.h"
#if !defined(RUNCPM_HEADLESS)
#include "terminal_runtime.h"
#include "7xtgv-QL-font.h"
#include "7xtgv-gfxsetup.h"
#include "byte_queue.h"
#include "history_profiles.h"
#include "line_history.h"
#include "terminal.h"
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
static std::atomic<bool> ready{false};
static std::atomic<bool> pauseRequested{false}, pauseAcknowledged{false};
static std::atomic<bool> textBenchmarkRequested{false}, textBenchmarkFinished{false};
static std::atomic<uint32_t> rebootScheduledAt{0};
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
// The SD file is deliberately ordinary text: one section per history bank,
// and each retained line starts with >.  Keep I/O board-specific and let the
// runtime only own parsing, serialisation, and a low-write-rate dirty flag.
static uint32_t historyObservedGeneration = 0;
static uint32_t historyChangedAt = 0;
static bool historySavePending = false;
static uint8_t historyLoadBank = 0, historyLoadLength = 0;
static char historyLoadText[vt::LineHistory::MaxLine] = {};
static bool historyNoticeActive = false;
static bool historyNoticePeek = false;
static bool historyNoticeRaw = false;
static uint8_t historyNoticeBank = 0, historyNoticeRow = 0;
static uint32_t historyNoticeUntil = 0;
static uint8_t historyNoticePaper[vt::Cols], historyNoticeChars[vt::Cols];
static uint8_t historyNoticeAttrs[vt::Cols], historyNoticeFlags[vt::Cols];
static void showHistoryBank(uint8_t bank, bool peek = false);
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
static void observeLineSession() {
    if (!history || lineSessionActive)
        return;
    const vt::HistoryProfile &profile = profiles.get(history->activeBank());
    if (!profile.activatePrefixLength || terminal.x < profile.activatePrefixLength)
        return;
    const int first = terminal.y * vt::Cols;
    if (!memcmp(terminal.charRAM + first, profile.activatePrefixBytes,
                profile.activatePrefixLength)) {
        history->discardCurrent();
        lineSessionActive = true;
        lineModelAuthoritative = false;
    }
}
static bool cpmPromptOnCursorRow() {
    const int first = terminal.y * vt::Cols;
    const uint8_t drive = terminal.charRAM[first];
    if (drive < 'A' || drive > 'P')
        return false;
    int col = 1, digits = 0;
    while (digits < 2 && col < terminal.x) {
        const uint8_t c = terminal.charRAM[first + col];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
            break;
        ++col; ++digits;
    }
    return digits >= 1 && col < terminal.x && terminal.charRAM[first + col] == '>';
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
    selectEffectiveBank(cpmPromptOnCursorRow() ? 0 : selectedBank);
    observeLineSession();
}
static uint8_t readCursorRow(char *text, uint8_t &cursor) {
    if (!history)
        return 0;
    const vt::HistoryProfile &profile = profiles.get(history->activeBank());
    const int first = terminal.y * vt::Cols;
    int start = 0;
    if (profile.linePrefixLength) {
        const int lastPromptColumn = std::min(8, terminal.x);
        for (int col = 0; col + profile.linePrefixLength <= lastPromptColumn; ++col) {
            if (!memcmp(terminal.charRAM + first + col, profile.linePrefixBytes,
                        profile.linePrefixLength)) {
                start = col + profile.linePrefixLength;
                break;
            }
        }
    }
    if (profile.trimLeft)
        while (start < vt::Cols && terminal.charRAM[first + start] == ' ')
            ++start;
    int end = vt::Cols;
    while (end > start && terminal.charRAM[first + end - 1] == ' ')
        --end;
    uint8_t length = uint8_t(std::min(end - start, int(vt::LineHistory::MaxLine)));
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
    char text[vt::LineHistory::MaxLine];
    uint8_t cursor;
    uint8_t length = readCursorRow(text, cursor);
    history->replaceCurrent(text, length, cursor);
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
        if (mbasicEditMode && (data[i] == 13 || data[i] == 27)) {
            mbasicEditMode = false;
            mbasicEditStartPending = false;
            mbasicEditStartKind = MbasicStartNone;
        }
        if (capture && (data[i] == 13 || (profile->escapeCancels && data[i] == 27))) {
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
    char text[vt::LineHistory::MaxLine]; uint8_t cursor;
    uint8_t length = readCursorRow(text, cursor);
    if (history && length) {
        history->replaceCurrent(text, length, length);
        input.push(27); input.push('Q');
        localComposeActive = true;
        localComposeOriginalLength = length;
        localComposeOriginalCursor = length;
        memcpy(localComposeOriginal, text, length);
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
    if (profile.historyClearLength) {
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
    char visible[vt::LineHistory::MaxLine];
    uint8_t cursor;
    uint8_t length = readCursorRow(visible, cursor);
    (void)cursor;
    const bool matched = length == recallSent.length && !memcmp(visible, recallSent.text, length);
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
    const vt::KeyBinding &binding = profiles.get(history->activeBank()).keys[key];
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
static void keyboardAction(void *, vt::Keyboard::Action action) {
    // Recall waits until the transient label is gone, but bank changes remain
    // live so Alt+PgUp/PgDn can browse banks and redraw #n immediately.
    if (historyNoticeActive &&
        (action == vt::Keyboard::HistoryPrevious || action == vt::Keyboard::HistoryNext))
        return;
    if (action == vt::Keyboard::MirrorSnapshot) {
        requestMirrorSnapshot();
        return;
    }
    if (action == vt::Keyboard::HistoryBankPrevious) {
        if (history) {
            uint8_t erase = history->discardCurrent();
            while (erase--) input.push(8);
            selectedBank = uint8_t((int(selectedBank) + vt::HistoryProfiles::Banks - 1) %
                                   vt::HistoryProfiles::Banks);
            history->select(selectedBank);
            resetLineSession();
            showHistoryBank(history->activeBank());
        }
        return;
    }
    if (action == vt::Keyboard::HistoryBankNext) {
        if (history) {
            uint8_t erase = history->discardCurrent();
            while (erase--) input.push(8);
            selectedBank = uint8_t((selectedBank + 1) % vt::HistoryProfiles::Banks);
            history->select(selectedBank);
            resetLineSession();
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
            uint8_t erase = history->discardCurrent();
            while (erase--) input.push(8);
            selectedBank = uint8_t(action - vt::Keyboard::HistoryBank0);
            history->select(selectedBank);
            resetLineSession();
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
static vt::Keyboard keyboard(terminal, keyboardSend, nullptr, keyboardAction);
static uint8_t rawControl[6];
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
    if (rawControlLength >= 4 && rawControl[3] != '1' && rawControl[3] != '2' && rawControl[3] != '6') return false;
    if (rawControlLength >= 5) {
        if (rawControl[3] == '1' && (rawControl[4] < '4' || rawControl[4] > '7')) return false;
        if (rawControl[3] == '2' && (rawControl[4] < '0' || rawControl[4] > '9')) return false;
        if (rawControl[3] == '6' && rawControl[4] != '2') return false;
    }
    return rawControlLength < 6 || rawControl[5] == 'v';
}
static void rawFeed(uint8_t c, uint32_t now) {
    if (!rawControlLength && c != 0x1b) {
        input.push(c);
        return;
    }
    rawControl[rawControlLength++] = c;
    rawControlAt = now;
    if (!rawControlPrefix()) {
        rawFlush();
        return;
    }
    if (rawControlLength != sizeof(rawControl))
        return;
    uint8_t group = rawControl[3], command = rawControl[4];
    rawControlLength = 0;
    if (group == '2') {
        rawInputMode = false;
        if (history) {
            selectedBank = uint8_t(command - '0');
            history->select(selectedBank);
            resetLineSession();
            showHistoryBank(history->activeBank());
        }
    } else if (command == '4') {
        if (history) showHistoryBank(history->activeBank(), true);
    } else if (command == '5' || command == '7') {
        setRawInputMode(false);
    } else if (command == '6') {
        setRawInputMode(true);
    } else if (group == '6' && command == '2') {
        requestMirrorSnapshot();
    }
}
static void rawTick(uint32_t now) {
    if (rawControlLength && uint32_t(now - rawControlAt) >= 50)
        rawFlush();
}
static bool displayOK = false, cursorPhase = true, connected = false;
static uint32_t blinkAt = 0, bellUntil = 0, oldBells = 0;
#if defined(GFX_BUFFERED_CANVAS)
static uint32_t displayFlushAt = 0;
static bool displayFlushPending = false;
#endif
static uint16_t pixels[6 * 10];
// xterm-256 plotting uses only a transient logical scanline.  The actual
// backing is whichever Arduino_GFX surface this target selected: the S3 canvas
// today, or a direct controller path on the RP2350 target.
static uint16_t *plotScanline = nullptr;
static int plotWidth = 0, plotX = 0, plotY = 0;
static bool plotCapture = false;
// Remains true after CSI 0m: the TFT still contains a completed temporary
// plot even though the parser has resumed ordinary ANSI text.
static bool plotCanvasValid = false;
static int scanCell = 0, mirrorScan = 0;
static char mirrorBuffer[8192];
static size_t mirrorLength = 0, mirrorSent = 0;
static bool mirrorReset = false;
static int activeRow = -1;
static uint8_t activeRowBank = 255;
static uint32_t plotActiveRows = 0;

// A plot may hold back normal opaque redraws for a long time.  When it ends,
// retire the old highlight explicitly before allowing the next prompt row to
// become active.  This is independent of whether GCLS was typed or read from
// a CP/M SUB file.
static void retirePlotActiveRows();
static void rememberPlotActiveRow(int row);

static void requestMirrorSnapshot() {
    terminal.invalidateMirror();
    mirrorReset = true;
}

static void requestDisplayFlush() {
#if defined(GFX_BUFFERED_CANVAS)
    displayFlushPending = true;
#endif
}
static void flushCanvasIfDue() {
#if defined(GFX_BUFFERED_CANVAS)
    const uint32_t now = millis();
    if (displayFlushPending && int32_t(now - displayFlushAt) >= 0) {
        gfx->flush();
        displayFlushPending = false;
        displayFlushAt = now + 33;
    }
#endif
}
static void flushPlotRow() {
    if (!displayOK || !plotScanline || !plotX)
        return;
    if (plotY >= 0 && plotY < gfx->height())
        gfx->draw16bitRGBBitmap(0, plotY, plotScanline, std::min(plotX, plotWidth), 1);
    requestDisplayFlush();
}
static void terminalPlot(void *, vt::Terminal::PlotEvent event, uint16_t color) {
    switch (event) {
    case vt::Terminal::PlotBegin:
        // GCLS clears the graphics surface but still lets the user see and
        // enter the CP/M command which will generate the plot. Suppression of
        // opaque terminal cells begins only with the first actual sample.
        plotCapture = false;
        plotCanvasValid = false;
        plotActiveRows = 0;
        plotX = plotY = 0;
        // The PC has no graphics canvas.  Repaint its ordinary text cells now
        // so beginning a TFT-only xterm256 plot can never leave its mirror
        // blank until the next normal CP/M output arrives.
        requestMirrorSnapshot();
        // GCLS only clears the physical graphics backing. Logical text and its
        // PC mirror remain intact; future normal output will repaint it.
        std::fill(terminal.dirty, terminal.dirty + vt::Cells, false);
        if (displayOK) {
            gfx->fillScreen(0xffff);
            requestDisplayFlush();
        }
        break;
    case vt::Terminal::PlotSample:
        plotCapture = true;
        plotCanvasValid = true;
        rememberPlotActiveRow(activeRow);
        if (plotScanline && plotY >= 0 && plotY < gfx->height() && plotX >= 0 && plotX < plotWidth)
            plotScanline[plotX] = color;
        ++plotX;
        break;
    case vt::Terminal::PlotRow:
        flushPlotRow();
        plotX = 0;
        ++plotY;
        break;
    case vt::Terminal::PlotEnd:
        flushPlotRow();
        plotX = 0;
        // Discard terminal cells accumulated while plotting was deliberately
        // suppressing opaque TFT writes. SGR 0 arrives before the benchmark's
        // completion message, so subsequent normal output is still drawn.
        // Without this cleanup the delayed cells erase the completed plot in
        // one full repaint as soon as capture ends.
        std::fill(terminal.dirty, terminal.dirty + vt::Cells, false);
        plotCapture = false;
        retirePlotActiveRows();
        break;
    case vt::Terminal::PlotAbort:
        plotX = plotY = 0;
        plotCapture = false;
        plotCanvasValid = false;
        plotActiveRows = 0;
        break;
    }
}

static vt::RGB profileRGB(const uint8_t rgb[3]) { return {rgb[0], rgb[1], rgb[2]}; }
static void dirtyRow(int row) {
    if (row < 0 || row >= vt::Rows)
        return;
    std::fill(terminal.dirty + row * vt::Cols, terminal.dirty + (row + 1) * vt::Cols, true);
    terminal.mirrorDirty[row] = true;
}
static void rememberPlotActiveRow(int row) {
    if (row >= 0 && row < vt::Rows)
        plotActiveRows |= uint32_t(1) << row;
}
static void updateActiveRow() {
    const uint8_t bank = history ? history->activeBank() : 0;
    const int next = profiles.get(bank).activeLine ? terminal.y : -1;
    if (plotCapture) {
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
    const uint32_t rows = plotActiveRows;
    plotActiveRows = 0;
    activeRow = -1;
    activeRowBank = 255;
    for (int row = 0; row < vt::Rows; ++row)
        if (rows & (uint32_t(1) << row))
            dirtyRow(row);
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
    int column = i % vt::Cols - terminal.viewport, row = i / vt::Cols;
    uint8_t bank = (terminal.attrRAM[i] >> 6) & 3, ch = terminal.charRAM[i];
    vt::RGB foreground = terminal.foreground(i), background = terminal.background(i);
    const vt::HistoryProfile &profile = profiles.get(history ? history->activeBank() : 0);
    if (profile.activeLine && row == activeRow)
        background = profileRGB(profile.activeLineRGB);
    bool cursor =
        terminal.cursorVisible && cursorPhase && terminal.x == i % vt::Cols && terminal.y == row;
    if (cursor) {
        background = profileRGB(profile.cursorRGB);
        foreground = {0, 0, 0};
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
    gfx->draw16bitRGBBitmap(column * 6 + GFX_OFS_X, row * 10 + GFX_OFS_Y, pixels, 6, 10);
}
static void writeHistoryNotice(bool label) {
    if (historyNoticePeek) {
        int first = historyNoticeRow * vt::Cols;
        for (int col = 0; col < vt::Cols; ++col) {
            terminal.paperRAM[first + col] = ' ';
            terminal.charRAM[first + col] = ' ';
            terminal.attrRAM[first + col] = 1;
            terminal.flags[first + col] = 0;
            terminal.dirty[first + col] = true;
        }
        char text[vt::HistoryProfile::MaxName + 16];
        if (historyNoticeRaw)
            snprintf(text, sizeof(text), "[## RAW / #%u]", unsigned(historyNoticeBank));
        else
            snprintf(text, sizeof(text), "[#%u %s]", unsigned(historyNoticeBank),
                     profiles.get(historyNoticeBank).name);
        for (int col = 0; text[col] && col < vt::Cols; ++col)
            terminal.charRAM[first + col] = uint8_t(text[col]);
        terminal.mirrorDirty[historyNoticeRow] = true;
        return;
    }
    char sequence[24];
    snprintf(sequence, sizeof(sequence), "\033[%u;1H\033[2K", unsigned(historyNoticeRow + 1));
    for (const char *p = sequence; *p; ++p)
        terminal.feed(uint8_t(*p));
    if (label) {
        terminal.feed('[');
        terminal.feed('#');
        terminal.feed(uint8_t('0' + historyNoticeBank));
        terminal.feed(' ');
        const char *name = profiles.get(historyNoticeBank).name;
        while (*name) terminal.feed(uint8_t(*name++));
        terminal.feed(']');
    }
}
static void restoreHistoryPeek() {
    if (!historyNoticePeek)
        return;
    int first = historyNoticeRow * vt::Cols;
    memcpy(terminal.paperRAM + first, historyNoticePaper, vt::Cols);
    memcpy(terminal.charRAM + first, historyNoticeChars, vt::Cols);
    memcpy(terminal.attrRAM + first, historyNoticeAttrs, vt::Cols);
    memcpy(terminal.flags + first, historyNoticeFlags, vt::Cols);
    std::fill(terminal.dirty + first, terminal.dirty + first + vt::Cols, true);
    terminal.mirrorDirty[historyNoticeRow] = true;
    terminal.mirrorCursorDirty = true;
    historyNoticePeek = false;
}
static void showHistoryBank(uint8_t bank, bool peek) {
    // Repeated Alt+Space refreshes the same overlay from its original row.
    // A profile change first restores a pending non-destructive peek.
    restoreHistoryPeek();
    historyNoticeBank = bank;
    historyNoticeRow = uint8_t(terminal.y);
    historyNoticePeek = peek;
    historyNoticeRaw = rawInputMode;
    if (peek) {
        int first = historyNoticeRow * vt::Cols;
        memcpy(historyNoticePaper, terminal.paperRAM + first, vt::Cols);
        memcpy(historyNoticeChars, terminal.charRAM + first, vt::Cols);
        memcpy(historyNoticeAttrs, terminal.attrRAM + first, vt::Cols);
        memcpy(historyNoticeFlags, terminal.flags + first, vt::Cols);
    }
    historyNoticeUntil = millis() + 1000;
    historyNoticeActive = true;
    writeHistoryNotice(true);
}
static void setRawInputMode(bool enabled) {
    rawControlLength = 0;
    rawInputMode = enabled;
    if (history)
        showHistoryBank(selectedBank, true);
}
static void historyNoticePoll() {
    if (!historyNoticeActive)
        return;
    if (int32_t(millis() - historyNoticeUntil) >= 0) {
        historyNoticeActive = false;
        if (historyNoticePeek) {
            restoreHistoryPeek();
            return;
        }
        writeHistoryNotice(false);
        const vt::HistoryProfile &profile = profiles.get(historyNoticeBank);
        for (uint8_t i = 0; i < profile.selectLength; ++i)
            input.push(profile.selectBytes[i]);
        return;
    }
    int first = historyNoticeRow * vt::Cols;
    bool intact = terminal.charRAM[first] == '[' && terminal.charRAM[first + 1] == '#' &&
                  terminal.charRAM[first + 2] ==
                      uint8_t(historyNoticeRaw ? '#' : '0' + historyNoticeBank);
    if (!intact)
        writeHistoryNotice(true);
}
#if !defined(GFX_BUFFERED_CANVAS)
static void drawIndicator() {
    gfx->fillRect(312, 0, 8, 240, 0);
    // Track shows the visible fraction of the 80-column page.
    int start = terminal.viewport * 220 / vt::Cols;
    gfx->fillRect(314, start + 10, 4, vt::Visible * 220 / vt::Cols, 0x7bef);
    if (terminal.x < terminal.viewport)
        gfx->fillRect(312, 0, 8, 6, 0xffff);
    if (terminal.x >= terminal.viewport + vt::Visible)
        gfx->fillRect(312, 234, 8, 6, 0xffff);
    if (int32_t(bellUntil - millis()) > 0)
        gfx->fillRect(312, 112, 8, 16, 0xffe0);
    if (replyOverruns)
        gfx->fillRect(312, 106, 8, 4, 0xf800);
}
#endif
static void renderBatch() {
    if (!displayOK)
        return;
    // Plot samples paint directly into the graphics surface. Keep terminal
    // cells out until SGR 0 ends that transaction, but still flush a buffered
    // canvas so the physical Mandelbrot image becomes visible.
    if (plotCapture) {
        flushCanvasIfDue();
        return;
    }
    int drawn = 0;
#if defined(GFX_BUFFERED_CANVAS)
    constexpr int DrawBudget = vt::Visible * vt::Rows;
#else
    constexpr int DrawBudget = 12;
#endif
    for (int checked = 0; checked < vt::Visible * vt::Rows && drawn < DrawBudget; checked++) {
        int view = scanCell++ % (vt::Visible * vt::Rows);
        int i = (view / vt::Visible) * vt::Cols + view % vt::Visible + terminal.viewport;
        if (terminal.dirty[i]) {
            drawCell(i);
            terminal.dirty[i] = false;
            drawn++;
        }
    }
    // Keep the scan index bounded even after months of continuous operation.
    scanCell %= vt::Visible * vt::Rows;
    if (terminal.indicatorDirty) {
#if !defined(GFX_BUFFERED_CANVAS)
        drawIndicator();
#endif
        terminal.indicatorDirty = false;
    }
#if defined(GFX_BUFFERED_CANVAS)
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
    for (int j = 0; j < vt::Rows; j++) {
        int r = (mirrorScan + j) % vt::Rows;
        if (terminal.mirrorDirty[r]) {
            row = r;
            mirrorScan = (r + 1) % vt::Rows;
            break;
        }
    }
    if (row >= 0) {
        mirrorLength = snprintf(mirrorBuffer, sizeof(mirrorBuffer), "\033[?25l\033[%d;1H", row + 1);
        int oldAttr = -1, oldFlags = -1;
        for (int col = 0; col < vt::Cols; col++) {
            int i = row * vt::Cols + col;
            if (oldAttr != terminal.attrRAM[i] || oldFlags != terminal.flags[i]) {
                vt::RGB f = terminal.foreground(i), b = terminal.background(i);
                const vt::HistoryProfile &profile = profiles.get(history ? history->activeBank() : 0);
                if (profile.activeLine && row == activeRow)
                    b = profileRGB(profile.activeLineRGB);
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
bool terminalHistoryWrite(TerminalHistoryWriteByte writeByte, void *context) {
    if (!history || !writeHistoryText(writeByte, context, "# 7xtgv terminal history v1\n"))
        return false;
    char text[vt::LineHistory::MaxLine];
    for (uint8_t bank = 0; bank < vt::LineHistory::Banks; ++bank) {
        char header[8];
        snprintf(header, sizeof(header), "[%u]\n", unsigned(bank));
        if (!writeHistoryText(writeByte, context, header))
            return false;
        for (uint8_t ordinal = 0; ordinal < history->bankCount(bank); ++ordinal) {
            uint8_t length = 0;
            if (!history->copyEntry(bank, ordinal, text, length) ||
                !writeByte(context, '>'))
                return false;
            for (uint8_t i = 0; i < length; ++i)
                if (!writeByte(context, uint8_t(text[i])))
                    return false;
            if (!writeByte(context, '\n'))
                return false;
        }
    }
    return true;
}
bool terminalHistorySaveDue() {
    if (!history)
        return false;
    const uint32_t generation = history->generation();
    if (generation != historyObservedGeneration) {
        historyObservedGeneration = generation;
        historyChangedAt = millis();
        historySavePending = true;
    }
    return historySavePending && uint32_t(millis() - historyChangedAt) >= 2000;
}
void terminalHistorySaveComplete(bool saved) {
    if (saved)
        historySavePending = false;
    else
        historyChangedAt = millis(); // retry later without a busy SD loop
}

void terminalSetup() {
    history = new (std::nothrow) vt::LineHistory;
    Serial.begin(115200);
    // No USB diagnostics: this port carries only the interpreted screen mirror.
    GFX_DEVICE_INIT();
    displayOK = GFX_BEGIN_OK();
    pinMode(GFX_BL, OUTPUT);
    analogWrite(GFX_BL, 150);
    if (displayOK) {
        gfx->fillScreen(0);
#if defined(GFX_BUFFERED_CANVAS)
        gfx->flush();
        displayFlushAt = millis() + 33;
#endif
        plotWidth = gfx->width();
        if (plotWidth > 0)
            plotScanline = new (std::nothrow) uint16_t[plotWidth];
    }
    terminal.setPlotSink(terminalPlot, nullptr);
    terminal.reset();
    if (!displayOK) {
        const char *s = "TFT initialization failed; PC mirror remains available.\r\n";
        while (*s)
            terminal.feed(*s++);
    }
    ready.store(true, std::memory_order_release);
}
void terminalConfigBegin() { profiles.beginLoad(); }
void terminalConfigFeed(uint8_t byte) { profiles.feed(byte); }
unsigned terminalConfigEnd() {
    profiles.finishLoad();
    return profiles.errors();
}
void terminalWaitReady() {
    while (!ready.load(std::memory_order_acquire))
        delay(1);
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
#if defined(ARDUINO_ARCH_ESP32)
    ESP.restart();
#endif
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
    while (!input.pop(c))
        delay(1);
    return c;
}
void virtualWrite(uint8_t c) {
    while (!output.push(c))
        delay(1);
}
void terminalLoop() {
    terminalRebootPoll();
    terminalTextBenchmarkPoll();
    if (pauseRequested.load(std::memory_order_acquire)) {
        pauseAcknowledged.store(true, std::memory_order_release);
        delay(1);
        return;
    }
    pauseAcknowledged.store(false, std::memory_order_release);
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
    if (historyNoticePeek && output.available()) {
        historyNoticeActive = false;
        restoreHistoryPeek();
    }
    // Always consume CP/M output, even when its input queue is full.
    for (int n = 0; n < 512 && output.pop(c); n++) {
        if (localComposeRedrawPending) {
            continue;
        }
        terminal.feed(c);
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
    updateActiveRow();
    uint32_t now = millis();
    if (uint32_t(now - blinkAt) >= 500) {
        blinkAt = now;
        cursorPhase = !cursorPhase;
        // Cursor and SGR-5 text share the phase, so repaint every visible
        // blinking cell when the phase changes.
        for (int i = 0; i < vt::Cells; ++i) {
            if (terminal.flags[i] & vt::Blink)
                terminal.dirty[i] = true;
        }
        terminal.dirty[terminal.y * vt::Cols + terminal.x] = true;
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
    renderBatch();
    mirrorPump();
}
#endif
