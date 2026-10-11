#include <Arduino.h>
#include <cerrno>
#include <stdexcept>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

std::string XtgvDesktopSerial::openPty() {
    master_ = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (master_ < 0 || grantpt(master_) || unlockpt(master_)) {
        closePty(); throw std::runtime_error("Cannot create desktop PTY");
    }
    char *name = ptsname(master_);
    if (!name) { closePty(); throw std::runtime_error("Cannot name desktop PTY"); }
    const std::string path(name);
    int slave = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    termios settings{};
    if (slave < 0 || tcgetattr(slave, &settings)) {
        if (slave >= 0) ::close(slave);
        closePty(); throw std::runtime_error("Cannot configure desktop PTY");
    }
    cfmakeraw(&settings);
    const bool ok = tcsetattr(slave, TCSANOW, &settings) == 0;
    ::close(slave); // no retained slave: hangup tells us when the mirror leaves
    if (!ok) { closePty(); throw std::runtime_error("Cannot set raw PTY"); }
    return path;
}
void XtgvDesktopSerial::closePty() {
    closeLoopback();
    if (master_ >= 0) ::close(master_);
    master_ = -1; attached_ = false; output_.clear();
}
int XtgvDesktopSerial::availableForWrite() const {
    return attached_ ? int(65536 - output_.size()) : 0;
}
size_t XtgvDesktopSerial::write(const uint8_t *bytes, size_t count) {
    size_t accepted = std::min(count, size_t(availableForWrite()));
    output_.insert(output_.end(), bytes, bytes + accepted);
    return accepted;
}
void XtgvDesktopSerial::pump() {
    if (loopback_) { pumpLoopback(); return; }
    if (master_ < 0) return;
    pollfd fd{master_, POLLIN | POLLOUT, 0};
    if (::poll(&fd, 1, 0) < 0) return;
    const bool connected = !(fd.revents & (POLLHUP | POLLERR | POLLNVAL));
    if (!connected) {
        attached_ = false; output_.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        input_.clear();
        return;
    }
    attached_ = true;
    uint8_t bytes[4096];
    if (fd.revents & POLLIN) {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t room = std::min(sizeof(bytes), size_t(16384 - input_.size()));
        const auto n = room ? ::read(master_, bytes, room) : 0;
        if (n > 0) input_.insert(input_.end(), bytes, bytes + n);
    }
    if ((fd.revents & POLLOUT) && !output_.empty()) {
        const auto count = std::min(sizeof(bytes), output_.size());
        std::copy_n(output_.begin(), count, bytes);
        const auto n = ::write(master_, bytes, count);
        if (n > 0) output_.erase(output_.begin(), output_.begin() + n);
    }
}
