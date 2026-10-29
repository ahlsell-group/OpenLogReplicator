/* Header for StreamNetwork class
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

#ifndef STREAM_NETWORK_H_
#define STREAM_NETWORK_H_

#include <netinet/in.h>
#if defined(__linux__) || defined(__GLIBC__)
    #include <endian.h>
#elif defined(__APPLE__)
    #include <libkern/OSByteOrder.h>
    #define htole16(x) OSSwapHostToLittleInt16(x)
    #define htole32(x) OSSwapHostToLittleInt32(x)
    #define htole64(x) OSSwapHostToLittleInt64(x)
    #define le16toh(x) OSSwapLittleToHostInt16(x)
    #define le32toh(x) OSSwapLittleToHostInt32(x)
    #define le64toh(x) OSSwapLittleToHostInt64(x)
#endif

#include "Stream.h"

namespace OpenLogReplicator {
    class StreamNetworkTestAccess;

    class StreamNetwork final : public Stream {
    public:
        static constexpr uint64_t KEEPALIVE_IDLE_S_DEFAULT{60};
        static constexpr uint64_t KEEPALIVE_INTERVAL_S_DEFAULT{10};
        static constexpr uint64_t KEEPALIVE_COUNT_DEFAULT{6};
        static constexpr uint64_t KEEPALIVE_S_MAX{32767};
        static constexpr uint64_t KEEPALIVE_COUNT_MAX{127};

    protected:
        int socketFD{-1};
        int serverFD{-1};
        sockaddr_storage address{};
        std::string host;
        std::string port;
        uint8_t readBuffer[READ_NETWORK_BUFFER]{};
        uint64_t readBufferLen{0};
        struct addrinfo* res{nullptr};
        static constexpr uint32_t MAX_LENGTH = 0xFFFFFFFF;
        // TCP keepalive on an accepted client; idle 0 leaves it off
        uint64_t keepAliveIdleS{KEEPALIVE_IDLE_S_DEFAULT};
        uint64_t keepAliveIntervalS{KEEPALIVE_INTERVAL_S_DEFAULT};
        uint64_t keepAliveCount{KEEPALIVE_COUNT_DEFAULT};

        bool applyKeepAlive(int fd, std::string& error) const;
        void dropClient();

    public:
        StreamNetwork(Ctx* newCtx, std::string newUri);
        ~StreamNetwork() override;

        void initialize() override;
        [[nodiscard]] std::string getName() const override;
        void initializeClient() override;
        void initializeServer() override;
        void sendMessage(const void* msg, uint64_t length) override;
        uint64_t receiveMessage(void* msg, uint64_t bufferSize) override;
        uint64_t receiveMessageNB(void* msg, uint64_t bufferSize) override;
        [[nodiscard]] bool isConnected() override;
        void setKeepAlive(uint64_t idleS, uint64_t intervalS, uint64_t count);

        friend class StreamNetworkTestAccess;
    };
}

#endif
