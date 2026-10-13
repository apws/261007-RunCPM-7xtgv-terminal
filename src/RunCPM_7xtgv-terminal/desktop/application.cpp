#include "platform.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <vector>
#include <csignal>
#if defined(_WIN32)
// The existing RunCPM CPU throttle calls Win32 Sleep on Windows.
#include <windows.h>
#include <mmsystem.h>
#endif
#ifndef XTGV_DESKTOP_SDL
#define XTGV_DESKTOP_SDL 0
#endif
#if XTGV_DESKTOP_SDL
#include "presenter.h"
#endif

// Compile the original sketch, core and 7XF.
#include "../RunCPM_7xtgv-terminal.ino"

namespace
{
    volatile std::sig_atomic_t interrupted = 0;

    void stopSignal(int)
    {
        interrupted = 1;
    }
#if defined(_WIN32)
    struct TimerPeriod
    {
        bool active;

        explicit TimerPeriod(bool enabled) : active(enabled && timeBeginPeriod(1) == TIMERR_NOERROR)
        {
            std::cout << "Desktop timer: " << (active ? "1 ms requested" : "system default") << std::endl;
        }

        ~TimerPeriod()
        {
            if (active)
                timeEndPeriod(1);
        }
    };
#endif
    void snapshot(const std::filesystem::path& path)
    {
        auto* canvas = xtgvDesktopCanvas();
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << canvas->width() << ' ' << canvas->height() << "\n255\n";
        for (size_t i = 0; i < size_t(canvas->width()) * canvas->height(); ++i)
        {
            const uint16_t c = canvas->getFramebuffer()[i];
            const char rgb[] = {char(((c >> 11) & 31) * 255 / 31),
                            char(((c >> 5) & 63) * 255 / 63), char((c & 31) * 255 / 31)};
            file.write(rgb, 3);
        }
        if (!file)
            throw std::runtime_error("Cannot write framebuffer snapshot");
    }
}

int desktopMain(int argc, char** argv)
{
    bool usePty = false, headless = !bool(XTGV_DESKTOP_SDL), timingReport = false;
    bool coarseTimer = false;
    std::string card, inputPath, loopbackToken;
    std::filesystem::path capture;
    uint32_t runForMs = 0;
    bool presenterOpen = false;
    int result = 0;
    int windowScale = 1;
    try
    {
        if (argc == 3 && argv[1][0] != '-')
        {
            card = argv[1];
            inputPath = argv[2];
        }
        else
            for (int i = 1; i < argc; ++i)
            {
                const std::string option(argv[i]);
                if (option == "--describe")
                {
                    std::cout << XTGV_DISPLAY_DEFAULTS.id << ' ' << XTGV_BOARD_TARGET_NAME << ' '
                              << XTGV_DISPLAY_DEFAULTS.physicalWidth << 'x' << XTGV_DISPLAY_DEFAULTS.physicalHeight
                              << " desktop flush=1 full-budget=1; board flush=" << XTGV_DISPLAY_NEEDS_FLUSH
                              << " full-budget=" << XTGV_DISPLAY_FULL_FRAME_BUDGET << '\n';
                    return 0;
                }
                if (option == "--pty")
                {
                    usePty = true;
                    continue;
                }
                if (option == "--headless")
                {
                    headless = true;
                    continue;
                }
                if (option == "--timing-report")
                {
                    timingReport = true;
                    continue;
                }
                if (option == "--coarse-timer")
                {
                    coarseTimer = true;
                    continue;
                }
                if (i + 1 == argc)
                    throw std::runtime_error("Missing option value");
                const std::string value(argv[++i]);
                if (option == "--card-root")
                    card = value;
                else if (option == "--loopback-token")
                    loopbackToken = value;
                else if (option == "--input")
                    inputPath = value;
                else if (option == "--snapshot")
                    capture = std::filesystem::absolute(std::filesystem::u8path(value));
                else if (option == "--window-scale")
                {
                    if (value.size() != 1 || value[0] < '1' || value[0] > '9')
                        throw std::runtime_error("Window scale must be an integer 1..9");
                    windowScale = value[0] - '0';
                }
                else if (option == "--run-for-ms")
                {
                    size_t used = 0;
                    const auto parsed = std::stoul(value, &used);
                    if (used != value.size() || parsed > 3600000)
                        throw std::runtime_error("Invalid duration");
                    runForMs = uint32_t(parsed);
                }
                else
                    throw std::runtime_error("Unknown option: " + option);
            }
        if (card.empty())
            throw std::runtime_error("Use --card-root PATH [--pty] [--input FILE] [--headless] [--snapshot FILE.ppm]");
        if (usePty && !loopbackToken.empty())
            throw std::runtime_error("Choose PTY or loopback, not both");
#if defined(_WIN32)
        // Keep idle/input polling responsive on Windows; active drawing and
        // queued terminal work no longer wait between batches.
        TimerPeriod timerPeriod(!coarseTimer);
        std::cout << "Desktop delay: " << xtgvDesktopConfigureDelay(coarseTimer) << std::endl;
#else
        (void)coarseTimer;
#endif
        std::string commands;
        if (!inputPath.empty())
        {
            std::ifstream input(std::filesystem::u8path(inputPath), std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot read input fixture");
            commands.assign(std::istreambuf_iterator<char>(input), {});
            if (commands.size() > 16384)
                throw std::runtime_error("Input fixture too large");
        }
        const auto root = std::filesystem::canonical(std::filesystem::u8path(card));
        if (!std::filesystem::is_directory(root))
            throw std::runtime_error("Card root must be a directory");
        std::filesystem::current_path(root);
        xtgvDesktopSetCardRoot(root);
        std::signal(SIGINT, stopSignal);
        std::signal(SIGTERM, stopSignal);
        std::cout << "Desktop " << XTGV_DISPLAY_DEFAULTS.id << " | " << xtgvFirmwareBuild()
                  << " | card=" << std::quoted(root.u8string()) << std::endl;
        if (usePty)
            std::cout << "PTY=" << Serial.openPty() << std::endl;
        if (!loopbackToken.empty())
            std::cout << "LOOPBACK=" << Serial.openLoopback(loopbackToken) << std::endl;
#if XTGV_DESKTOP_SDL
        if (!headless)
        {
            const std::string title = std::string("7xtgv desktop / ") + XTGV_DISPLAY_DEFAULTS.id;
            xtgvPresenterOpen(XTGV_DISPLAY_DEFAULTS.physicalWidth, XTGV_DISPLAY_DEFAULTS.physicalHeight, title.c_str(),
                              windowScale);
            presenterOpen = true;
        }
#endif
        (void)windowScale; // Also accepted by the headless diagnostic binary.
        setup();
#if XTGV_DESKTOP_SDL
        if (presenterOpen)
        {
            xtgvDesktopCanvas()->publish = xtgvPresenterPublish;
            xtgvDesktopCanvas()->flush();
        }
#endif
        bool queued = commands.empty();
        const auto started = millis();
        uint64_t iterations = 0, pumpUs = 0, terminalUs = 0, presentUs = 0, sleepUs = 0, idleWaits = 0;
        while (!xtgvEmulatorFinished.load(std::memory_order_acquire))
        {
            if (interrupted || (runForMs && uint32_t(millis() - started) >= runForMs))
                xtgvDesktopRequestStop();
            auto tick = timingReport ? micros() : 0;
            Serial.pump();
            if (timingReport)
            {
                const auto now = micros();
                pumpUs += uint32_t(now - tick);
                tick = now;
            }
            loop();
            if (timingReport)
            {
                const auto now = micros();
                terminalUs += uint32_t(now - tick);
                tick = now;
            }
#if XTGV_DESKTOP_SDL
            if (presenterOpen)
                xtgvPresenterPoll();
#endif
            if (timingReport)
            {
                const auto now = micros();
                presentUs += uint32_t(now - tick);
                tick = now;
            }
            if (!queued && uint32_t(millis() - started) >= 250)
                queued =
                    Serial.feed(reinterpret_cast<const uint8_t*>(commands.data()), commands.size()) == commands.size();
            if (!terminalDesktopWorkPending())
            {
                delay(1);
                if (timingReport)
                {
                    sleepUs += uint32_t(micros() - tick);
                    ++idleWaits;
                }
            }
            if (timingReport)
                ++iterations;
        }
        if (timingReport && iterations)
            std::cout << "TIMING loops=" << iterations << " average_us: socket=" << pumpUs / iterations
                      << " terminal_and_texture=" << terminalUs / iterations << " presenter=" << presentUs / iterations
                      << " sleep=" << sleepUs / iterations << " idle_waits=" << idleWaits
                      << " sleep_per_wait=" << (idleWaits ? sleepUs / idleWaits : 0) << '\n';
        xtgvEmulatorJoinFinished();
        for (unsigned i = 0; i < 120; ++i)
        {
            Serial.pump();
            loop();
#if XTGV_DESKTOP_SDL
            if (presenterOpen)
                xtgvPresenterPoll();
#endif
            delay(1);
        }
        if (!capture.empty())
            snapshot(capture);
        if (!xtgvDesktopWorkerError.empty())
            throw std::runtime_error(xtgvDesktopWorkerError);
        if (!xtgvDesktopSaveOK)
            throw std::runtime_error("Desktop shutdown could not save/close all pending storage");
        if (Status != STATUS_EXIT && !xtgvDesktopStopping())
            throw std::runtime_error("RunCPM stopped without guest EXIT");
        std::cout << "Integrated Arduino sketch / RunCPM 6.9 / 7XF: finished normally\n";
        result = xtgvDesktopRebootRequested() && !Serial.transportStopRequested() ? 75 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Desktop: " << error.what() << '\n';
        // A failed presenter must not throw again while we drain terminal
        // queues for the storage owner's cooperative shutdown.
        if (xtgvDesktopCanvas())
            xtgvDesktopCanvas()->publish = nullptr;
        xtgvDesktopRequestStop();
        while (xtgvEmulatorWorker.joinable() && !xtgvEmulatorFinished.load(std::memory_order_acquire))
        {
            loop();
            delay(1);
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

#if defined(_WIN32)
// Windows supplies UTF-16 arguments. Narrow main() loses characters through
// the active ANSI code page before our parser can see them. Keep UTF-8 at the
// parser/log boundary and native wide filesystem paths for actual file I/O.
int wmain(int argc, wchar_t** argv)
{
    try
    {
        std::vector<std::string> arguments;
        arguments.reserve(argc);
        for (int i = 0; i < argc; ++i)
        {
            const int length =
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
            if (!length)
                throw std::runtime_error("Invalid Unicode command-line argument");
            std::string value(size_t(length), '\0');
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, value.data(), length, nullptr,
                                     nullptr))
                throw std::runtime_error("Cannot encode command-line argument as UTF-8");
            value.pop_back(); // API length includes its terminating NUL.
            arguments.push_back(std::move(value));
        }
        std::vector<char*> pointers;
        for (auto& argument : arguments)
            pointers.push_back(argument.data());
        pointers.push_back(nullptr);
        return desktopMain(argc, pointers.data());
    }
    catch (const std::exception& error)
    {
        std::cerr << "Desktop startup: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv)
{
    return desktopMain(argc, argv);
}
#endif
