/* Test for invalid input in the Unicode character set decoders
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
#include <sstream>
#include <string>
#include <vector>

#include "../common/Ctx.h"
#include "../common/types/Xid.h"
#include "../locales/CharacterSetAL16UTF16.h"
#include "../locales/CharacterSetAL32UTF8.h"
#include "../locales/CharacterSetUTF8.h"

// A decoded code point is written as UTF-8 to the JSON output, so a surrogate (U+D800-DFFF) there is invalid UTF-8.
// Invalid input decodes to U+FFFD with warning 60008 and consumes only the bytes that belong to it, so the next
// character is still decoded.
namespace {
    using OpenLogReplicator::CharacterSet;
    using OpenLogReplicator::Ctx;

    constexpr typeUnicode BAD = CharacterSet::UNICODE_UNKNOWN_CHARACTER;

    int failures = 0;

    std::string hex(const std::vector<typeUnicode>& values) {
        std::ostringstream s;
        s << std::hex << std::uppercase;
        for (const typeUnicode v: values)
            s << " U+" << v;
        return s.str();
    }

    size_t countWarnings(const std::string& log) {
        size_t count = 0;
        for (size_t pos = log.find("60008"); pos != std::string::npos; pos = log.find("60008", pos + 1))
            ++count;
        return count;
    }

    void check(const Ctx& ctx, const CharacterSet& charset, const std::string& name, const std::vector<uint8_t>& bytes,
               const std::vector<typeUnicode>& expected, size_t expectedWarnings) {
        std::vector<typeUnicode> decoded;
        std::ostringstream log;
        std::streambuf* old = std::cerr.rdbuf(log.rdbuf());
        const uint8_t* str = bytes.data();
        uint64_t length = bytes.size();
        while (length > 0)
            decoded.push_back(charset.decode(&ctx, OpenLogReplicator::Xid(), str, length));
        std::cerr.rdbuf(old);

        const size_t warnings = countWarnings(log.str());
        const std::string label = charset.name + " " + name;
        if (decoded != expected || warnings != expectedWarnings) {
            std::cerr << "FAIL " << label << ": got" << hex(decoded) << " (" << warnings << " warnings), expected" << hex(expected) << " ("
                      << expectedWarnings << " warnings)\n";
            ++failures;
        } else
            std::cout << "ok   " << label << "\n";
    }
}

int main() {
    using namespace OpenLogReplicator;

    Ctx ctx;
    const CharacterSetAL16UTF16 al16utf16;
    const CharacterSetAL32UTF8 al32utf8;
    const CharacterSetUTF8 utf8;

    // AL16UTF16: big-endian UTF-16
    check(ctx, al16utf16, "BMP", {0x00, 0x41, 0x00, 0xC5}, {0x41, 0xC5}, 0);
    check(ctx, al16utf16, "surrogate pair", {0xD8, 0x3D, 0xDE, 0x00}, {0x1F600}, 0);
    check(ctx, al16utf16, "high surrogate then BMP", {0xD8, 0x00, 0x00, 0x41}, {BAD, 0x41}, 1);
    check(ctx, al16utf16, "high surrogate then high surrogate", {0xD8, 0x00, 0xD8, 0x3D, 0xDE, 0x00}, {BAD, 0x1F600}, 1);
    check(ctx, al16utf16, "high surrogate at end", {0x00, 0x41, 0xD8, 0x00}, {0x41, BAD}, 1);
    check(ctx, al16utf16, "lone low surrogate", {0xDC, 0x00, 0x00, 0x41}, {BAD, 0x41}, 1);

    // AL32UTF8: standard UTF-8
    check(ctx, al32utf8, "valid boundaries", {0xC2, 0x80, 0xE0, 0xA0, 0x80, 0xED, 0x9F, 0xBF, 0xEE, 0x80, 0x80, 0xF0, 0x90, 0x80, 0x80},
          {0x80, 0x800, 0xD7FF, 0xE000, 0x10000}, 0);
    check(ctx, al32utf8, "surrogate U+D800", {0xED, 0xA0, 0x80, 0x41}, {BAD, 0x41}, 1);
    check(ctx, al32utf8, "surrogate U+DFFF", {0xED, 0xBF, 0xBF, 0x41}, {BAD, 0x41}, 1);
    check(ctx, al32utf8, "overlong 2 bytes C0 80", {0xC0, 0x80, 0x41}, {BAD, 0x41}, 1);
    check(ctx, al32utf8, "overlong 2 bytes C1 BF", {0xC1, 0xBF, 0x41}, {BAD, 0x41}, 1);
    check(ctx, al32utf8, "overlong 3 bytes E0 80 80", {0xE0, 0x80, 0x80, 0x41}, {BAD, 0x41}, 1);
    check(ctx, al32utf8, "overlong 3 bytes E0 9F BF", {0xE0, 0x9F, 0xBF, 0x41}, {BAD, 0x41}, 1);
    check(ctx, al32utf8, "overlong 4 bytes F0 80 80 80", {0xF0, 0x80, 0x80, 0x80, 0x41}, {BAD, 0x41}, 1);
    check(ctx, al32utf8, "overlong 4 bytes F0 8F BF BF", {0xF0, 0x8F, 0xBF, 0xBF, 0x41}, {BAD, 0x41}, 1);
    check(ctx, al32utf8, "above U+10FFFF", {0xF4, 0x90, 0x80, 0x80, 0x41}, {BAD, 0x41}, 1);

    // UTF8: CESU-8, a supplementary character is a pair of 3-byte surrogates
    check(ctx, utf8, "valid boundaries", {0xC2, 0x80, 0xE0, 0xA0, 0x80, 0xED, 0x9F, 0xBF, 0xEE, 0x80, 0x80}, {0x80, 0x800, 0xD7FF, 0xE000}, 0);
    check(ctx, utf8, "surrogate pair", {0xED, 0xA0, 0xBD, 0xED, 0xB8, 0x80}, {0x1F600}, 0);
    check(ctx, utf8, "high surrogate then BMP", {0xED, 0xA0, 0x80, 0x41}, {BAD, 0x41}, 1);
    check(ctx, utf8, "high surrogate then 3-byte BMP", {0xED, 0xA0, 0x80, 0xE2, 0x82, 0xAC}, {BAD, 0x20AC}, 1);
    check(ctx, utf8, "high surrogate at end", {0x41, 0xED, 0xAF, 0xBF}, {0x41, BAD}, 1);
    check(ctx, utf8, "lone low surrogate U+DC00", {0xED, 0xB0, 0x80, 0x41}, {BAD, 0x41}, 1);
    check(ctx, utf8, "lone low surrogate U+DFFF", {0xED, 0xBF, 0xBF, 0x41}, {BAD, 0x41}, 1);
    check(ctx, utf8, "overlong 2 bytes C0 80", {0xC0, 0x80, 0x41}, {BAD, 0x41}, 1);
    check(ctx, utf8, "overlong 2 bytes C1 BF", {0xC1, 0xBF, 0x41}, {BAD, 0x41}, 1);
    check(ctx, utf8, "overlong 3 bytes E0 80 80", {0xE0, 0x80, 0x80, 0x41}, {BAD, 0x41}, 1);
    check(ctx, utf8, "overlong 3 bytes E0 9F BF", {0xE0, 0x9F, 0xBF, 0x41}, {BAD, 0x41}, 1);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
