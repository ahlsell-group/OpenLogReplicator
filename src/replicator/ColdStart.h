/* Choice of the starting position when starting without a checkpoint
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

#ifndef COLD_START_H_
#define COLD_START_H_

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../common/types/Types.h"
#include "../common/types/Scn.h"
#include "../common/types/Seq.h"
#include "../common/types/Xid.h"

namespace OpenLogReplicator {
    // Without a checkpoint and without start-seq a transaction open at the starting SCN began in an earlier redo log.
    // Reading from the redo log holding its begin keeps it; that log may be gone, or very old. ColdStart picks the begin
    // of the oldest open transaction whose redo is still available (and which is not older than the configured maximum
    // age) and names the older ones, which are skipped at commit (60011).
    class ColdStart final {
    public:
        struct OpenTransaction {
            Xid xid;
            Scn startScn;
            uint64_t ageS;
        };

        // Where the redo of an SCN is: the sequence of the log holding it (Seq::none() if not known) and whether that log
        // and every later one up to the current log can be read
        struct RedoPosition {
            Seq sequence{Seq::none()};
            bool available{false};
        };

        struct Lost {
            OpenTransaction transaction;
            std::string reason;
        };

        struct Choice {
            Scn positionScn;
            Seq sequence{Seq::none()};
            std::vector<Lost> lost;
        };

        // transactions: open at startup, in any order. firstDataScn: the starting SCN. maxAgeS: 0 = no limit.
        // locate: looks up the redo position of an SCN.
        static Choice choose(std::vector<OpenTransaction> transactions, Scn firstDataScn, uint64_t maxAgeS,
                             const std::function<RedoPosition(Scn)>& locate) {
            std::sort(transactions.begin(), transactions.end(), [](const OpenTransaction& a, const OpenTransaction& b) {
                return a.startScn < b.startScn;
            });

            std::map<uint64_t, RedoPosition> cache;
            const auto locateCached = [&](Scn scn) {
                const auto it = cache.find(scn.getData());
                if (it != cache.end())
                    return it->second;
                const RedoPosition position = locate(scn);
                cache.emplace(scn.getData(), position);
                return position;
            };

            Choice choice;
            choice.positionScn = firstDataScn;
            std::vector<std::pair<OpenTransaction, std::string>> skipped;

            for (const OpenTransaction& transaction: transactions) {
                // No redo generated yet
                if (transaction.startScn == Scn::zero())
                    continue;
                // Began at or after the starting SCN: the log holding the starting SCN covers it, and all later ones
                if (transaction.startScn >= firstDataScn)
                    break;

                if (maxAgeS > 0 && transaction.ageS > maxAgeS) {
                    skipped.emplace_back(transaction, "began " + std::to_string(transaction.ageS) + " s ago, more than cold-start-max-age-s " +
                                         std::to_string(maxAgeS));
                    continue;
                }

                const RedoPosition position = locateCached(transaction.startScn);
                if (position.sequence == Seq::none() || !position.available) {
                    skipped.emplace_back(transaction, position.sequence == Seq::none()
                                                      ? "no redo log found for its begin"
                                                      : "redo log sequence " + position.sequence.toString() + " holding its begin, or a later one, is not available");
                    continue;
                }

                choice.positionScn = transaction.startScn;
                choice.sequence = position.sequence;
                break;
            }

            if (choice.positionScn == firstDataScn)
                choice.sequence = locateCached(firstDataScn).sequence;

            // A skipped transaction whose begin lies in the log reading starts with is read completely anyway
            for (auto& [transaction, reason]: skipped) {
                const Seq sequence = locateCached(transaction.startScn).sequence;
                if (sequence == Seq::none() || choice.sequence == Seq::none() || sequence < choice.sequence)
                    choice.lost.push_back(Lost{transaction, reason});
            }
            return choice;
        }
    };
}

#endif
