/* Test that a shutdown wakes a thread waiting for memory with an exception, not nullptr
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

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

#include "../common/Ctx.h"
#include "../common/Thread.h"
#include "../common/exception/RuntimeException.h"

// The parser and builder dereference the chunk they get; a shutdown while they wait for memory must not hand them nullptr
namespace {
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    class TestThread final : public OpenLogReplicator::Thread {
    public:
        explicit TestThread(OpenLogReplicator::Ctx* newCtx): Thread(newCtx, "test") {}
        void run() override {}
        std::string getName() const override { return "test"; }
    };

    enum class RESULT : unsigned char { CHUNK, NULLPTR, SHUTDOWN, OTHER };

    // Takes chunks until the call blocks and is woken by the shutdown
    RESULT exhaust(OpenLogReplicator::Ctx* ctx, OpenLogReplicator::Thread* t, OpenLogReplicator::Ctx::MEMORY module, bool swap) {
        try {
            for (int i = 0; i < 1000; ++i) {
                if (ctx->getMemoryChunk(t, module, swap) == nullptr)
                    return RESULT::NULLPTR;
            }
            return RESULT::CHUNK;
        } catch (OpenLogReplicator::RuntimeException& ex) {
            return ex.code == 10018 ? RESULT::SHUTDOWN : RESULT::OTHER;
        }
    }

    RESULT runUntilShutdown(OpenLogReplicator::Ctx::MEMORY module, bool swap) {
        OpenLogReplicator::Ctx ctx;
        TestThread thread(&ctx);
        ctx.initialize(8, 8, 0, 0, 0, 0, 8, 0);

        auto waiter = std::async(std::launch::async, exhaust, &ctx, &thread, module, swap);
        if (waiter.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready)
            return waiter.get();
        ctx.stopHard(false);
        if (waiter.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            std::cerr << "waiter not woken by the shutdown\n";
            std::_Exit(1);
        }
        return waiter.get();
    }
}

int main() {
    using namespace OpenLogReplicator;

    check("parser waiting for memory gets 10018 at shutdown", runUntilShutdown(Ctx::MEMORY::PARSER, false) == RESULT::SHUTDOWN);
    check("builder waiting for memory gets 10018 at shutdown", runUntilShutdown(Ctx::MEMORY::BUILDER, false) == RESULT::SHUTDOWN);
    check("swap caller waiting for memory gets nullptr at shutdown", runUntilShutdown(Ctx::MEMORY::TRANSACTIONS, true) == RESULT::NULLPTR);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
