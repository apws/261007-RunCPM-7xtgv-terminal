// Windows has no POSIX PTY. Scripted input and explicit loopback are supported.
#include <Arduino.h>
#include <stdexcept>

std::string XtgvDesktopSerial::openPty() {
    throw std::runtime_error("Windows has no desktop PTY; use loopback or scripted input");
}
void XtgvDesktopSerial::closePty() { closeLoopback(); }
int XtgvDesktopSerial::availableForWrite() const {
    return attached_ ? int(65536 - output_.size()) : 0;
}
size_t XtgvDesktopSerial::write(const uint8_t *bytes, size_t count) {
    const size_t accepted = std::min(count, size_t(availableForWrite()));
    output_.insert(output_.end(), bytes, bytes + accepted);
    return accepted;
}
void XtgvDesktopSerial::pump() { if (loopback_) pumpLoopback(); }
