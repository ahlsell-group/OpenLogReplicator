/* Test for the character set conversion of DDL text in JSON output
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

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/RedoLogRecord.h"
#include "../common/Thread.h"
#include "../common/exception/RedoLogException.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// The SQL text of a DDL record is in the database character set. It was copied to the JSON output byte for byte, so in a
// WE8ISO8859P1 database a comment with "för" sent byte 0xF6, which is not UTF-8, and the client decoded it as U+FFFD.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        // The "sql" value of the DDL message the builder writes for this redo text
        static std::string ddlJson(BuilderJson& builder, const std::string& sql) {
            builder.releaseDdl();
            builder.appendDdlChunk(reinterpret_cast<const uint8_t*>(sql.data()), sql.size());
            builder.ddlToValueBuffer(FileOffset(0));
            // builderCommit clears msg, so take the header where builderBegin will put it
            const auto* message = reinterpret_cast<const BuilderMsg*>(builder.lastBuilderQueue->data + builder.lastBuilderSize);
            static_cast<Builder&>(builder).processDdl(Seq::zero(), Scn::zero(), Time(0), nullptr, 1);
            std::string json(reinterpret_cast<const char*>(message->data), message->size);
            builder.releaseDdl();
            return json;
        }

        // The JSON message the builder writes for these 0x18010000 redo records
        static std::string ddlRedoJson(BuilderJson& builder, const std::vector<const RedoLogRecord*>& records) {
            builder.releaseDdl();
            const uint64_t before = builder.lastBuilderSize;
            const auto* message = reinterpret_cast<const BuilderMsg*>(builder.lastBuilderQueue->data + before);
            for (const RedoLogRecord* record: records)
                static_cast<Builder&>(builder).processDdl(Seq::zero(), Scn::zero(), Time(0), record);
            if (builder.lastBuilderSize == before)
                return "";
            return {reinterpret_cast<const char*>(message->data), message->size};
        }

        static uint64_t ddlChunkCount(const Builder& builder) {
            uint64_t count = 0;
            for (const uint8_t* chunk = builder.ddlFirst; chunk != nullptr; chunk = *reinterpret_cast<uint8_t* const*>(chunk))
                ++count;
            return count;
        }

        static std::string ddlConverted(Builder& builder, const std::string& sql, uint64_t& chunks) {
            builder.releaseDdl();
            builder.appendDdlChunk(reinterpret_cast<const uint8_t*>(sql.data()), sql.size());
            chunks = ddlChunkCount(builder);
            builder.ddlToValueBuffer(FileOffset(0));
            std::string text(builder.valueBuffer, builder.valueSize);
            builder.releaseDdl();
            return text;
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

    // A little-endian 0x18010000 record: field 1 carries seq and cnt, then the given fields
    class DdlRecord {
    public:
        OpenLogReplicator::RedoLogRecord record{};

        DdlRecord(uint16_t seq, uint16_t cnt, const std::vector<std::string>& fields) {
            std::string field1(24, '\0');
            field1[18] = static_cast<char>(seq & 0xFF);
            field1[19] = static_cast<char>(seq >> 8);
            field1[20] = static_cast<char>(cnt & 0xFF);
            field1[21] = static_cast<char>(cnt >> 8);
            std::vector<std::string> all{field1};
            all.insert(all.end(), fields.begin(), fields.end());

            const uint64_t headerSize = ((all.size() + 1) * 2 + 3) & ~3ULL;
            data.assign(headerSize, 0);
            for (uint64_t i = 0; i < all.size(); ++i) {
                data[(i + 1) * 2] = static_cast<uint8_t>(all[i].size() & 0xFF);
                data[(i + 1) * 2 + 1] = static_cast<uint8_t>(all[i].size() >> 8);
                data.insert(data.end(), all[i].begin(), all[i].end());
                data.resize((data.size() + 3) & ~3ULL, 0);
            }

            record.clear();
            record.dataExt = data.data();
            record.obj = 1;
            record.fieldCnt = static_cast<typeField>(all.size());
            record.fieldSizesDelta = 0;
            record.fieldPos = static_cast<typePos>(headerSize);
            record.size = static_cast<uint32_t>(data.size());
        }

    private:
        std::vector<uint8_t> data;
    };

    // The DDL message parses as JSON and its "sql" is the expected UTF-8 text
    void checkSql(const std::string& label, const std::string& json, const std::string& expected) {

        rapidjson::Document document;
        document.Parse(json.c_str(), json.size());
        const bool parsed = !document.HasParseError() && document.IsObject() && document.HasMember("payload") &&
                document["payload"].IsArray() && document["payload"].Size() == 1 && document["payload"][0].HasMember("sql") &&
                document["payload"][0]["sql"].IsString();
        if (!parsed)
            std::cerr << "     " << json << "\n";
        check(label + ": the message is valid JSON", parsed);
        if (!parsed)
            return;

        const std::string sql(document["payload"][0]["sql"].GetString(), document["payload"][0]["sql"].GetStringLength());
        if (sql != expected)
            std::cerr << "     expected " << expected << ", got " << sql << "\n";
        check(label + ": sql is the text in UTF-8", sql == expected);
    }

    void ddlMessage(OpenLogReplicator::BuilderJson& builder, const std::string& label, const std::string& redo, const std::string& expected) {
        checkSql(label, OpenLogReplicator::BuilderTestAccess::ddlJson(builder, redo), expected);
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
    ctx.flags = static_cast<uint>(Ctx::REDO_FLAGS::SHOW_DDL);

    // WE8ISO8859P1: one byte per character, 0xF6 is ö
    metadata.defaultCharacterMapId = 31;
    ddlMessage(builder, "WE8ISO8859P1 comment", "COMMENT ON COLUMN EHDA.T.C IS 'f\xF6r \xC5\xC4\xD6'",
               "COMMENT ON COLUMN EHDA.T.C IS 'f\xC3\xB6r \xC3\x85\xC3\x84\xC3\x96'");
    ddlMessage(builder, "WE8ISO8859P1 quote and newline", "ALTER TABLE T ADD (\"K\xD6\" NUMBER)\n",
               "ALTER TABLE T ADD (\"K\xC3\x96\" NUMBER)\n");
    ddlMessage(builder, "ASCII", "TRUNCATE TABLE EHDA.T", "TRUNCATE TABLE EHDA.T");

    // The redo-record path: a DDL that fits in one record has its text in field 8, a longer one in field 2 of each later record
    const std::string latinSql = "COMMENT ON COLUMN EHDA.T.C IS 'f\xF6r \xC5\xC4\xD6'";
    const std::string utf8Sql = "COMMENT ON COLUMN EHDA.T.C IS 'f\xC3\xB6r \xC3\x85\xC3\x84\xC3\x96'";
    const DdlRecord single(1, 1, {"EHDA", "", "", "", "", "", latinSql + std::string(1, '\0')});
    checkSql("one redo record", BuilderTestAccess::ddlRedoJson(builder, {&single.record}), utf8Sql);
    const DdlRecord first(1, 2, {"EHDA"});
    const DdlRecord second(2, 2, {latinSql});
    checkSql("two redo records", BuilderTestAccess::ddlRedoJson(builder, {&first.record, &second.record}), utf8Sql);

    // AL32UTF8: the text is already UTF-8 and passes unchanged, also when a character is split between two DDL chunks
    metadata.defaultCharacterMapId = 873;
    ddlMessage(builder, "AL32UTF8 comment", "COMMENT ON TABLE T IS 'f\xC3\xB6r'", "COMMENT ON TABLE T IS 'f\xC3\xB6r'");

    const uint64_t chunkCapacity = Ctx::MEMORY_CHUNK_SIZE - sizeof(uint8_t*) - sizeof(uint64_t);
    const std::string head = "COMMENT ON TABLE T IS '" + std::string(chunkCapacity - 24, 'a');
    const std::string split = head + "\xC3\xB6" + "'";
    uint64_t chunks = 0;
    const std::string converted = BuilderTestAccess::ddlConverted(builder, split, chunks);
    check("AL32UTF8 split: the text spans two chunks", chunks == 2);
    check("AL32UTF8 split: the character across the chunk boundary is kept", converted == split);

    // No known character set (schemaless mode without a schema file): the text is copied as it is, it does not throw
    metadata.defaultCharacterMapId = 0;
    try {
        ddlMessage(builder, "no character set ASCII", "TRUNCATE TABLE EHDA.T", "TRUNCATE TABLE EHDA.T");
        ddlMessage(builder, "no character set raw byte", "COMMENT ON TABLE T IS 'f\xF6r'", "COMMENT ON TABLE T IS 'f\xF6r'");
        checkSql("no character set one redo record", BuilderTestAccess::ddlRedoJson(builder, {&single.record}), latinSql);
    } catch (const RedoLogException& e) {
        std::cerr << "     " << e.msg << "\n";
        check("no character set: the DDL does not throw", false);
    }

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
