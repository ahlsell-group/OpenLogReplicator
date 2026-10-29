/* Test of the network writer's client connection: TCP keepalive and dropping a dead client
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

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "../common/Ctx.h"
#include "../common/Thread.h"
#include "../common/exception/NetworkException.h"
#include "../stream/StreamNetwork.h"

// OpenLogReplicator serves one client at a time and accepts the next one only after the current connection is closed.
// A client that vanishes without FIN or RST is noticed only by TCP keepalive while nothing is sent, so keepalive must be
// on for every accepted client, and a connection error must close it so that the next client is accepted.
namespace OpenLogReplicator {
    class StreamNetworkTestAccess {
    public:
        static int client(const StreamNetwork& stream) { return stream.socketFD; }

        static uint16_t port(const StreamNetwork& stream) {
            sockaddr_storage address{};
            socklen_t length = sizeof(address);
            getsockname(stream.serverFD, reinterpret_cast<sockaddr*>(&address), &length);
            return ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port);
        }
    };
}

using namespace OpenLogReplicator;

namespace {
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    class TestThread final : public Thread {
    public:
        explicit TestThread(Ctx* newCtx): Thread(newCtx, "test") {}
        void run() override {}
        std::string getName() const override { return "test"; }
    };

    int option(int fd, int level, int name) {
        int value = -1;
        socklen_t length = sizeof(value);
        if (getsockopt(fd, level, name, &value, &length) != 0)
            return -2;
        return value;
    }

    int connectTo(uint16_t port) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            close(fd);
            return -1;
        }
        return fd;
    }

    // A request as the client frames it: 32-bit length, then the bytes
    void sendFrame(int fd, const std::string& payload) {
        const uint32_t length = htole32(static_cast<uint32_t>(payload.size()));
        std::string frame(reinterpret_cast<const char*>(&length), sizeof(length));
        frame += payload;
        [[maybe_unused]] const ssize_t written = write(fd, frame.data(), frame.size());
    }

    // Closes with RST, like a connection whose peer is gone
    void reset(int fd) {
        const linger lin{1, 0};
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        close(fd);
    }

    bool acceptClient(StreamNetwork& stream) {
        for (int i = 0; i < 1000; ++i) {
            if (stream.isConnected())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    // Reads until a message or an exception; returns the exception code, 0 for a message
    int receive(StreamNetwork& stream, std::string& payload) {
        uint8_t buffer[Stream::READ_NETWORK_BUFFER];
        try {
            for (int i = 0; i < 1000; ++i) {
                errno = 0;
                const uint64_t size = stream.receiveMessageNB(buffer, sizeof(buffer));
                if (size > 0) {
                    payload.assign(reinterpret_cast<const char*>(buffer), size);
                    return 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (const NetworkException& ex) {
            return ex.code;
        }
        return -1;
    }

    struct Server {
        Ctx ctx;
        TestThread thread{&ctx};
        StreamNetwork stream{&ctx, "127.0.0.1:0"};
        uint16_t port{0};

        Server() {
            ctx.writerThread = &thread;
            ctx.pollIntervalUs = 100;
            stream.initialize();
            stream.initializeServer();
            port = StreamNetworkTestAccess::port(stream);
        }
    };

    void defaults() {
        Server server;
        const int client = connectTo(server.port);
        check("defaults: client accepted", client >= 0 && acceptClient(server.stream));
        const int fd = StreamNetworkTestAccess::client(server.stream);
        check("defaults: SO_KEEPALIVE on", option(fd, SOL_SOCKET, SO_KEEPALIVE) == 1);
#ifdef TCP_KEEPIDLE
        check("defaults: idle 60 s", option(fd, IPPROTO_TCP, TCP_KEEPIDLE) == 60);
#endif
#ifdef TCP_KEEPINTVL
        check("defaults: interval 10 s", option(fd, IPPROTO_TCP, TCP_KEEPINTVL) == 10);
#endif
#ifdef TCP_KEEPCNT
        check("defaults: 6 probes", option(fd, IPPROTO_TCP, TCP_KEEPCNT) == 6);
#endif
        close(client);
    }

    void configured() {
        Server server;
        server.stream.setKeepAlive(5, 2, 3);
        const int client = connectTo(server.port);
        check("configured: client accepted", client >= 0 && acceptClient(server.stream));
        const int fd = StreamNetworkTestAccess::client(server.stream);
        check("configured: SO_KEEPALIVE on", option(fd, SOL_SOCKET, SO_KEEPALIVE) == 1);
#ifdef TCP_KEEPIDLE
        check("configured: idle 5 s", option(fd, IPPROTO_TCP, TCP_KEEPIDLE) == 5);
#endif
#ifdef TCP_KEEPINTVL
        check("configured: interval 2 s", option(fd, IPPROTO_TCP, TCP_KEEPINTVL) == 2);
#endif
#ifdef TCP_KEEPCNT
        check("configured: 3 probes", option(fd, IPPROTO_TCP, TCP_KEEPCNT) == 3);
#endif
        close(client);
    }

    void off() {
        Server server;
        server.stream.setKeepAlive(0, 10, 6);
        const int client = connectTo(server.port);
        check("idle 0: client accepted", client >= 0 && acceptClient(server.stream));
        check("idle 0: SO_KEEPALIVE off", option(StreamNetworkTestAccess::client(server.stream), SOL_SOCKET, SO_KEEPALIVE) == 0);
        close(client);
    }

    // The first client dies while a second one waits in the backlog with its request sent, as a Connect task started
    // after the old pod vanished does. The error closes the dead connection and the waiting client is served.
    void deadClientReplaced() {
        Server server;
        const int first = connectTo(server.port);
        check("dead client: first accepted", first >= 0 && acceptClient(server.stream));
        const int firstFd = StreamNetworkTestAccess::client(server.stream);

        const int second = connectTo(server.port);
        sendFrame(second, "INFO-2");
        check("dead client: second waits while the first is held", server.stream.isConnected() &&
              StreamNetworkTestAccess::client(server.stream) == firstFd);

        reset(first);
        std::string payload;
        const int code = receive(server.stream, payload);
        check("dead client: the error is reported (10061)", code == 10061);
        check("dead client: the connection is closed at once", StreamNetworkTestAccess::client(server.stream) == -1);

        check("dead client: the waiting client is accepted", acceptClient(server.stream));
        check("dead client: its request is read", receive(server.stream, payload) == 0 && payload == "INFO-2");
        close(second);
    }

    void closedClient() {
        Server server;
        const int client = connectTo(server.port);
        check("closed client: accepted", client >= 0 && acceptClient(server.stream));
        close(client);
        std::string payload;
        check("closed client: reported as disconnected (10056)", receive(server.stream, payload) == 10056);
        check("closed client: the connection is closed", StreamNetworkTestAccess::client(server.stream) == -1);
    }

    // A connection that dies in the middle of a request
    void deadMidRequest() {
        Server server;
        const int client = connectTo(server.port);
        check("dead mid request: accepted", client >= 0 && acceptClient(server.stream));
        const uint32_t length = htole32(100);
        [[maybe_unused]] const ssize_t written = write(client, &length, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::thread killer([client] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            reset(client);
        });
        std::string payload;
        const int code = receive(server.stream, payload);
        killer.join();
        check("dead mid request: the error is reported (10061)", code == 10061);
        check("dead mid request: the connection is closed", StreamNetworkTestAccess::client(server.stream) == -1);
    }
}

int main() {
    defaults();
    configured();
    off();
    deadClientReplaced();
    closedClient();
    deadMidRequest();

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
