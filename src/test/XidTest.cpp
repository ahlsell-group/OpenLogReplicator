/* Test for Xid::toRaw, the LogMiner representation of a transaction id
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

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "../common/types/Xid.h"

namespace {
    int failures = 0;

    void check(const std::string& name, const std::string& actual, const std::string& expected) {
        if (actual != expected) {
            std::cerr << "FAIL " << name << ": got " << actual << ", expected " << expected << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    std::string hex16(uint64_t value) {
        std::ostringstream ss;
        ss << std::setfill('0') << std::setw(16) << std::hex << value;
        return ss.str();
    }

    // Byte order produced by 2.0.0 for every redo log: each field in little-endian order
    std::string hex16ReversedOld(uint64_t value) {
        static const int shifts[16]{52, 48, 60, 56, 36, 32, 44, 40, 4, 0, 12, 8, 20, 16, 28, 24};
        std::string result;
        for (const int shift: shifts)
            result += "0123456789abcdef"[(value >> shift) & 0xF];
        return result;
    }
}

int main() {
    using OpenLogReplicator::Xid;

    // Transaction 0x0012.005.0003f2a1 on a big-endian database host (e.g. Oracle 19c on AIX):
    // V$LOGMNR_CONTENTS.XID = 001200050003F2A1, OpenLogReplicator 2.0.0 emitted 12000500a1f20300
    const Xid aix(0x0012, 0x0005, 0x0003f2a1);
    check("usn.slt.sqn text", aix.toString(), "0x0012.005.0003f2a1");
    check("big-endian redo gives the LogMiner bytes", hex16(aix.toRaw(true)), "001200050003f2a1");
    check("little-endian redo gives the 2.0.0 bytes", hex16(aix.toRaw(false)), "12000500a1f20300");
    check("little-endian redo matches the 2.0.0 algorithm", hex16(aix.toRaw(false)), hex16ReversedOld(aix.getData()));

    // Transaction 0x0002.012.00004162 from the documentation example (little-endian host): 0200120062410000
    const Xid doc(0x0002, 0x0012, 0x00004162);
    check("doc example little-endian", hex16(doc.toRaw(false)), "0200120062410000");
    check("doc example little-endian matches the 2.0.0 algorithm", hex16(doc.toRaw(false)), hex16ReversedOld(doc.getData()));
    check("doc example big-endian", hex16(doc.toRaw(true)), "0002001200004162");

    // All bits set in every field, both orders round trip through the string parser
    const Xid full(static_cast<typeUsn>(0xFFFF), 0xFFFF, 0xFFFFFFFF);
    check("all ones little-endian", hex16(full.toRaw(false)), "ffffffffffffffff");
    check("all ones big-endian", hex16(full.toRaw(true)), "ffffffffffffffff");
    const Xid mixed(0x1234, 0x5678, 0x9abcdef0);
    check("mixed big-endian", hex16(mixed.toRaw(true)), "123456789abcdef0");
    check("mixed little-endian", hex16(mixed.toRaw(false)), "34127856f0debc9a");
    check("parse of raw big-endian text gives the xid back", Xid(hex16(mixed.toRaw(true))).toString(), mixed.toString());

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
