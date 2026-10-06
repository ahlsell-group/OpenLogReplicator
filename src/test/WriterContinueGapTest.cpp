/* Test for the detection of a CONTINUE that would skip messages released by CONFIRM
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

#include <iostream>
#include <string>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../writer/Writer.h"

// CONFIRM tells the writer that the client no longer needs the messages up to a position, and their buffers are
// released. A client which later continues from an earlier position (it confirms what it has read, then continues from
// what it has stored) gets only the messages still held. The writer has to notice the gap: the oldest held message
// comes after the message following the client's position. Message indexes are 1-based within an scn.
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

    class TestWriter final : public Writer {
    public:
        TestWriter(Ctx* newCtx, Builder* newBuilder, Metadata* newMetadata): Writer(newCtx, "test-writer", "TEST", newBuilder, newMetadata) {}

    protected:
        void sendMessage(BuilderMsg*) override {}
        std::string getType() const override { return "test"; }
        void pollQueue() override {}
    };

    constexpr Writer::HELD MESSAGE = Writer::HELD::MESSAGE;

    bool gap(uint64_t clientScn, typeIdx clientIdx, uint64_t confirmedScn, typeIdx confirmedIdx, Writer::HELD held, uint64_t oldestScn,
             typeIdx oldestIdx) {
        return Writer::isContinueGap(Scn(clientScn), clientIdx, confirmedScn == 0 ? Scn::none() : Scn(confirmedScn), confirmedIdx, held,
                                     Scn(oldestScn), oldestIdx);
    }
}

int main() {
    // One large transaction, all messages at scn 1000. The client stored 9999, read and confirmed 27309, OLR released
    // the buffers before the one starting with 25615: messages 10000 .. 25614 are lost on CONTINUE (1000, 9999).
    check("restart inside the transaction after a CONFIRM ahead of the stored position", gap(1000, 9999, 1000, 27309, MESSAGE, 1000, 25615));
    // The client stored a position in an earlier transaction (scn 900), the large transaction at 1000 is held from
    // message 7378 on: messages 1 .. 7377 of scn 1000 are lost.
    check("restart from an earlier transaction, the next one partly released", gap(900, 3, 1000, 9140, MESSAGE, 1000, 7378));
    // Every confirmed message was released and nothing is held
    check("nothing held after the confirmed position", gap(1000, 10, 1000, 20, Writer::HELD::NONE, 0, 0));
    // The confirmed message lies in an scn before the oldest held one
    check("confirmed scn released, oldest held starts a later scn", gap(900, 3, 950, 4, MESSAGE, 1000, 1));

    // No gap
    check("continue at the confirmed position", !gap(1000, 27309, 1000, 27309, MESSAGE, 1000, 25615));
    check("continue after the confirmed position", !gap(1000, 27400, 1000, 27309, MESSAGE, 1000, 25615));
    check("continue at a later scn than confirmed", !gap(1100, 1, 1000, 27309, MESSAGE, 1000, 25615));
    check("nothing confirmed yet", !gap(1000, 9999, 0, 0, MESSAGE, 1000, 25615));
    check("held from the message after the client's position", !gap(1000, 1651, 1000, 3000, MESSAGE, 1000, 1652));
    check("held from before the client's position", !gap(1000, 1651, 1000, 3000, MESSAGE, 1000, 1000));
    check("held from an earlier scn", !gap(1000, 1651, 1000, 3000, MESSAGE, 900, 4));
    check("oldest held starts the next scn, confirmed inside it", !gap(900, 3, 1000, 50, MESSAGE, 1000, 1));
    check("oldest held message not known", !gap(1000, 9999, 1000, 27309, Writer::HELD::UNKNOWN, 0, 0));
    check("client without a position", !Writer::isContinueGap(Scn::none(), 0, Scn(1000), 5, MESSAGE, Scn(1000), 7));

    // The oldest held message is read from the builder's first buffer
    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    Locales locales;
    Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
    Format format(Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                  Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                  Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                  Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                  Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                  Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT);
    BuilderJson builder(&ctx, &locales, &metadata, format, 0);
    builder.initialize();
    const TestWriter writer(&ctx, &builder, &metadata);

    Scn scn = Scn::none();
    typeIdx idx = 0;
    check("empty builder holds nothing", writer.oldestHeldMessage(scn, idx) == Writer::HELD::NONE);

    builder.processCheckpoint(Seq(1), Scn(500), Time(0), FileOffset(0), false);
    builder.processCheckpoint(Seq(1), Scn(500), Time(0), FileOffset(0), false);
    builder.processCheckpoint(Seq(1), Scn(600), Time(0), FileOffset(0), false);
    check("oldest held message is the first one", writer.oldestHeldMessage(scn, idx) == Writer::HELD::MESSAGE && scn == Scn(500) && idx == 1);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
