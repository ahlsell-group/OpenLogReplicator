/* Redo Log OP Code 19.1
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

#ifndef OP_CODE_13_01_H_
#define OP_CODE_13_01_H_

#include "../common/RedoLogRecord.h"
#include "OpCode.h"

namespace OpenLogReplicator {
    class OpCode1301 final : public OpCode {
    public:
        static constexpr uint8_t BLOCK_TYPE_DATA = 0x06;
        static constexpr uint8_t KTBBH_TYPE_DATA = 0x01;

        static void process1301(const Ctx* ctx, RedoLogRecord* redoLogRecord) {
            typePos fieldPos = 0;
            typeField fieldNum = 0;
            typeSize fieldSize = 0;

            RedoLogRecord::nextField(ctx, redoLogRecord, fieldNum, fieldPos, fieldSize, 0x130101);
            // Field: 1
            const typePos blockPos = fieldPos;
            const typeSize blockSize = fieldSize;

            if (unlikely(fieldSize < 36))
                throw RedoLogException(50061, "too short field 19.1.1: " + std::to_string(fieldSize) + " offset: " + redoLogRecord->fileOffset.toString());

            redoLogRecord->dataObj = ctx->read32(redoLogRecord->data(fieldPos + 0));
            redoLogRecord->recordDataObj = redoLogRecord->dataObj;
            redoLogRecord->lobId.set(redoLogRecord->data(fieldPos + 4));
            redoLogRecord->lobPageNo = ctx->read32(redoLogRecord->data(fieldPos + 24));
            redoLogRecord->lobData = fieldPos + 36;
            redoLogRecord->lobDataSize = fieldSize - 36;
            process(ctx, redoLogRecord);

            if (unlikely(ctx->dumpRedoLog >= 1)) {
                const uint32_t v2 = ctx->read32(redoLogRecord->data(fieldPos + 16));
                const uint16_t v1 = ctx->read16(redoLogRecord->data(fieldPos + 20));
                const typeDba dba = ctx->read32(redoLogRecord->data(fieldPos + 28));

                *ctx->dumpStream << "Direct Loader block redo entry\n";
                *ctx->dumpStream << "Long field block dump:\n";
                *ctx->dumpStream << "Object Id    " << std::dec << redoLogRecord->dataObj << " \n";
                *ctx->dumpStream << "LobId: " << redoLogRecord->lobId.narrow() <<
                        " PageNo " << std::setfill(' ') << std::setw(8) << std::dec << std::right << redoLogRecord->lobPageNo << " \n";
                *ctx->dumpStream << "Version: 0x" << std::setfill('0') << std::setw(4) << std::hex << v1 <<
                        "." << std::setfill('0') << std::setw(8) << std::hex << v2 <<
                        "  pdba: " << std::setfill(' ') << std::setw(8) << std::dec << std::right << dba << "  \n";

                for (typeSize j = 0; j < fieldSize - 36U; ++j) {
                    *ctx->dumpStream << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint>(*redoLogRecord->data(fieldPos + j + 36)) << " ";
                    if ((j % 24) == 23 && j != fieldSize - 1U)
                        *ctx->dumpStream << "\n    ";
                }
                *ctx->dumpStream << '\n';
            }

            RedoLogRecord::nextField(ctx, redoLogRecord, fieldNum, fieldPos, fieldSize, 0x130102);
            // Field: 2
            dumpMemory(ctx, redoLogRecord, fieldPos, fieldSize);

            // Field 2 is the block type. A direct-path load logs table data blocks (type 6) the same way as LOB pages
            // (type 40); field 1 is then the block without its cache header: KTBBH type 1, then the segment data object id.
            if (fieldSize >= 1 && *redoLogRecord->data(fieldPos) == BLOCK_TYPE_DATA && blockSize >= 8 &&
                *redoLogRecord->data(blockPos) == KTBBH_TYPE_DATA) {
                redoLogRecord->directLoadDataBlock = true;
                redoLogRecord->dataObj = ctx->read32(redoLogRecord->data(blockPos + 4));
                redoLogRecord->recordDataObj = redoLogRecord->dataObj;
            }
        }
    };
}

#endif
