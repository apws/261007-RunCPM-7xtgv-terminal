#include "platform.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <csignal>
#ifndef XTGV_DESKTOP_SDL
#define XTGV_DESKTOP_SDL 0
#endif
#if XTGV_DESKTOP_SDL
#include "presenter.h"
#endif

// Compile the original sketch, core and 7XF.
#include "../RunCPM_7xtgv-terminal.ino"

namespace {
volatile std::sig_atomic_t interrupted = 0;
void stopSignal(int) { interrupted = 1; }
void snapshot(const std::string &path) {
    auto *canvas = xtgvDesktopCanvas();
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << canvas->width() << ' ' << canvas->height() << "\n255\n";
    for (size_t i = 0; i < size_t(canvas->width()) * canvas->height(); ++i) {
        const uint16_t c = canvas->getFramebuffer()[i];
        const char rgb[] = {char(((c >> 11) & 31) * 255 / 31),
                            char(((c >> 5) & 63) * 255 / 63), char((c & 31) * 255 / 31)};
        file.write(rgb, 3);
    }
    if (!file) throw std::runtime_error("Cannot write framebuffer snapshot");
}
}
int main(int argc, char **argv) {
    bool usePty = false, headless = !bool(XTGV_DESKTOP_SDL);
    std::string card, inputPath, capture;
    uint32_t runForMs = 0;
    bool presenterOpen = false;
    int result = 0;
    try {
        if (argc == 3 && argv[1][0] != '-') { card = argv[1]; inputPath = argv[2]; }
        else for (int i = 1; i < argc; ++i) {
            const std::string option(argv[i]);
            if (option == "--describe") {
                std::cout << XTGV_DISPLAY_DEFAULTS.id << ' ' << XTGV_BOARD_TARGET_NAME << ' '
                          << XTGV_DISPLAY_DEFAULTS.physicalWidth << 'x' << XTGV_DISPLAY_DEFAULTS.physicalHeight
                          << " desktop flush=" << XTGV_DISPLAY_NEEDS_FLUSH
                          << " full-budget=" << XTGV_DISPLAY_FULL_FRAME_BUDGET << '\n';
                return 0;
            }
            if (option == "--pty") { usePty = true; continue; }
            if (option == "--headless") { headless = true; continue; }
            if (i + 1 == argc) throw std::runtime_error("Missing option value");
            const std::string value(argv[++i]);
            if (option == "--card-root") card = value;
            else if (option == "--input") inputPath = value;
            else if (option == "--snapshot") capture = std::filesystem::absolute(value).string();
            else if (option == "--run-for-ms") {
                size_t used = 0;
                const auto parsed = std::stoul(value, &used);
                if (used != value.size() || parsed > 3600000) throw std::runtime_error("Invalid duration");
                runForMs = uint32_t(parsed);
            } else throw std::runtime_error("Unknown option: " + option);
        }
        if (card.empty()) throw std::runtime_error("Use --card-root PATH [--pty] [--input FILE] [--headless] [--snapshot FILE.ppm]");
        std::string commands;
        if (!inputPath.empty()) {
            std::ifstream input(inputPath, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot read input fixture");
            commands.assign(std::istreambuf_iterator<char>(input), {});
            if (commands.size() > 16384) throw std::runtime_error("Input fixture too large");
        }
        const auto root = std::filesystem::canonical(card);
        if (!std::filesystem::is_directory(root)) throw std::runtime_error("Card root must be a directory");
        std::filesystem::current_path(root);
        xtgvDesktopSetCardRoot(root);
        std::signal(SIGINT, stopSignal); std::signal(SIGTERM, stopSignal);
        std::cout << "Desktop " << XTGV_DISPLAY_DEFAULTS.id << " | " << xtgvFirmwareBuild()
                  << " | card=" << root << std::endl;
        if (usePty) std::cout << "PTY=" << Serial.openPty() << std::endl;
#if XTGV_DESKTOP_SDL
        if (!headless) {
            const std::string title = std::string("7xtgv desktop / ") + XTGV_DISPLAY_DEFAULTS.id;
            xtgvPresenterOpen(XTGV_DISPLAY_DEFAULTS.physicalWidth, XTGV_DISPLAY_DEFAULTS.physicalHeight, title.c_str());
            presenterOpen = true;
        }
#endif
        setup();
#if XTGV_DESKTOP_SDL
        if (presenterOpen) {
            xtgvDesktopCanvas()->publish = xtgvPresenterPublish;
            xtgvDesktopCanvas()->flush();
        }
#endif
        bool queued = commands.empty();
        const auto started = millis();
        while (!xtgvEmulatorFinished.load(std::memory_order_acquire)) {
            if (interrupted || (runForMs && uint32_t(millis() - started) >= runForMs)) xtgvDesktopRequestStop();
            Serial.pump(); loop();
            if (!XTGV_DISPLAY_NEEDS_FLUSH) xtgvDesktopCanvas()->publishDirect();
#if XTGV_DESKTOP_SDL
            if (presenterOpen) xtgvPresenterPoll();
#endif
            if (!queued && uint32_t(millis() - started) >= 250)
                queued = Serial.feed(reinterpret_cast<const uint8_t *>(commands.data()), commands.size()) == commands.size();
            delay(1);
        }
        xtgvEmulatorJoinFinished();
        for (unsigned i = 0; i < 120; ++i) {
            Serial.pump(); loop();
            if (!XTGV_DISPLAY_NEEDS_FLUSH) xtgvDesktopCanvas()->publishDirect();
#if XTGV_DESKTOP_SDL
            if (presenterOpen) xtgvPresenterPoll();
#endif
            delay(1);
        }
        if (!capture.empty()) snapshot(capture);
        if (!xtgvDesktopWorkerError.empty()) throw std::runtime_error(xtgvDesktopWorkerError);
        if (!xtgvDesktopSaveOK) throw std::runtime_error("Desktop shutdown could not save/close all pending storage");
        if (Status != STATUS_EXIT && !xtgvDesktopStopping()) throw std::runtime_error("RunCPM stopped without guest EXIT");
        std::cout << "Integrated Arduino sketch / RunCPM 6.9 / 7XF: finished normally\n";
        result = xtgvDesktopRebootRequested() ? 75 : 0;
    } catch (const std::exception &error) {
        std::cerr << "Desktop: " << error.what() << '\n';
        // A failed presenter must not throw again while we drain terminal
        // queues for the storage owner's cooperative shutdown.
        if (xtgvDesktopCanvas()) xtgvDesktopCanvas()->publish = nullptr;
        xtgvDesktopRequestStop();
        while (xtgvEmulatorWorker.joinable() && !xtgvEmulatorFinished.load(std::memory_order_acquire)) {
            loop(); delay(1);
        }
        xtgvEmulatorJoinFinished();
        result = 1;
    }
    Serial.closePty();
#if XTGV_DESKTOP_SDL
    xtgvPresenterClose(); // Also releases partially initialized SDL resources.
#endif
    return result;
}
