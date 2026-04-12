#ifndef SPUTTEROS_EXAMPLES_COMMON_HAL_TCPSTREAMSERVER_H
#define SPUTTEROS_EXAMPLES_COMMON_HAL_TCPSTREAMSERVER_H

/**
 * @file TcpStreamServer.h
 * @brief Single-client TCP server that satisfies SputterOS::IStream.
 *
 * Intended for host-native example projects so that sputterctl (or any
 * other COBS-framed client) can connect over a local TCP socket instead
 * of a physical serial port.
 *
 * ### Usage
 * @code
 *   ExamplesCommon::TcpStreamServer tcpStream(9000);
 *   tcpStream.startAccept();           // blocks until one client connects
 *   builder.setStream(&tcpStream);
 *   // ... normal SystemBuilder + System tick loop ...
 * @endcode
 *
 * ### Design
 * - Listens on 0.0.0.0:<port>.
 * - Accepts exactly one client; subsequent connection attempts are queued
 *   by the OS but not serviced until the current client disconnects.
 * - A background receive thread reads from the socket into a 4 KiB
 *   circular buffer; the main tick thread drains via available()/read().
 * - write() sends directly on the connected socket (blocking send).
 * - Thread safety: receive buffer access is protected by a std::mutex.
 *
 * @note Link with Threads::Threads (already required by all example targets).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/12/2026
 */

#include "sputteros/hal/devices/IStream.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

// ── Platform socket headers ──────────────────────────────────────────────────
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "Ws2_32.lib")
#  endif
// Undefine Windows macros that conflict with SputterOS enum values.
#  ifdef ERROR
#    undef ERROR
#  endif
#  ifdef IGNORE
#    undef IGNORE
#  endif
using SocketFd = SOCKET;
static constexpr SocketFd kInvalidSocket = INVALID_SOCKET;
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
using SocketFd = int;
static constexpr SocketFd kInvalidSocket = -1;
#endif

namespace ExamplesCommon
{

class TcpStreamServer : public SputterOS::IStream
{
  public:
    static constexpr std::size_t kRxBufSize = 4096U;

    /**
     * @brief Construct with the TCP port to listen on.
     * @param port  Port number (e.g. 9000).
     */
    explicit TcpStreamServer(uint16_t port) : m_port(port) {}

    /**
     * @brief Block until one client connects, then start the receive thread.
     *
     * Call this before entering the System tick loop.
     *
     * @return true on success, false if the server socket could not be bound.
     */
    bool startAccept()
    {
#ifdef _WIN32
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            std::fprintf(stderr, "[TcpStreamServer] WSAStartup failed\n");
            return false;
        }
#endif
        m_serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_serverFd == kInvalidSocket)
        {
            std::fprintf(stderr, "[TcpStreamServer] socket() failed\n");
            return false;
        }

        int opt = 1;
#ifdef _WIN32
        ::setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&opt), sizeof(opt));
#else
        ::setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(m_port);

        if (::bind(m_serverFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            std::fprintf(stderr, "[TcpStreamServer] bind() failed on port %u\n",
                         static_cast<unsigned>(m_port));
            closeSocket(m_serverFd);
            return false;
        }

        ::listen(m_serverFd, 1);
        std::printf("[TcpStreamServer] Listening on port %u — waiting for sputterctl...\n",
                    static_cast<unsigned>(m_port));
        std::fflush(stdout);

        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);
        m_clientFd = ::accept(m_serverFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
        if (m_clientFd == kInvalidSocket)
        {
            std::fprintf(stderr, "[TcpStreamServer] accept() failed\n");
            closeSocket(m_serverFd);
            return false;
        }

        char clientIp[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
        std::printf("[TcpStreamServer] Client connected from %s\n", clientIp);
        std::fflush(stdout);

        m_connected.store(true, std::memory_order_release);

        // Start background receive thread.
        m_rxThread = std::thread(&TcpStreamServer::rxLoop, this);

        return true;
    }

    /**
     * @brief Gracefully stop: close sockets and join the receive thread.
     */
    void stop()
    {
        m_connected.store(false, std::memory_order_release);
        closeSocket(m_clientFd);
        closeSocket(m_serverFd);
        if (m_rxThread.joinable())
        {
            m_rxThread.join();
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

    ~TcpStreamServer() override { stop(); }

    // ── IStream ─────────────────────────────────────────────────────────────

    std::size_t available() const override
    {
        std::lock_guard<std::mutex> lock(m_rxMutex);
        return m_rxLen;
    }

    std::size_t read(uint8_t *buffer, std::size_t max_len) override
    {
        std::lock_guard<std::mutex> lock(m_rxMutex);
        const std::size_t           n = (max_len < m_rxLen) ? max_len : m_rxLen;
        for (std::size_t i = 0; i < n; ++i)
        {
            buffer[i]  = m_rxBuf[m_rxHead];
            m_rxHead   = (m_rxHead + 1) % kRxBufSize;
        }
        m_rxLen -= n;
        return n;
    }

    std::size_t write(const uint8_t *data, std::size_t len) override
    {
        if (m_clientFd == kInvalidSocket || !m_connected.load(std::memory_order_acquire))
        {
            return 0;
        }
        const int sent = ::send(m_clientFd,
                                reinterpret_cast<const char *>(data),
                                static_cast<int>(len), 0);
        return (sent < 0) ? 0U : static_cast<std::size_t>(sent);
    }

    bool isConnected() const override
    {
        return m_connected.load(std::memory_order_acquire);
    }

  private:
    void rxLoop()
    {
        uint8_t tmp[256];
        while (m_connected.load(std::memory_order_acquire))
        {
            const int n = ::recv(m_clientFd, reinterpret_cast<char *>(tmp),
                                 static_cast<int>(sizeof(tmp)), 0);
            if (n <= 0)
            {
                m_connected.store(false, std::memory_order_release);
                break;
            }
            std::lock_guard<std::mutex> lock(m_rxMutex);
            for (int i = 0; i < n; ++i)
            {
                if (m_rxLen < kRxBufSize)
                {
                    m_rxBuf[m_rxTail] = tmp[i];
                    m_rxTail          = (m_rxTail + 1) % kRxBufSize;
                    ++m_rxLen;
                }
                // If buffer full, newest byte is silently dropped.
            }
        }
    }

    static void closeSocket(SocketFd &fd)
    {
        if (fd == kInvalidSocket)
        {
            return;
        }
#ifdef _WIN32
        ::closesocket(fd);
#else
        ::close(fd);
#endif
        fd = kInvalidSocket;
    }

    // ── Members ─────────────────────────────────────────────────────────────
    uint16_t              m_port;
    SocketFd              m_serverFd{kInvalidSocket};
    SocketFd              m_clientFd{kInvalidSocket};
    std::atomic<bool>     m_connected{false};

    mutable std::mutex    m_rxMutex{};
    uint8_t               m_rxBuf[kRxBufSize]{};
    std::size_t           m_rxHead{0};
    std::size_t           m_rxTail{0};
    std::size_t           m_rxLen{0};

    std::thread           m_rxThread;
};

} // namespace ExamplesCommon

#endif // SPUTTEROS_EXAMPLES_COMMON_HAL_TCPSTREAMSERVER_H
