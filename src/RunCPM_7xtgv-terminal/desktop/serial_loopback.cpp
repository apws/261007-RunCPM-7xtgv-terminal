// Optional local desktop transport. Guest bytes and owner control use separate
// authenticated sockets. Linux keeps PTY as its ordinary launch path.
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif
#include <Arduino.h>
#include "platform.h"
#include <stdexcept>

namespace
{
#if defined(_WIN32)
    using Socket = SOCKET;
    constexpr Socket invalid = INVALID_SOCKET;

    bool wouldBlock()
    {
        return WSAGetLastError() == WSAEWOULDBLOCK;
    }

    void closeSocket(Socket s)
    {
        closesocket(s);
    }

    void nonblocking(Socket s)
    {
        u_long mode = 1;
        if (ioctlsocket(s, FIONBIO, &mode))
            throw std::runtime_error("Cannot configure loopback socket");
        if (!SetHandleInformation(reinterpret_cast<HANDLE>(s), HANDLE_FLAG_INHERIT, 0))
            throw std::runtime_error("Cannot protect loopback handle inheritance");
    }

    constexpr int sendFlags = 0;
#else
    using Socket = int;
    constexpr Socket invalid = -1;

    bool wouldBlock()
    {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }

    void closeSocket(Socket s)
    {
        ::close(s);
    }

    void nonblocking(Socket s)
    {
        if (fcntl(s, F_SETFL, O_NONBLOCK) < 0 || fcntl(s, F_SETFD, FD_CLOEXEC) < 0)
            throw std::runtime_error("Cannot configure loopback socket");
    }

    constexpr int sendFlags = MSG_NOSIGNAL;
#endif
    struct Channel
    {
        Socket socket = invalid;
        bool authenticated = false;
        uint32_t acceptedAt = 0;
        std::string handshake;

        ~Channel()
        {
            close();
        }

        void close()
        {
            if (socket != invalid)
                closeSocket(socket);
            socket = invalid;
            authenticated = false;
            handshake.clear();
        }
    };

    Socket listenLocal(uint16_t& port)
    {
        Socket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == invalid)
            throw std::runtime_error("Cannot create loopback listener");
        try
        {
            nonblocking(s);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(s, 2))
                throw std::runtime_error("Cannot bind loopback listener");
#if defined(_WIN32)
            int length = sizeof(address);
#else
            socklen_t length = sizeof(address);
#endif
            if (getsockname(s, reinterpret_cast<sockaddr*>(&address), &length))
                throw std::runtime_error("Cannot identify loopback listener");
            port = ntohs(address.sin_port);
            return s;
        }
        catch (...)
        {
            closeSocket(s);
            throw;
        }
    }

    void authenticate(Socket listener, Channel& channel, const std::string& expected)
    {
        if (channel.socket == invalid)
        {
            channel.socket = accept(listener, nullptr, nullptr);
            if (channel.socket == invalid)
            {
                if (!wouldBlock())
                    throw std::runtime_error("Cannot accept loopback connection");
                return;
            }
            nonblocking(channel.socket);
            channel.acceptedAt = millis();
        }
        if (channel.authenticated)
            return;
        if (uint32_t(millis() - channel.acceptedAt) > 5000)
        {
            channel.close();
            return;
        }
        // Consume only the handshake. Following bytes belong to the selected channel.
        for (unsigned n = 0; n < 80; ++n)
        {
            char byte = 0;
            const int received = recv(channel.socket, &byte, 1, 0);
            if (received < 0 && wouldBlock())
                return;
            if (received <= 0)
            {
                channel.close();
                return;
            }
            channel.handshake += byte;
            if (expected.compare(0, channel.handshake.size(), channel.handshake) != 0)
            {
                channel.close();
                return;
            }
            if (channel.handshake == expected)
            {
                channel.authenticated = true;
                return;
            }
        }
    }
}

struct XtgvDesktopSerial::Loopback
{
    Socket dataListener = invalid, controlListener = invalid;
    Channel data, control;
    std::string handshake, command;
    uint32_t started = millis();
    bool owned = false, stopped = false;
#if defined(_WIN32)
    bool wsa = false;
#endif
    ~Loopback()
    {
        data.close();
        control.close();
        if (dataListener != invalid)
            closeSocket(dataListener);
        if (controlListener != invalid)
            closeSocket(controlListener);
#if defined(_WIN32)
        if (wsa)
            WSACleanup();
#endif
    }
};

std::string XtgvDesktopSerial::openLoopback(const std::string& token)
{
    if (token.size() != 64 || token.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Loopback token must contain 64 lowercase hexadecimal digits");
    closePty();
    auto state = std::make_shared<Loopback>();
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        throw std::runtime_error("Cannot initialize Winsock");
    state->wsa = true;
#endif
    uint16_t dataPort = 0, controlPort = 0;
    state->dataListener = listenLocal(dataPort);
    state->controlListener = listenLocal(controlPort);
    state->handshake = "XTGV/1 " + token + "\n";
    loopback_ = state;
    return "{\"data_port\":" + std::to_string(dataPort) + ",\"control_port\":" + std::to_string(controlPort) + "}";
}

void XtgvDesktopSerial::closeLoopback()
{
    loopback_.reset();
    attached_ = false;
    output_.clear();
}

bool XtgvDesktopSerial::transportStopRequested() const
{
    return loopback_ && loopback_->stopped;
}

void XtgvDesktopSerial::pumpLoopback()
{
    auto& s = *loopback_;
    auto stop = [&]
    {
        s.stopped = true;
        xtgvDesktopRequestStop();
    };
    auto detach = [&]
    {
        s.data.close();
        attached_ = false;
        output_.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        input_.clear();
    };
    if (s.stopped)
        return;
    authenticate(s.controlListener, s.control, s.handshake);
    if (!s.control.authenticated)
    {
        if (s.owned || uint32_t(millis() - s.started) > 30000)
            stop();
        return;
    }
    s.owned = true;
    char control[16];
    const int n = recv(s.control.socket, control, sizeof(control), 0);
    if (n == 0 || (n < 0 && !wouldBlock()))
    {
        stop();
        return;
    }
    if (n > 0)
    {
        s.command.append(control, size_t(n));
        // STOP is host control only. Invalid control input also closes the owner.
        if (s.command == "STOP\n" || std::string("STOP\n").compare(0, s.command.size(), s.command) != 0)
        {
            stop();
            return;
        }
    }
    authenticate(s.dataListener, s.data, s.handshake);
    if (!s.data.authenticated)
        return;
    attached_ = true;
    char bytes[4096];
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const int room = int(std::min(sizeof(bytes), size_t(16384 - input_.size())));
        // Even with a full queue, poll for EOF without consuming guest input.
        const int count = recv(s.data.socket, bytes, room ? room : 1, room ? 0 : MSG_PEEK);
        if (count == 0 || (count < 0 && !wouldBlock()))
        {
            s.data.close();
            attached_ = false;
            output_.clear();
            input_.clear();
            return;
        }
        if (count > 0 && room)
            input_.insert(input_.end(), bytes, bytes + count);
    }
    if (!output_.empty())
    {
        const int count = int(std::min(sizeof(bytes), output_.size()));
        std::copy_n(output_.begin(), count, bytes);
        const int sent = send(s.data.socket, bytes, count, sendFlags);
        if (sent > 0)
            output_.erase(output_.begin(), output_.begin() + sent);
        else if (sent == 0 || !wouldBlock())
            detach();
    }
}
