/* Test for the character set a column's text is decoded with (Schema::columnCharmapId)
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

#include "../common/Ctx.h"
#include "../common/table/SysCol.h"
#include "../common/types/RowId.h"
#include "../common/types/Xid.h"
#include "../locales/CharacterSet.h"
#include "../locales/Locales.h"
#include "../metadata/Schema.h"

// Oracle stores CLOB data in the database character set when that character set is single-byte, and as AL16UTF16
// (UCS-2) when it is multibyte; NCLOB is always AL16UTF16. A CLOB in a WE8ISO8859P1 database was decoded as
// AL16UTF16, so "AB" (41 42) came out as U+4142.
namespace {
    using OpenLogReplicator::SysCol;

    constexpr uint64_t US7ASCII = 1;
    constexpr uint64_t WE8ISO8859P1 = 31;
    constexpr uint64_t UTF8 = 871;
    constexpr uint64_t AL32UTF8 = 873;
    constexpr uint64_t AL16UTF16 = 2000;

    int failures = 0;

    void check(const std::string& label, bool ok) {
        std::cout << (ok ? "ok   " : "FAIL ") << label << "\n";
        if (!ok)
            ++failures;
    }

    uint64_t charmapId(const OpenLogReplicator::Locales& locales, SysCol::COLTYPE type, uint charsetForm, uint64_t dbCharset,
                       uint64_t ncharCharset) {
        const SysCol sysCol(OpenLogReplicator::RowId(), 1, 1, 1, 1, "C", type, 4000, -1, -1, charsetForm, 0, 0, 0, 0);
        return OpenLogReplicator::Schema::columnCharmapId(&locales, &sysCol, dbCharset, ncharCharset);
    }

    std::vector<uint64_t> decode(const OpenLogReplicator::Ctx& ctx, const OpenLogReplicator::Locales& locales, uint64_t id,
                                 const std::vector<uint8_t>& bytes) {
        std::vector<uint64_t> out;
        auto it = locales.characterMap.find(id);
        if (it == locales.characterMap.end())
            return out;
        const uint8_t* str = bytes.data();
        uint64_t length = bytes.size();
        while (length > 0)
            out.push_back(it->second->decode(&ctx, OpenLogReplicator::Xid(), str, length));
        return out;
    }

    void expectId(const std::string& label, uint64_t actual, uint64_t expected) {
        if (actual != expected)
            std::cerr << "     got charset id " << actual << ", expected " << expected << "\n";
        check(label, actual == expected);
    }
}

int main() {
    using namespace OpenLogReplicator;

    Ctx ctx;
    Locales locales;
    locales.initialize();

    // CLOB in single-byte database character sets: stored in the database character set
    const uint64_t clobWe8 = charmapId(locales, SysCol::COLTYPE::CLOB, 1, WE8ISO8859P1, AL16UTF16);
    expectId("CLOB in WE8ISO8859P1 database uses WE8ISO8859P1", clobWe8, WE8ISO8859P1);
    const std::vector<uint64_t> text = decode(ctx, locales, clobWe8, {0x41, 0x42, 0xE5, 0xD8, 0xDB});
    check("CLOB bytes 41 42 E5 D8 DB decode to U+0041 U+0042 U+00E5 U+00D8 U+00DB",
          text == std::vector<uint64_t>({0x41, 0x42, 0xE5, 0xD8, 0xDB}));
    expectId("CLOB in US7ASCII database uses US7ASCII", charmapId(locales, SysCol::COLTYPE::CLOB, 1, US7ASCII, AL16UTF16), US7ASCII);

    // CLOB in multibyte database character sets: stored as AL16UTF16, whatever the national character set is
    expectId("CLOB in AL32UTF8 database uses AL16UTF16", charmapId(locales, SysCol::COLTYPE::CLOB, 1, AL32UTF8, AL16UTF16), AL16UTF16);
    expectId("CLOB in AL32UTF8 database with UTF8 national charset uses AL16UTF16",
             charmapId(locales, SysCol::COLTYPE::CLOB, 1, AL32UTF8, UTF8), AL16UTF16);
    check("CLOB bytes 00 41 00 42 in AL32UTF8 database decode to U+0041 U+0042",
          decode(ctx, locales, charmapId(locales, SysCol::COLTYPE::CLOB, 1, AL32UTF8, AL16UTF16), {0x00, 0x41, 0x00, 0x42}) ==
          std::vector<uint64_t>({0x41, 0x42}));

    // NCLOB and national character columns keep the national character set
    expectId("NCLOB in WE8ISO8859P1 database uses the national charset",
             charmapId(locales, SysCol::COLTYPE::CLOB, 2, WE8ISO8859P1, AL16UTF16), AL16UTF16);
    expectId("NVARCHAR2 uses the national charset", charmapId(locales, SysCol::COLTYPE::VARCHAR, 2, WE8ISO8859P1, AL16UTF16), AL16UTF16);

    // VARCHAR2 and CHAR keep the database character set
    expectId("VARCHAR2 in WE8ISO8859P1 database uses WE8ISO8859P1",
             charmapId(locales, SysCol::COLTYPE::VARCHAR, 1, WE8ISO8859P1, AL16UTF16), WE8ISO8859P1);
    expectId("CHAR in AL32UTF8 database uses AL32UTF8", charmapId(locales, SysCol::COLTYPE::CHAR, 1, AL32UTF8, AL16UTF16), AL32UTF8);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
