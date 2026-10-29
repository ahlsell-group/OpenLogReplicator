/* Base class for streaming using network sockets
   Copyright (C) 2018-2026 Adam Leszczynski (aleszczynski@bersler.com)

This file is part of OpenLogReplicator.

This program is free software: you can redistribute it and/or
modify it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with this program; see the file LICENSE;
If not, see <http://www.gnu.org/licenses/>. */

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include "../common/Ctx.h"
#include "../common/Thread.h"
#include "../common/exception/ConfigurationException.h"
#include "../common/exception/NetworkException.h"
#include "../common/exception/RuntimeException.h"
#include "StreamNetwork.h"

namespace OpenLogReplicator {
    StreamNetwork::StreamNetwork(Ctx* newCtx, std::string newUri):
            Stream(newCtx, std::move(newUri)) {
        readBuffer[0] = 0;
    }

    StreamNetwork::~StreamNetwork() {
        if (res != nullptr) {
            freeaddrinfo(res);
            res = nullptr;
        }

        if (socketFD != -1) {
            close(socketFD);
            socketFD = -1;
        }

        if (serverFD != -1) {
            close(serverFD);
            serverFD = -1;
        }
    }

    void StreamNetwork::initialize() {
        // Colon
        const auto uriIt = uri.find(':');
        if (uriIt == std::string::npos)
            throw ConfigurationException(30008, "uri is missing ':' in parameter: " + uri);

        host = uri.substr(0, uriIt);
        port = uri.substr(uriIt + 1, uri.length() - 1);
    }

    std::string StreamNetwork::getName() const {
        return "Network:" + uri;
    }

    void StreamNetwork::initializeClient() {
        sockaddr_in addressC{};
        memset(&addressC, 0, sizeof(addressC));
        addressC.sin_family = AF_INET;
        addressC.sin_port = htons(atoi(port.c_str()));

        if ((socketFD = socket(AF_INET, SOCK_STREAM, 0)) == 0)
            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (1)");

        const hostent* server = gethostbyname(host.c_str());
        if (server == nullptr)
            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (2)");

        memcpy(&addressC.sin_addr.s_addr, server->h_addr, server->h_length);
        if (connect(socketFD, reinterpret_cast<sockaddr*>(&addressC), sizeof(addressC)) < 0)
            throw NetworkException(10062, "connection to " + uri + " failed, errno: " + std::to_string(errno) + ", message: " +
                                   strerror(errno));
    }

    void StreamNetwork::initializeServer() {
        addrinfo hints{};
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || res == nullptr)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (3)");

        serverFD = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (serverFD == 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (4)");

        const int flags = fcntl(serverFD, F_GETFL);
        if (flags < 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (5)");
        if (fcntl(serverFD, F_SETFL, flags | O_NONBLOCK) < 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (6)");

        constexpr int64_t opt = 1;
        if (setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) != 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (7)");

        if (bind(serverFD, res->ai_addr, res->ai_addrlen) < 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (8)");
        if (listen(serverFD, 1) < 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (9)");

        freeaddrinfo(res);
        res = nullptr;
    }

    void StreamNetwork::sendMessage(const void* msg, uint64_t length) {
        uint64_t sent = 0;

        if (socketFD == -1)
            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (10)");

        fd_set wset;
        fd_set w;
        FD_ZERO(&wset);
        FD_SET(socketFD, &wset);

        // Header content
        if (length < MAX_LENGTH) {
            uint32_t length32 = length;
            // 32-bit length
            while (sent < sizeof(uint32_t)) {
                if (ctx->softShutdown)
                    return;

                w = wset;
                // Blocking select
                select(socketFD + 1, nullptr, &w, nullptr, nullptr);
                ssize_t r = write(socketFD, reinterpret_cast<const uint8_t*>(&length32) + sent, sizeof(uint32_t) - sent);
                if (r <= 0) {
                    if (r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
                        r = 0;
                    else {
                        close(socketFD);
                        socketFD = -1;
                        throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                               strerror(errno) + " (11)");
                    }
                }
                sent += r;
            }
        } else {
            // 64-bit length
            uint32_t length32 = htole32(MAX_LENGTH);
            while (sent < sizeof(uint32_t)) {
                if (ctx->softShutdown)
                    return;

                w = wset;
                // Blocking select
                select(socketFD + 1, nullptr, &w, nullptr, nullptr);
                ssize_t r = write(socketFD, reinterpret_cast<const uint8_t*>(&length32) + sent, sizeof(uint32_t) - sent);
                if (r <= 0) {
                    if (r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
                        r = 0;
                    else {
                        close(socketFD);
                        socketFD = -1;
                        throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                               strerror(errno) + " (12)");
                    }
                }
                sent += r;
            }

            uint32_t length64 = htole64(length);
            sent = 0;
            while (sent < sizeof(uint64_t)) {
                if (ctx->softShutdown)
                    return;

                w = wset;
                // Blocking select
                select(socketFD + 1, nullptr, &w, nullptr, nullptr);
                ssize_t r = write(socketFD, reinterpret_cast<const uint8_t*>(&length64) + sent, sizeof(uint64_t) - sent);
                if (r <= 0) {
                    if (r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
                        r = 0;
                    else {
                        close(socketFD);
                        socketFD = -1;
                        throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                               strerror(errno) + " (13)");
                    }
                }
                sent += r;
            }
        }

        sent = 0;
        // Message content
        while (sent < length) {
            if (ctx->softShutdown)
                return;

            w = wset;
            // Blocking select
            select(socketFD + 1, nullptr, &w, nullptr, nullptr);
            ssize_t r = write(socketFD, static_cast<const char*>(msg) + sent, length - sent);
            if (r <= 0) {
                if (r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
                    r = 0;
                else {
                    close(socketFD);
                    socketFD = -1;
                    throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                           strerror(errno) + " (14)");
                }
            }
            sent += r;
        }
    }

    uint64_t StreamNetwork::receiveMessage(void* msg, uint64_t bufferSize) {
        uint64_t recvd = 0;

        // Read message length
        while (recvd < sizeof(uint32_t)) {
            if (ctx->softShutdown)
                return 0;

            const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, sizeof(uint32_t) - recvd);

            if (bytes > 0)
                recvd += bytes;
            else if (bytes == 0) {
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10056, "host disconnected");
            } else {
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (15)");
            }
        }

        uint64_t length = le32toh(*static_cast<const uint32_t*>(msg));
        if (length < MAX_LENGTH) {
            // 32-bit message length
            if (bufferSize < length)
                throw NetworkException(10055, "message from client exceeds buffer size (length: " + std::to_string(bufferSize) +
                                       ", buffer size: " + std::to_string(length) + ")");
            recvd = 0;
        } else {
            // 64-bit message length
            recvd = 0;

            while (recvd < sizeof(uint64_t)) {
                if (ctx->softShutdown)
                    return 0;

                const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, sizeof(uint64_t) - recvd);

                if (bytes > 0)
                    recvd += bytes;
                else if (bytes == 0) {
                    close(socketFD);
                    socketFD = -1;
                    throw NetworkException(10056, "host disconnected");
                } else {
                    close(socketFD);
                    socketFD = -1;
                    throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                           strerror(errno) + " (16)");
                }
            }

            length = le64toh(*static_cast<const uint32_t*>(msg));
            if (bufferSize < length)
                throw NetworkException(10055, "message from client exceeds buffer size (length: " + std::to_string(bufferSize) +
                                       ", buffer size: " + std::to_string(length) + ")");
            recvd = 0;
        }

        while (recvd < length) {
            if (ctx->softShutdown)
                return 0;

            const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, length - recvd);

            if (bytes > 0)
                recvd += bytes;
            else if (bytes == 0) {
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10056, "host disconnected");
            } else {
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (17)");
            }
        }

        return recvd;
    }

    uint64_t StreamNetwork::receiveMessageNB(void* msg, uint64_t bufferSize) {
        uint64_t recvd = 0;

        // Read message length
        while (recvd < sizeof(uint32_t)) {
            if (ctx->softShutdown)
                return 0;

            const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, sizeof(uint32_t) - recvd);

            if (bytes > 0)
                recvd += bytes;
            else if (bytes == 0) {
                // Client disconnected
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10056, "host disconnected");
            } else {
                // A dead connection (keepalive or retransmission timeout, reset) is dropped here, so the next client is accepted
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    const int err = errno;
                    dropClient();
                    throw NetworkException(10061, "network error, errno: " + std::to_string(err) + ", message: " + strerror(err) + " (21)");
                }
                if (recvd == 0)
                    return 0;

                ctx->writerThread->contextSet(Thread::CONTEXT::SLEEP);
                ctx->usleepInt(ctx->pollIntervalUs);
                ctx->writerThread->contextSet(Thread::CONTEXT::CPU);
            }
        }

        uint64_t length = le32toh(*static_cast<const uint32_t*>(msg));
        if (length < MAX_LENGTH) {
            // 32-bit message length
            if (bufferSize < length)
                throw NetworkException(10055, "message from client exceeds buffer size (length: " + std::to_string(bufferSize) +
                                       ", buffer size: " + std::to_string(length) + ")");
            recvd = 0;
        } else {
            // 64-bit message length
            recvd = 0;

            while (recvd < sizeof(uint64_t)) {
                if (ctx->softShutdown)
                    return 0;

                const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, sizeof(uint64_t) - recvd);

                if (bytes > 0)
                    recvd += bytes;
                else if (bytes == 0) {
                    // Client disconnected
                    close(socketFD);
                    socketFD = -1;
                    throw NetworkException(10056, "host disconnected");
                } else {
                    if (errno != EWOULDBLOCK && errno != EAGAIN) {
                        const int err = errno;
                        dropClient();
                        throw NetworkException(10061, "network error, errno: " + std::to_string(err) + ", message: " + strerror(err) + " (22)");
                    }

                    ctx->writerThread->contextSet(Thread::CONTEXT::SLEEP);
                    ctx->usleepInt(ctx->pollIntervalUs);
                    ctx->writerThread->contextSet(Thread::CONTEXT::CPU);
                }
            }

            length = le64toh(*static_cast<const uint32_t*>(msg));
            if (bufferSize < length)
                throw NetworkException(10055, "message from client exceeds buffer size (length: " + std::to_string(bufferSize) +
                                       ", buffer size: " + std::to_string(length) + ")");
            recvd = 0;
        }

        while (recvd < length) {
            const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, length - recvd);

            if (bytes > 0)
                recvd += bytes;
            else if (bytes == 0) {
                // Client disconnected
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10056, "host disconnected");
            } else {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    const int err = errno;
                    dropClient();
                    throw NetworkException(10061, "network error, errno: " + std::to_string(err) + ", message: " + strerror(err) + " (23)");
                }

                ctx->writerThread->contextSet(Thread::CONTEXT::SLEEP);
                ctx->usleepInt(ctx->pollIntervalUs);
                ctx->writerThread->contextSet(Thread::CONTEXT::CPU);
            }
        }

        return recvd;
    }

    void StreamNetwork::dropClient() {
        if (socketFD != -1) {
            close(socketFD);
            socketFD = -1;
        }
    }

    void StreamNetwork::setKeepAlive(uint64_t idleS, uint64_t intervalS, uint64_t count) {
        keepAliveIdleS = idleS;
        keepAliveIntervalS = intervalS;
        keepAliveCount = count;
    }

    // A client that vanishes without FIN or RST (a deleted pod, a dropped route) is noticed only by keepalive while
    // nothing is sent: OpenLogReplicator serves one client at a time and would keep the dead one forever. Probes stop
    // while sent data is unacknowledged; then the retransmission timeout (tcp_retries2) drops the connection.
    bool StreamNetwork::applyKeepAlive(int fd, std::string& error) const {
        if (keepAliveIdleS == 0)
            return true;

        const int on = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) != 0) {
            error = "SO_KEEPALIVE, errno: " + std::to_string(errno) + ", message: " + strerror(errno);
            return false;
        }

        const int idle = static_cast<int>(keepAliveIdleS);
#if defined(TCP_KEEPIDLE)
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) != 0) {
            error = "TCP_KEEPIDLE, errno: " + std::to_string(errno) + ", message: " + strerror(errno);
            return false;
        }
#elif defined(TCP_KEEPALIVE)
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle)) != 0) {
            error = "TCP_KEEPALIVE, errno: " + std::to_string(errno) + ", message: " + strerror(errno);
            return false;
        }
#endif
#if defined(TCP_KEEPINTVL)
        const int interval = static_cast<int>(keepAliveIntervalS);
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) != 0) {
            error = "TCP_KEEPINTVL, errno: " + std::to_string(errno) + ", message: " + strerror(errno);
            return false;
        }
#endif
#if defined(TCP_KEEPCNT)
        const int count = static_cast<int>(keepAliveCount);
        if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) != 0) {
            error = "TCP_KEEPCNT, errno: " + std::to_string(errno) + ", message: " + strerror(errno);
            return false;
        }
#endif
        return true;
    }

    bool StreamNetwork::isConnected() {
        if (socketFD != -1)
            return true;

        int64_t addrlen = sizeof(address);
        socketFD = accept(serverFD, reinterpret_cast<sockaddr*>(&address), reinterpret_cast<socklen_t*>(&addrlen));
        if (socketFD < 0) {
            if (errno == EWOULDBLOCK)
                return false;

            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (18)");
        }

        const int flags = fcntl(socketFD, F_GETFL);
        if (flags < 0 || fcntl(socketFD, F_SETFL, flags | O_NONBLOCK) < 0) {
            const int err = errno;
            dropClient();
            throw NetworkException(10061, "network error, errno: " + std::to_string(err) + ", message: " + strerror(err) +
                                   (flags < 0 ? " (19)" : " (20)"));
        }

        char clientHost[NI_MAXHOST];
        char clientService[NI_MAXSERV];
        std::string client = "unknown";
        if (getnameinfo(reinterpret_cast<sockaddr*>(&address), addrlen, clientHost, sizeof(clientHost), clientService, sizeof(clientService),
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0)
            client = std::string(clientHost) + ":" + clientService;
        ctx->info(0, "new client connection: " + client);

        std::string error;
        if (!applyKeepAlive(socketFD, error))
            ctx->warning(60047, "TCP keepalive not set for client: " + client + ", " + error +
                         "; a client that vanishes without closing the connection is not noticed while nothing is sent");
        return true;
    }
}
