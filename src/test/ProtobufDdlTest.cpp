/* Test for the DDL text in Protobuf output
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

#include <algorithm>
#include <iostream>
#include <string>

#include "../common/DbColumn.h"
#include "../builder/BuilderProtobuf.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../metadata/Schema.h"

// The "ddl" field of a Protobuf DDL message must hold the whole SQL text as UTF-8, like "sql" in JSON
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        // Parses the DDL message the builder writes for this redo text; false when it does not parse
        static bool ddlMessage(BuilderProtobuf& builder, const std::string& sql, pb::RedoResponse& response, uint64_t& chunks) {
            builder.releaseDdl();
            builder.appendDdlChunk(reinterpret_cast<const uint8_t*>(sql.data()), sql.size());
            chunks = 0;
            for (const uint8_t* chunk = builder.ddlFirst; chunk != nullptr; chunk = *reinterpret_cast<uint8_t* const*>(chunk))
                ++chunks;
            builder.ddlToValueBuffer(FileOffset(0));
            // builderCommit clears msg, so take the header where builderBegin will put it
            const BuilderQueue* queue = builder.lastBuilderQueue;
            uint64_t offset = builder.lastBuilderSize + sizeof(BuilderMsg);
            const auto* message = reinterpret_cast<const BuilderMsg*>(queue->data + builder.lastBuilderSize);
            static_cast<Builder&>(builder).processDdl(Seq::zero(), Scn::zero(), Time(0), nullptr, 1);
            // builderRotate moved the message to the start of the next buffer, leaving the old header with size 0
            if (message->size == 0 && queue->next != nullptr) {
                queue = queue->next;
                offset = sizeof(BuilderMsg);
                message = reinterpret_cast<const BuilderMsg*>(queue->data);
            }

            // A message larger than one builder buffer continues in the next ones, as the writer reads it
            std::string data;
            uint64_t end = queue->confirmedSize;
            while (queue != nullptr && data.size() < message->size) {
                const uint64_t part = std::min<uint64_t>(message->size - data.size(), end - offset);
                data.append(reinterpret_cast<const char*>(queue->data + offset), part);
                queue = queue->next;
                offset = 0;
                end = Builder::OUTPUT_BUFFER_DATA_SIZE;
            }
            const bool parsed = response.ParseFromString(data);
            builder.releaseDdl();
            return parsed;
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

    void ddlMessage(OpenLogReplicator::BuilderProtobuf& builder, const std::string& label, const std::string& redo,
                    const std::string& expected, uint64_t expectedChunks) {
        OpenLogReplicator::pb::RedoResponse response;
        uint64_t chunks = 0;
        const bool parsed = OpenLogReplicator::BuilderTestAccess::ddlMessage(builder, redo, response, chunks);
        check(label + ": the text spans " + std::to_string(expectedChunks) + " chunk(s)", chunks == expectedChunks);
        check(label + ": the message parses with one DDL payload", parsed && response.payload_size() == 1 &&
              response.payload(0).op() == OpenLogReplicator::pb::DDL);
        if (!parsed || response.payload_size() != 1)
            return;

        const std::string& ddl = response.payload(0).ddl();
        if (ddl.size() != expected.size())
            std::cerr << "     expected " << expected.size() << " bytes, got " << ddl.size() << "\n";
        check(label + ": ddl is the whole text in UTF-8", ddl == expected);
        check(label + ": schema names the object", response.payload(0).schema().name() == "OBJ_1");
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
    BuilderProtobuf builder(&ctx, &locales, &metadata, format, 0);
    builder.initialize();

    // WE8ISO8859P1: one byte per character, 0xF6 is ö
    metadata.defaultCharacterMapId = 31;
    ddlMessage(builder, "WE8ISO8859P1 comment", "COMMENT ON COLUMN EHDA.T.C IS 'f\xF6r \xC5\xC4\xD6'",
               "COMMENT ON COLUMN EHDA.T.C IS 'f\xC3\xB6r \xC3\x85\xC3\x84\xC3\x96'", 1);
    ddlMessage(builder, "ASCII", "TRUNCATE TABLE EHDA.T", "TRUNCATE TABLE EHDA.T", 1);

    // A text longer than one DDL chunk is sent whole
    const uint64_t chunkCapacity = Ctx::MEMORY_CHUNK_SIZE - sizeof(uint8_t*) - sizeof(uint64_t);
    const std::string longSql = "COMMENT ON TABLE T IS '" + std::string(chunkCapacity, 'a') + "'";
    ddlMessage(builder, "two chunks", longSql, longSql, 2);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
