/* Test of the writer queue gauges on the Prometheus endpoint
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
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "../common/Clock.h"
#include "../common/Ctx.h"
#include "../common/metrics/MetricsPrometheus.h"

// The gauges are set by the writer thread and read by the endpoint's thread. The age is computed at scrape time, so it
// grows while the writer does not run, for example while it is blocked in a send to a dead client.
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

    class TestClock final : public Clock {
    public:
        std::atomic<time_ut> now{5000000000};
        [[nodiscard]] time_ut getTimeUt() const override { return now.load(); }
        [[nodiscard]] time_t getTimeT() const override { return static_cast<time_t>(now.load() / 1000000); }
    };

    std::string scrape(uint16_t port) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            close(fd);
            return "";
        }
        const std::string request = "GET /metrics HTTP/1.0\r\nHost: localhost\r\n\r\n";
        [[maybe_unused]] const ssize_t written = write(fd, request.data(), request.size());
        std::string response;
        char buffer[4096];
        ssize_t r;
        while ((r = read(fd, buffer, sizeof(buffer))) > 0)
            response.append(buffer, r);
        close(fd);
        return response;
    }

    bool has(const std::string& text, const std::string& line) {
        return text.find("\n" + line + "\n") != std::string::npos;
    }
}

int main() {
    check("age: empty queue is 0", WriterQueueAge::ageS(0, 5000000000) == 0);
    check("age: clock behind the send time is 0", WriterQueueAge::ageS(5000000000, 4000000000) == 0);
    check("age: 2.5 s", WriterQueueAge::ageS(5000000000, 5002500000) == 2.5);

    Ctx ctx;
    delete ctx.clock;
    auto* clock = new TestClock();
    ctx.clock = clock;

    // Port 0 is not reported back by the exposer; find a free one
    uint16_t port = 0;
    {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        socklen_t length = sizeof(address);
        getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
        port = ntohs(address.sin_port);
        close(fd);
    }
    auto* metrics = new MetricsPrometheus(Metrics::TAG_NAMES::NONE, "127.0.0.1:" + std::to_string(port));
    ctx.metrics = metrics;
    metrics->initialize(&ctx);

    std::string text = scrape(port);
    check("scrape: before any message the gauges are 0", has(text, "writer_queue_size 0") && has(text, "writer_queue_held_positions 0") &&
          has(text, "writer_queue_oldest_age_s 0"));

    metrics->emitWriterQueueSize(3);
    metrics->emitWriterQueueHeldPositions(2);
    metrics->emitWriterQueueOldestSentUs(5000000000);
    clock->now = 5007000000;
    text = scrape(port);
    check("scrape: queue size", has(text, "writer_queue_size 3"));
    check("scrape: held positions", has(text, "writer_queue_held_positions 2"));
    check("scrape: age of the oldest message", has(text, "writer_queue_oldest_age_s 7"));
    check("scrape: types", text.find("# TYPE writer_queue_oldest_age_s gauge") != std::string::npos &&
          text.find("# TYPE writer_queue_size gauge") != std::string::npos);

    // Nothing is emitted: the age keeps growing
    clock->now = 5907000000;
    text = scrape(port);
    check("scrape: age grows between emits", has(text, "writer_queue_oldest_age_s 907"));

    metrics->emitWriterQueueOldestSentUs(0);
    metrics->emitWriterQueueSize(0);
    text = scrape(port);
    check("scrape: empty again", has(text, "writer_queue_oldest_age_s 0") && has(text, "writer_queue_size 0"));

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
