/* Test for the checkpoint interval triggers (state interval-s / interval-mb)
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


#include <cstdint>
#include <iostream>
#include <string>

#include "../metadata/Metadata.h"

// state.interval-s and state.interval-mb are documented as "0 disables this trigger". The size check used to be
// "elapsed MB < interval-mb" on unsigned values, which with interval-mb 0 is never true, so a checkpoint was written on
// every 100 ms checkpoint-thread loop and keep-checkpoints held only seconds of history.
namespace {
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    bool due(uint64_t elapsedS, uint64_t elapsedMb, uint64_t intervalS, uint64_t intervalMb) {
        return OpenLogReplicator::Metadata::checkpointIntervalDue(elapsedS, elapsedMb, intervalS, intervalMb);
    }
}

int main() {
    // production block: interval-s 1800, interval-mb 0
    check("mb 0 disabled, nothing elapsed", !due(0, 0, 1800, 0));
    check("mb 0 disabled, 1 s and 3 MB elapsed", !due(1, 3, 1800, 0));
    check("mb 0 disabled, time reached", due(1800, 0, 1800, 0));

    check("s 0 disabled, nothing elapsed", !due(0, 0, 0, 500));
    check("s 0 disabled, size reached", due(0, 500, 0, 500));
    check("s 0 disabled, size not reached", !due(100000, 499, 0, 500));

    check("both 0: never by interval", !due(100000, 100000, 0, 0));

    check("defaults, neither reached", !due(599, 499, 600, 500));
    check("defaults, time reached", due(600, 0, 600, 500));
    check("defaults, size reached", due(0, 500, 600, 500));

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
