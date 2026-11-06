/* Open transactions a restart can skip the redo of
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

#ifndef DORMANT_TRANSACTION_H_
#define DORMANT_TRANSACTION_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Attribute.h"
#include "types/FileOffset.h"
#include "types/Scn.h"
#include "types/Seq.h"
#include "types/Time.h"
#include "types/Types.h"
#include "types/Xid.h"

namespace OpenLogReplicator {
    // An open transaction that holds nothing a restart needs (no rows, LOBs, DDL or dictionary changes), only its begin.
    // A restart can start after its begin and recreate it from these fields.
    struct DormantTransaction {
        Xid xid;
        typeConId conId{0};
        uint16_t thread{0};
        Seq beginSequence{Seq::none()};
        FileOffset beginFileOffset;
        Scn beginScn{Scn::none()};
        Time beginTimestamp{0};
        std::shared_ptr<const AttributeMap> attributes;

        [[nodiscard]] bool beginsBefore(Seq sequence, FileOffset fileOffset) const {
            return beginSequence < sequence || (beginSequence == sequence && beginFileOffset < fileOffset);
        }
    };

    // Restart position over the transactions that are not dormant, and the dormant ones, for one checkpoint.
    // valid = false: not computed for this checkpoint, which then is written without them.
    struct DormantCheckpoint {
        bool valid{false};
        Seq sequence{Seq::none()};
        FileOffset fileOffset;
        Xid xid;
        std::vector<DormantTransaction> transactions;

        void clear() {
            valid = false;
            sequence = Seq::none();
            fileOffset = FileOffset::zero();
            xid = Xid::zero();
            transactions.clear();
        }
    };

    // FNV-1a over what decides which transactions are dormant (filter, flags, transaction-max-mb, debug table)
    inline std::string dormantFingerprint(const std::string& configuration) {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (const unsigned char c: configuration) {
            hash ^= c;
            hash *= 0x100000001b3ULL;
        }
        static constexpr char HEX[] = "0123456789abcdef";
        std::string out(16, '0');
        for (int i = 15; i >= 0; --i) {
            out[static_cast<size_t>(i)] = HEX[hash & 0xF];
            hash >>= 4;
        }
        return out;
    }
}

#endif
