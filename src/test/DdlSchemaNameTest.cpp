/* Test for the owner name of a DDL record (OP 24.1)
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

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/RedoLogRecord.h"
#include "../common/Thread.h"
#include "../common/table/SysUser.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// The first DDL record (seq 1) carries the owner name in field 2, which is copied into a buffer of SysUser::NAME_LENGTH
// bytes. A longer field from damaged redo must not be copied past the end of that buffer.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        static uint64_t schemaSize(const Builder& builder) {
            return builder.ddlSchemaSize;
        }

        static std::string schemaName(const Builder& builder) {
            return {builder.ddlSchemaName, builder.ddlSchemaSize};
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

    // Change vector built from its fields: the field size array first (one unused slot, then one 16-bit size per
    // field), then the fields, each padded to 4 bytes, the way RedoLogRecord::nextField walks them.
    class Vector {
    public:
        std::vector<uint8_t> bytes;
        OpenLogReplicator::RedoLogRecord record{};

        explicit Vector(const std::vector<std::vector<uint8_t>>& fields) {
            const size_t sizesBytes = (fields.size() + 1) * 2;
            const size_t fieldPos = (sizesBytes + 3) & ~static_cast<size_t>(3);
            bytes.assign(fieldPos, 0);
            for (size_t i = 0; i < fields.size(); ++i) {
                const auto size = static_cast<uint16_t>(fields[i].size());
                bytes[(i + 1) * 2] = static_cast<uint8_t>(size & 0xFF);
                bytes[(i + 1) * 2 + 1] = static_cast<uint8_t>(size >> 8);
                bytes.insert(bytes.end(), fields[i].begin(), fields[i].end());
                bytes.resize((bytes.size() + 3) & ~static_cast<size_t>(3), 0);
            }
            record.dataExt = bytes.data();
            record.size = static_cast<uint32_t>(bytes.size());
            record.fieldCnt = static_cast<uint16_t>(fields.size());
            record.fieldSizesDelta = 0;
            record.fieldPos = static_cast<uint16_t>(fieldPos);
            record.opCode = 0x1801;
            record.obj = 1;
        }
    };

    // First record of a DDL split into two: field 1 with the DDL type and seq 1 of 2, field 2 with the owner name
    Vector firstDdlRecord(const std::string& owner) {
        std::vector<uint8_t> header(24, 0);
        header[12] = 15;                                                     // ddl type: alter table
        header[18] = 1;                                                      // seq
        header[20] = 2;                                                      // cnt
        return Vector({header, std::vector<uint8_t>(owner.begin(), owner.end())});
    }

    void ownerName(OpenLogReplicator::Builder& builder, const std::string& label, const std::string& owner, const std::string& expected) {
        Vector v = firstDdlRecord(owner);
        builder.processDdl(OpenLogReplicator::Seq::zero(), OpenLogReplicator::Scn::zero(), OpenLogReplicator::Time(0), &v.record);
        const uint64_t size = OpenLogReplicator::BuilderTestAccess::schemaSize(builder);
        check(label + ": the stored length fits the buffer", size <= OpenLogReplicator::SysUser::NAME_LENGTH);
        if (size > OpenLogReplicator::SysUser::NAME_LENGTH)
            return;
        const std::string name = OpenLogReplicator::BuilderTestAccess::schemaName(builder);
        if (name != expected)
            std::cerr << "     expected \"" << expected << "\", got \"" << name << "\"\n";
        check(label + ": the stored owner", name == expected);
    }
}

int main() {
    using namespace OpenLogReplicator;

    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    ctx.flags = static_cast<uint>(Ctx::REDO_FLAGS::SHOW_DDL);
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

    ownerName(builder, "short owner", "EHDA", "EHDA");
    const std::string longest(SysUser::NAME_LENGTH, 'A');
    ownerName(builder, "owner of 128 bytes", longest, longest);
    // Longer than any Oracle name: the owner is left out of the message instead of being cut to a wrong name
    ownerName(builder, "owner of 200 bytes", std::string(200, 'B'), "");
    ownerName(builder, "short owner after a long one", "EHDA", "EHDA");

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
