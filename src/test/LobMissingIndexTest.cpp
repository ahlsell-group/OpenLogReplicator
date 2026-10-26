/* Test for a LOB locator whose LOB index is not in the transaction
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
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/LobCtx.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../common/types/LobId.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// An out-of-row LOB locator without a LOB index in the transaction must leave the column out (parseLob returns false)
// and log warning 60050, instead of sending "" or an empty RAW that would overwrite the real value downstream.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        static bool parseLob(Builder& builder, LobCtx& lobCtx, const std::vector<uint8_t>& data, bool isClob, uint64_t& valueSize, typeObj obj) {
            const bool ok = builder.parseLob(&lobCtx, data.data(), data.size(), isClob ? 31 : 0, obj, FileOffset(4096), isClob, false);
            valueSize = builder.valueSize;
            return ok;
        }

        static void setCommitTimestamp(Builder& builder, uint32_t time) {
            builder.commitTimestamp = Time(time);
        }
    };
}

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

    const uint8_t LOB_ID[10] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78};

    // Out-of-row locator (flag 0x04 clear): the data is only reachable through the LOB index
    std::vector<uint8_t> outOfRowLocator() {
        std::vector<uint8_t> data(40, 0);
        data[5] = 0x00;
        for (int i = 0; i < 10; ++i)
            data[10 + i] = LOB_ID[i];
        return data;
    }

    // In-row locator with the value stored in the locator itself
    std::vector<uint8_t> inValueLocator(const std::string& value) {
        const uint16_t bodySize = static_cast<uint16_t>(16 + value.size());
        std::vector<uint8_t> data(36 + value.size(), 0);
        data[5] = 0x04;
        for (int i = 0; i < 10; ++i)
            data[10 + i] = LOB_ID[i];
        data[20] = static_cast<uint8_t>(bodySize >> 8);
        data[21] = static_cast<uint8_t>(bodySize & 0xFF);
        data[22] = 0x01; // flg2 0x0100: in-value
        data[28] = static_cast<uint8_t>(value.size() >> 8);
        data[29] = static_cast<uint8_t>(value.size() & 0xFF);
        for (size_t i = 0; i < value.size(); ++i)
            data[36 + i] = static_cast<uint8_t>(value[i]);
        return data;
    }

    // Runs parseLob with stderr captured, so the warning it logs can be checked
    bool parseLobCaptured(OpenLogReplicator::Builder& builder, OpenLogReplicator::LobCtx& lobCtx, const std::vector<uint8_t>& data, bool isClob,
                          uint64_t& valueSize, std::string& log, typeObj obj = 12345) {
        std::ostringstream captured;
        std::streambuf* old = std::cerr.rdbuf(captured.rdbuf());
        const bool ok = OpenLogReplicator::BuilderTestAccess::parseLob(builder, lobCtx, data, isClob, valueSize, obj);
        std::cerr.rdbuf(old);
        log = captured.str();
        return ok;
    }
}

int main() {
    using namespace OpenLogReplicator;

    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    Locales locales;
    locales.initialize();
    Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
    Format format(Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                  Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                  Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                  Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                  Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                  Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT);
    BuilderJson builder(&ctx, &locales, &metadata, format, 0);
    builder.initialize();

    std::map<LobKey, uint8_t*> orphanedLobs;
    LobCtx lobCtx;
    lobCtx.orphanedLobs = &orphanedLobs;
    const std::string lobIdText = LobId(LOB_ID).lower();

    uint64_t valueSize = 0;
    std::string log;

    // Out-of-row CLOB, LOB index missing: the column is left out, not sent as ""
    bool ok = parseLobCaptured(builder, lobCtx, outOfRowLocator(), true, valueSize, log);
    check("CLOB without LOB index: parseLob returns false (column left out)", !ok);
    check("CLOB without LOB index: warning 60050 is logged", log.find("60050") != std::string::npos);
    check("CLOB without LOB index: warning names the LOB id", log.find(lobIdText) != std::string::npos);
    check("CLOB without LOB index: warning names the file offset", log.find("offset: 4096") != std::string::npos);
    if (!log.empty())
        std::cout << "     log: " << log;

    // Out-of-row BLOB, LOB index missing: the column is left out, not sent as an empty RAW
    ok = parseLobCaptured(builder, lobCtx, outOfRowLocator(), false, valueSize, log, 12346);
    check("BLOB without LOB index: parseLob returns false (column left out)", !ok);
    check("BLOB without LOB index: warning 60050 is logged", log.find("60050") != std::string::npos);

    // Repeats for the same table within 60 s of commit time are counted, not logged
    int warnings = 0;
    bool allLeftOut = true;
    for (int i = 0; i < 1000; ++i) {
        allLeftOut &= !parseLobCaptured(builder, lobCtx, outOfRowLocator(), true, valueSize, log);
        if (log.find("60050") != std::string::npos)
            ++warnings;
    }
    check("repeated missing LOB index: every column left out", allLeftOut);
    check("repeated missing LOB index within 60 s: no further warning", warnings == 0);

    // 60 s of commit time later the next one is logged in full, with the count left out
    BuilderTestAccess::setCommitTimestamp(builder, 60);
    ok = parseLobCaptured(builder, lobCtx, outOfRowLocator(), true, valueSize, log);
    check("missing LOB index after 60 s: column left out", !ok);
    check("missing LOB index after 60 s: warning 60050 is logged", log.find("60050") != std::string::npos);
    check("missing LOB index after 60 s: warning names the LOB id", log.find(lobIdText) != std::string::npos);
    check("missing LOB index after 60 s: warning counts the suppressed ones",
          log.find("suppressed since last warning: 1000") != std::string::npos);
    if (!log.empty())
        std::cout << "     log: " << log;
    ok = parseLobCaptured(builder, lobCtx, outOfRowLocator(), true, valueSize, log);
    check("missing LOB index right after: no warning", log.empty());

    // Another table is not held back by the first one
    ok = parseLobCaptured(builder, lobCtx, outOfRowLocator(), true, valueSize, log, 54321);
    check("missing LOB index for another table: warning 60050 is logged", log.find("60050") != std::string::npos);
    check("missing LOB index for another table: no suppressed count", log.find("suppressed") == std::string::npos);

    // An empty in-row LOB is still an empty value
    ok = parseLobCaptured(builder, lobCtx, inValueLocator(""), false, valueSize, log);
    check("empty in-row BLOB: parseLob returns true", ok);
    check("empty in-row BLOB: value is empty", valueSize == 0);
    check("empty in-row BLOB: no warning", log.empty());

    // An in-row LOB with data still decodes
    ok = parseLobCaptured(builder, lobCtx, inValueLocator("abc"), false, valueSize, log);
    check("in-row BLOB with data: parseLob returns true", ok);
    check("in-row BLOB with data: value has 3 bytes", valueSize == 3);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
