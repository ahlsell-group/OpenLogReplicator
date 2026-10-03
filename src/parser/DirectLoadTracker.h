/* Collects direct-path (19.1) block writes into replicated tables
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

#ifndef DIRECT_LOAD_TRACKER_H_
#define DIRECT_LOAD_TRACKER_H_

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../common/types/Scn.h"
#include "../common/types/Types.h"

namespace OpenLogReplicator {
    // A direct-path load (INSERT /*+ APPEND */, SQL*Loader direct, CTAS into an existing segment, ...) writes whole formatted
    // blocks as OP 19.1 "Direct Loader block redo entry" without row-level change vectors. OLR does not decode those blocks,
    // so the rows never reach the output. The tracker counts such blocks per table within one LWN, and the parser logs one
    // warning per table and LWN instead of one per block.
    class DirectLoadTracker final {
    public:
        static constexpr int WARNING_CODE = 60042;

        struct Entry {
            std::string owner;
            std::string table;
            typeObj obj{0};
            typeDataObj dataObj{0};
            Scn firstScn{Scn::none()};
            Scn lastScn{Scn::none()};
            uint64_t blocks{0};
        };

        void add(typeObj obj, typeDataObj dataObj, const std::string& owner, const std::string& table, Scn scn) {
            Entry& entry = entries[obj];
            if (entry.blocks == 0) {
                entry.owner = owner;
                entry.table = table;
                entry.obj = obj;
                entry.dataObj = dataObj;
                entry.firstScn = scn;
            }
            entry.lastScn = scn;
            ++entry.blocks;
        }

        [[nodiscard]] bool empty() const {
            return entries.empty();
        }

        // Returns one message per table seen since the last flush and clears the counters.
        std::vector<std::string> flush() {
            std::vector<std::string> messages;
            messages.reserve(entries.size());
            for (const auto& [obj, entry]: entries)
                messages.push_back(message(entry));
            entries.clear();
            return messages;
        }

        static std::string message(const Entry& entry) {
            std::string scn = entry.firstScn.toString();
            if (entry.lastScn != entry.firstScn)
                scn += "-" + entry.lastScn.toString();
            return "direct-path load into replicated table " + entry.owner + "." + entry.table + " (obj: " + std::to_string(entry.obj) +
                   ", dataobj: " + std::to_string(entry.dataObj) + ", scn: " + scn + ", blocks: " + std::to_string(entry.blocks) +
                   "): OP 19.1 direct loader blocks are not decoded, the rows they contain are missing from the output";
        }

    private:
        std::map<typeObj, Entry> entries;
    };
}

#endif
