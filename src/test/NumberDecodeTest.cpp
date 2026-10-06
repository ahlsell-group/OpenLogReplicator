/* Test for the NUMBER decoder (Builder::parseNumber)
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

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/exception/RedoLogException.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

namespace OpenLogReplicator {
    // One readable page followed by an inaccessible page; a record copied to the end of the readable page
    // makes any read past the record fault, with or without AddressSanitizer
    class GuardPage {
    public:
        GuardPage() {
            pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
            void* mem = mmap(nullptr, pageSize * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mem == MAP_FAILED)
                throw std::runtime_error("mmap failed");
            base = static_cast<uint8_t*>(mem);
            if (mprotect(base + pageSize, pageSize, PROT_NONE) != 0)
                throw std::runtime_error("mprotect failed");
        }

        ~GuardPage() {
            munmap(base, pageSize * 2);
        }

        GuardPage(const GuardPage&) = delete;
        GuardPage& operator=(const GuardPage&) = delete;

        const uint8_t* place(const std::vector<uint8_t>& bytes) {
            uint8_t* start = base + pageSize - bytes.size();
            if (!bytes.empty())
                memcpy(start, bytes.data(), bytes.size());
            return start;
        }

    private:
        uint8_t* base;
        size_t pageSize;
    };

    class BuilderTestAccess {
    public:
        // Decodes the record twice: from an exactly sized heap block (an overread is a heap-buffer-overflow under
        // AddressSanitizer) and from the end of a guard page (an overread is a segmentation fault in any build).
        // Returns the decoded text, or "error 50009" when the decoder rejects the record.
        static std::string decodeNumber(Builder& builder, const std::vector<uint8_t>& bytes) {
            static GuardPage guard;
            const std::unique_ptr<uint8_t[]> heap(new uint8_t[bytes.size()]);
            if (!bytes.empty())
                memcpy(heap.get(), bytes.data(), bytes.size());

            const std::string fromHeap = decode(builder, heap.get(), bytes.size());
            const std::string fromGuard = decode(builder, guard.place(bytes), bytes.size());
            if (fromHeap != fromGuard)
                return "heap/guard mismatch: " + fromHeap + " / " + fromGuard;
            return fromHeap;
        }

    private:
        static std::string decode(Builder& builder, const uint8_t* data, uint64_t size) {
            try {
                builder.parseNumber(data, size, FileOffset());
            } catch (const RedoLogException& e) {
                return "error " + std::to_string(e.code);
            }
            return {builder.valueBuffer, builder.valueSize};
        }
    };
}

// Byte vectors are Oracle's internal NUMBER format (SELECT DUMP(x) gives the same bytes in decimal).
namespace {
    using TestBuilder = OpenLogReplicator::BuilderJson;

    int failures = 0;

    void check(TestBuilder& builder, const std::string& name, const std::vector<uint8_t>& bytes, const std::string& expected) {
        const std::string actual = OpenLogReplicator::BuilderTestAccess::decodeNumber(builder, bytes);
        if (actual != expected) {
            std::cerr << "FAIL " << name << ": expected " << expected << ", got " << actual << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    // Independent encoder: value = sign * 0.d1 d2 ... (base 100) * 100^(exp100 + 1), digits without trailing zeros
    std::vector<uint8_t> encode(bool negative, int exp100, const std::vector<uint8_t>& digits) {
        std::vector<uint8_t> out;
        if (!negative) {
            out.push_back(static_cast<uint8_t>(0xC1 + exp100));
            for (const uint8_t d: digits)
                out.push_back(static_cast<uint8_t>(d + 1));
        } else {
            out.push_back(static_cast<uint8_t>(0xFF - (0xC1 + exp100)));
            for (const uint8_t d: digits)
                out.push_back(static_cast<uint8_t>(101 - d));
            if (digits.size() < 20)
                out.push_back(102);
        }
        return out;
    }

    // Oracle's internal NUMBER bytes for sign * 0.d1 d2 ... (base 100) * 100^(exp100 + 1); a negative value gets the 0x66
    // terminator only when `terminator` is set
    std::vector<uint8_t> encodeRaw(bool negative, int exp100, const std::vector<uint8_t>& digits, bool terminator) {
        std::vector<uint8_t> out;
        out.push_back(static_cast<uint8_t>(negative ? 0x3E - exp100 : 0xC1 + exp100));
        for (const uint8_t d: digits)
            out.push_back(static_cast<uint8_t>(negative ? 101 - d : d + 1));
        if (negative && terminator)
            out.push_back(102);
        return out;
    }

    // Reference decoder working on decimal text: all pairs as two digits, the point after (exp100 + 1) pairs,
    // leading zeros of the integer part and trailing zeros of the fraction removed
    std::string expectedDigits(bool negative, int exp100, const std::vector<uint8_t>& digits) {
        std::string pairs;
        for (const uint8_t d: digits) {
            pairs += static_cast<char>('0' + (d / 10));
            pairs += static_cast<char>('0' + (d % 10));
        }
        const int point = (exp100 + 1) * 2;
        std::string integer;
        std::string fraction;
        if (point <= 0) {
            fraction = std::string(static_cast<size_t>(-point), '0') + pairs;
        } else if (static_cast<size_t>(point) >= pairs.size()) {
            integer = pairs + std::string(static_cast<size_t>(point) - pairs.size(), '0');
        } else {
            integer = pairs.substr(0, static_cast<size_t>(point));
            fraction = pairs.substr(static_cast<size_t>(point));
        }
        integer.erase(0, std::min(integer.find_first_not_of('0'), integer.size()));
        if (integer.empty())
            integer = "0";
        while (!fraction.empty() && fraction.back() == '0')
            fraction.pop_back();
        return (negative ? "-" : "") + integer + (fraction.empty() ? "" : "." + fraction);
    }

    // Mantissa of `count` pairs: first pair 1..99, inner pairs 0..99, last pair 1..99
    std::vector<uint8_t> mantissa(size_t count, unsigned seed) {
        std::vector<uint8_t> digits;
        for (size_t i = 0; i < count; ++i) {
            unsigned d = (static_cast<unsigned>(i) * 37 + seed * 11 + 5) % 100;
            if ((i == 0 || i + 1 == count) && d == 0)
                d = 1;
            digits.push_back(static_cast<uint8_t>(d));
        }
        return digits;
    }

    // Plain decimal text of digit pair `d` (1..99) times 100^exp100
    std::string expectedPair(bool negative, int exp100, int d) {
        std::string s = negative ? "-" : "";
        if (exp100 >= 0) {
            s += std::to_string(d);
            s += std::string(static_cast<size_t>(exp100) * 2, '0');
        } else {
            std::string frac = std::string((static_cast<size_t>(-exp100) - 1) * 2, '0');
            frac += (d < 10 ? "0" : "") + std::to_string(d);
            while (frac.back() == '0')
                frac.pop_back();
            s += "0." + frac;
        }
        return s;
    }
}

int main() {
    using namespace OpenLogReplicator;

    Ctx ctx;
    Locales locales;
    Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
    Format format(Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                  Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                  Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                  Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                  Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                  Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT);
    TestBuilder builder(&ctx, &locales, &metadata, format, 0);

    const std::string zeros128(128, '0');
    const std::string zeros124(124, '0');

    // Zero; Oracle has no negative zero, -0 is stored as 0x80
    check(builder, "zero", {0x80}, "0");

    // Bottom of the exponent range: exponent byte 0x80 (positive) / 0x7F (negative) with a mantissa
    check(builder, "1e-130", {0x80, 0x02}, "0." + std::string(129, '0') + "1");
    check(builder, "-1e-130", {0x7F, 0x64, 0x66}, "-0." + std::string(129, '0') + "1");
    check(builder, "1.5e-129", {0x80, 0x10}, "0." + zeros128 + "15");
    check(builder, "-1.5e-129", {0x7F, 0x56, 0x66}, "-0." + zeros128 + "15");
    check(builder, "9.99e-129", {0x80, 0x64, 0x5B}, "0." + zeros128 + "999");
    check(builder, "1e-128", {0x81, 0x02}, "0." + std::string(127, '0') + "1");
    check(builder, "-1e-128", {0x7E, 0x64, 0x66}, "-0." + std::string(127, '0') + "1");

    // Top of the exponent range
    check(builder, "1e125", {0xFF, 0x0B}, "1" + std::string(125, '0'));
    check(builder, "9.99e125", {0xFF, 0x64, 0x5B}, "999" + std::string(123, '0'));
    check(builder, "-9.99e125", {0x00, 0x02, 0x0B, 0x66}, "-999" + std::string(123, '0'));
    check(builder, "1e124 (integer, large exponent)", {0xFF, 0x02}, "1" + zeros124);

    // Small integers and negatives
    check(builder, "1", {0xC1, 0x02}, "1");
    check(builder, "-1", {0x3E, 0x64, 0x66}, "-1");
    check(builder, "7", {0xC1, 0x08}, "7");
    check(builder, "1200", {0xC2, 0x0D}, "1200");
    check(builder, "-42.75", {0x3E, 0x3B, 0x1A, 0x66}, "-42.75");
    check(builder, "38 nines", encode(false, 18, std::vector<uint8_t>(19, 99)), std::string(38, '9'));
    check(builder, "-38 nines", encode(true, 18, std::vector<uint8_t>(19, 99)), "-" + std::string(38, '9'));
    check(builder, "-1/3 (20 mantissa bytes, no terminator)", encode(true, -1, std::vector<uint8_t>(20, 33)), "-0." + std::string(40, '3'));

    // Fractions and trailing zeros: Oracle keeps no scale, 12.500 is stored as 12.5
    check(builder, "12.5", {0xC1, 0x0D, 0x33}, "12.5");
    check(builder, "-12.5", {0x3E, 0x59, 0x33, 0x66}, "-12.5");
    check(builder, "0.5", {0xC0, 0x33}, "0.5");
    check(builder, "0.001", {0xBF, 0x0B}, "0.001");
    check(builder, "-0.001", {0x40, 0x5B, 0x66}, "-0.001");
    check(builder, "0.01", {0xC0, 0x02}, "0.01");
    check(builder, "999999999999.999", {0xC6, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x5B}, "999999999999.999");
    check(builder, "1/3 (40 digits)", encode(false, -1, std::vector<uint8_t>(20, 33)), "0." + std::string(40, '3'));

    // Sweep the whole exponent range, positive and negative, one- and two-digit leading pairs
    for (int exp100 = -65; exp100 <= 62; ++exp100) {
        for (const int d: {1, 15, 99}) {
            for (const bool negative: {false, true}) {
                const std::string name = std::string(negative ? "-" : "") + std::to_string(d) + "*100^" + std::to_string(exp100);
                check(builder, name, encode(negative, exp100, {static_cast<uint8_t>(d)}), expectedPair(negative, exp100, d));
            }
        }
    }

    // Records of every length from 0 to 22 bytes, each decoded from the very end of its buffer. Exponents cover the bottom
    // and top of the range, pure fractions, integer.fraction splits and integers longer than the mantissa.
    check(builder, "empty record", {}, "error 50009");
    check(builder, "1 byte: zero", {0x80}, "0");
    check(builder, "1 byte: 0x00", {0x00}, "0");
    check(builder, "1 byte: positive exponent without mantissa", {0xC1}, "error 50009");
    check(builder, "1 byte: negative exponent without mantissa", {0x3E}, "error 50009");
    check(builder, "1 byte: 0xFF", {0xFF}, "error 50009");
    check(builder, "2 bytes: 100", {0xC2, 0x02}, "100");
    check(builder, "2 bytes: 1e124", {0xFF, 0x02}, "1" + zeros124);
    check(builder, "2 bytes: -1 without terminator", {0x3E, 0x64}, "-1");
    check(builder, "3 bytes: -100", {0x3D, 0x64, 0x66}, "-100");
    check(builder, "3 bytes: -1e124", {0x00, 0x64, 0x66}, "-1" + zeros124);
    for (size_t length = 2; length <= 22; ++length) {
        const size_t m = length - 1;
        const int m100 = static_cast<int>(m);
        for (const int exp100: {-65, -2, -1, 0, (m100 - 1) / 2, m100 - 1, m100, m100 + 3, 62}) {
            if (exp100 < -65 || exp100 > 62)
                continue;
            for (const unsigned seed: {0U, 1U, 7U}) {
                const std::vector<uint8_t> digits = mantissa(m, seed + static_cast<unsigned>(length));
                const std::string suffix = std::to_string(length) + " bytes, exp " + std::to_string(exp100) + ", seed " + std::to_string(seed);
                check(builder, "+ " + suffix, encodeRaw(false, exp100, digits, false), expectedDigits(false, exp100, digits));
                // Negative without terminator: same length as the positive record
                check(builder, "- no terminator " + suffix, encodeRaw(true, exp100, digits, false), expectedDigits(true, exp100, digits));
                // Negative with terminator: one mantissa pair less, same length
                if (m >= 2) {
                    const std::vector<uint8_t> shorter(digits.begin(), digits.end() - 1);
                    std::vector<uint8_t> shorterDigits = shorter;
                    if (shorterDigits.back() == 0)
                        shorterDigits.back() = 1;
                    check(builder, "- terminator " + suffix, encodeRaw(true, exp100, shorterDigits, true), expectedDigits(true, exp100, shorterDigits));
                }
            }
        }
    }

    if (failures > 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
