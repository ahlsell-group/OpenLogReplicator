/* Test that a checkpoint not written to the buffers leaves every position as a written one does
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
#include <memory>
#include <string>
#include <vector>

#include "../builder/BuilderJson.h"
#ifdef LINK_LIBRARY_PROTOBUF
#include "../builder/BuilderProtobuf.h"
#endif
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// Without SHOW_CHECKPOINT a checkpoint is never sent. The builder keeps only its position instead of writing it, and
// must leave its counters as writing it does: the position (c_scn, c_idx) and id of every later message, and the
// builder's own index the parser reads, have to be the same. Two builders, one with SHOW_CHECKPOINT and one without,
// get the same input; every data message must be byte for byte the same and every hidden checkpoint must have the
// position and id of the written one.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        static void transaction(Builder& builder, uint32_t xid, Scn beginScn, Scn commitScn, const AttributeMap* attributes) {
            builder.processBegin(Xid(1, 1, xid), 1, Seq(1), beginScn, Time(0), Seq(1), commitScn, Time(0), attributes);
            builder.processBeginMessage(Seq(1), Time(0));
            builder.processCommit();
        }

        static uint64_t nextId(const Builder& builder) {
            return builder.id;
        }

        static const std::deque<HiddenCheckpoint>& hidden(const Builder& builder) {
            return builder.hiddenCheckpoints;
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

    struct Message {
        uint64_t id;
        Scn lwnScn;
        typeIdx lwnIdx;
        bool checkpoint;
        bool redo;
        std::string data;
    };

    // Reads every message written since the last call, merging the parts of a message split over buffers
    struct Reader {
        BuilderQueue* queue{nullptr};
        uint64_t offset{0};

        std::vector<Message> read(const Builder& builder) {
            std::vector<Message> messages;
            if (queue == nullptr)
                queue = builder.firstBuilderQueue;
            while (true) {
                if (offset == queue->confirmedSize && queue->next != nullptr) {
                    queue = queue->next;
                    offset = 0;
                }
                if (offset + sizeof(BuilderMsg) >= queue->confirmedSize)
                    break;
                const auto* msg = reinterpret_cast<const BuilderMsg*>(queue->data + offset);
                if (msg->size == 0)
                    break;
                Message m{msg->id, msg->lwnScn, msg->lwnIdx, msg->isFlagSet(BuilderMsg::OUTPUT_BUFFER::CHECKPOINT),
                          msg->isFlagSet(BuilderMsg::OUTPUT_BUFFER::REDO), {}};
                offset += sizeof(BuilderMsg);
                const uint64_t size8 = (msg->size + 7) & 0xFFFFFFFFFFFFFFF8;
                if (offset + size8 <= Builder::OUTPUT_BUFFER_DATA_SIZE) {
                    m.data.assign(reinterpret_cast<const char*>(queue->data + offset), msg->size);
                    offset += size8;
                } else {
                    while (m.data.size() < msg->size) {
                        const uint64_t part = std::min<uint64_t>(msg->size - m.data.size(), queue->confirmedSize - offset);
                        m.data.append(reinterpret_cast<const char*>(queue->data + offset), part);
                        if (m.data.size() < msg->size) {
                            queue = queue->next;
                            offset = 0;
                        } else
                            offset += (part + 7) & 0xFFFFFFFFFFFFFFF8;
                    }
                }
                messages.push_back(std::move(m));
            }
            return messages;
        }
    };

    Format makeFormat() {
        return {Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT};
    }

    struct Side {
        Ctx ctx;
        TestThread thread{&ctx};
        Locales locales;
        Format format{makeFormat()};
        std::unique_ptr<Metadata> metadata;
        std::unique_ptr<Builder> builder;
        Reader reader;

        template<typename B>
        void init(bool show) {
            ctx.parserThread = &thread;
            ctx.initialize(32, 512, 16, 16, 0, 0, 256, 16);
            if (show)
                ctx.flags |= static_cast<uint>(Ctx::REDO_FLAGS::SHOW_CHECKPOINT);
            metadata = std::make_unique<Metadata>(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
            builder = std::make_unique<B>(&ctx, &locales, metadata.get(), format, 0);
            builder->initialize();
        }
    };

    template<typename B>
    void compare(const std::string& type) {
        Side shown;
        Side hidden;
        shown.init<B>(true);
        hidden.init<B>(false);
        AttributeMap attributes;
        attributes[Attribute::KEY::LOGIN_USER_NAME] = "TEST";

        bool counters = true;
        bool checkpoints = true;
        bool data = true;
        uint64_t dataMessages = 0;
        uint64_t hiddenCount = 0;
        auto same = [&] {
            if (shown.builder->lwnScn != hidden.builder->lwnScn || shown.builder->lwnIdx != hidden.builder->lwnIdx ||
                BuilderTestAccess::nextId(*shown.builder) != BuilderTestAccess::nextId(*hidden.builder))
                counters = false;
        };
        auto checkpoint = [&](uint64_t scn, bool redo) {
            shown.builder->processCheckpoint(Seq(1), Scn(scn), Time(0), FileOffset(0), redo);
            hidden.builder->processCheckpoint(Seq(1), Scn(scn), Time(0), FileOffset(0), redo);
            ++hiddenCount;
            same();
            const std::vector<Message> written = shown.reader.read(*shown.builder);
            const auto& kept = BuilderTestAccess::hidden(*hidden.builder);
            if (written.size() != 1 || !written[0].checkpoint || kept.empty() || hidden.reader.read(*hidden.builder).size() != 0 ||
                kept.back().id != written[0].id || kept.back().lwnScn != written[0].lwnScn || kept.back().lwnIdx != written[0].lwnIdx ||
                (redo && !kept.back().redo))
                checkpoints = false;
        };
        auto transaction = [&](uint32_t xid, uint64_t beginScn, uint64_t commitScn) {
            BuilderTestAccess::transaction(*shown.builder, xid, Scn(beginScn), Scn(commitScn), &attributes);
            BuilderTestAccess::transaction(*hidden.builder, xid, Scn(beginScn), Scn(commitScn), &attributes);
            same();
            const std::vector<Message> a = shown.reader.read(*shown.builder);
            const std::vector<Message> b = hidden.reader.read(*hidden.builder);
            if (a.size() != 2 || b.size() != 2)
                data = false;
            for (uint64_t i = 0; i < a.size() && i < b.size(); ++i) {
                if (a[i].id != b[i].id || a[i].lwnScn != b[i].lwnScn || a[i].lwnIdx != b[i].lwnIdx || a[i].data != b[i].data ||
                    a[i].checkpoint || b[i].checkpoint)
                    data = false;
                ++dataMessages;
            }
        };

        // Moved and not moved checkpoints, commits at a higher, the same and a lower SCN than the position, a log switch
        checkpoint(500, false);
        checkpoint(500, false);
        transaction(1, 495, 510);
        checkpoint(505, false);
        checkpoint(520, true);
        transaction(2, 515, 520);
        transaction(3, 512, 515);
        checkpoint(519, false);
        for (uint64_t scn = 530; scn < 540; ++scn)
            checkpoint(scn, false);
        transaction(4, 535, 541);
        transaction(5, 536, 541);
        checkpoint(541, false);
        checkpoint(542, false);
        // Enough messages for the written checkpoints to fill several buffers, so the two builders rotate at other points
        for (uint64_t scn = 1000; scn < 31000; ++scn) {
            checkpoint(scn, scn % 997 == 0);
            if (scn % 7 == 0)
                transaction(static_cast<uint32_t>(scn), scn - 3, scn + 1);
        }

        check(type + ": position and id counters after every step are the same", counters);
        check(type + ": every hidden checkpoint has the id and position of the written one", checkpoints);
        check(type + ": every data message is the same, byte for byte (" + std::to_string(dataMessages) + ")", data && dataMessages > 8000);
        check(type + ": the written checkpoints filled several buffers", shown.builder->lastBuilderQueue->id >= 3);
        check(type + ": hidden checkpoints take no buffer space", hidden.builder->lastBuilderQueue->id < shown.builder->lastBuilderQueue->id);
        // Nothing read them: one kept per run between data messages, merged within a run
        const auto& kept = BuilderTestAccess::hidden(*hidden.builder);
        uint64_t merged = 0;
        bool sorted = true;
        for (uint64_t i = 0; i < kept.size(); ++i) {
            merged += kept[i].count;
            if (i > 0 && kept[i - 1].id >= kept[i].id)
                sorted = false;
        }
        check(type + ": hidden checkpoints merged within a run, in id order", merged == hiddenCount && sorted && kept.size() < hiddenCount / 2);
        check(type + ": first id published", hidden.builder->hiddenFirstId == kept.front().id);
        check(type + ": end id published", hidden.builder->hiddenEndId == kept.back().id + 1);
    }

    void constantMemory() {
        Side hidden;
        hidden.init<BuilderJson>(false);
        const uint64_t buffers = hidden.builder->buffersAllocated;
        for (uint64_t scn = 1000; scn < 1001000; ++scn)
            hidden.builder->processCheckpoint(Seq(1), Scn(scn), Time(0), FileOffset(0), false);
        const auto& kept = BuilderTestAccess::hidden(*hidden.builder);
        check("1M hidden checkpoints use no builder buffer", hidden.builder->buffersAllocated == buffers &&
              hidden.builder->lastBuilderQueue->confirmedSize == 0);
        check("1M hidden checkpoints with no message between them are one entry", kept.size() == 1 && kept.back().count == 1000000 &&
              kept.back().lwnScn == Scn(1000999) && kept.back().lwnIdx == 1);
    }

    void cap() {
        Side hidden;
        hidden.init<BuilderJson>(false);
        AttributeMap attributes;
        attributes[Attribute::KEY::LOGIN_USER_NAME] = "TEST";
        // A writer that reads nothing: one entry per run until the cap, then the newest absorbs the rest
        for (uint64_t scn = 1000; scn < 1000 + 70000 * 4; scn += 4) {
            hidden.builder->processCheckpoint(Seq(1), Scn(scn), Time(0), FileOffset(0), false);
            BuilderTestAccess::transaction(*hidden.builder, static_cast<uint32_t>(scn), Scn(scn), Scn(scn + 1), &attributes);
        }
        hidden.builder->processCheckpoint(Seq(1), Scn(1000 + 70000 * 4), Time(0), FileOffset(0), false);
        const auto& kept = BuilderTestAccess::hidden(*hidden.builder);
        check("entries capped while the writer reads nothing", kept.size() == 65536 && kept.back().lwnScn == Scn(1000 + 70000 * 4) &&
              kept.back().id + 1 == BuilderTestAccess::nextId(*hidden.builder));
    }
}

int main() {
    compare<BuilderJson>("json");
#ifdef LINK_LIBRARY_PROTOBUF
    compare<BuilderProtobuf>("protobuf");
#endif
    constantMemory();
    cap();

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
