#pragma once

namespace vt {
// Startup warnings and fatal errors only, never guest warm boots/VER.
// The HAL stores plain text; this presentation helper owns terminal syntax.
template <typename Write>
void writeBootWarning(Write write, const char *message, bool ansi) {
    if (!message || !*message) return;
    if (ansi) write("\033[30;41m");
    write(message);
    // Extend red paper to the right edge, then reset before the CCP prompt.
    if (ansi) write("\033[K\033[0m");
    write("\r\n");
}
} // namespace vt
