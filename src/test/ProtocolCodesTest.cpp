/* Test for the wire values of the network protocol request and response codes
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
#include <string>

#include "../common/OraProtoBuf.pb.h"

// RequestCode and ResponseCode are sent as numbers and shared with the client's copy of OraProtoBuf.proto
// (Debezium pins the same values in OlrProtocolCodesTest), so renumbering one breaks the protocol.
using namespace OpenLogReplicator;

namespace {
    int failures = 0;

    void check(const std::string& name, int actual, int expected) {
        if (actual != expected) {
            std::cerr << "FAIL " << name << ": " << actual << ", expected: " << expected << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << " = " << actual << "\n";
    }
}

int main() {
    check("RequestCode INFO", pb::INFO, 0);
    check("RequestCode START", pb::START, 1);
    check("RequestCode CONTINUE", pb::CONTINUE, 2);
    check("RequestCode CONFIRM", pb::CONFIRM, 3);
    check("RequestCode count", pb::RequestCode_ARRAYSIZE, 4);

    check("ResponseCode READY", pb::READY, 0);
    check("ResponseCode FAILED_START", pb::FAILED_START, 1);
    check("ResponseCode STARTING", pb::STARTING, 2);
    check("ResponseCode ALREADY_STARTED", pb::ALREADY_STARTED, 3);
    check("ResponseCode REPLICATE", pb::REPLICATE, 4);
    check("ResponseCode PAYLOAD", pb::PAYLOAD, 5);
    check("ResponseCode INVALID_DATABASE", pb::INVALID_DATABASE, 6);
    check("ResponseCode INVALID_COMMAND", pb::INVALID_COMMAND, 7);
    check("ResponseCode count", pb::ResponseCode_ARRAYSIZE, 8);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
