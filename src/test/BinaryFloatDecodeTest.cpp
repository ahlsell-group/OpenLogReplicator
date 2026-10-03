/* Test for the BINARY_FLOAT/BINARY_DOUBLE decoder and its JSON text
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

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "../builder/Builder.h"

namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        static float decodeFloat(const std::vector<uint8_t>& bytes) {
            return static_cast<float>(Builder::decodeFloat(bytes.data()));
        }

        static double decodeDouble(const std::vector<uint8_t>& bytes) {
            return static_cast<double>(Builder::decodeDouble(bytes.data()));
        }
    };
}

// Oracle's format (SELECT DUMP(x)): big-endian IEEE 754, sign bit set for positive values, all bits inverted for negative values.
namespace {
    using OpenLogReplicator::Builder;
    using OpenLogReplicator::BuilderTestAccess;

    int failures = 0;

    void report(const std::string& name, bool ok, const std::string& detail) {
        if (!ok) {
            std::cerr << "FAIL " << name << ": " << detail << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    uint32_t bitsOf(float value) {
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    uint64_t bitsOf(double value) {
        uint64_t bits;
        memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    std::vector<uint8_t> oracleFloat(float value) {
        uint32_t bits = bitsOf(value);
        bits = (bits & 0x80000000) == 0 ? (bits | 0x80000000) : ~bits;
        return {static_cast<uint8_t>(bits >> 24), static_cast<uint8_t>(bits >> 16), static_cast<uint8_t>(bits >> 8), static_cast<uint8_t>(bits)};
    }

    std::vector<uint8_t> oracleDouble(double value) {
        uint64_t bits = bitsOf(value);
        bits = (bits & 0x8000000000000000) == 0 ? (bits | 0x8000000000000000) : ~bits;
        std::vector<uint8_t> out;
        for (int shift = 56; shift >= 0; shift -= 8)
            out.push_back(static_cast<uint8_t>(bits >> shift));
        return out;
    }

    // Decoded value must be bit-identical; text must be the expected shortest form and read back as the same value
    void checkFloat(const std::string& name, float value, const std::string& text) {
        const float decoded = BuilderTestAccess::decodeFloat(oracleFloat(value));
        const bool same = std::isnan(value) ? std::isnan(decoded) : bitsOf(decoded) == bitsOf(value);
        report("float " + name + " decode", same, "got bits " + std::to_string(bitsOf(decoded)) + ", expected " + std::to_string(bitsOf(value)));
        const std::string actual = Builder::floatingPointToString(decoded);
        report("float " + name + " text", actual == text, "expected " + text + ", got " + actual);
        if (std::isfinite(value)) {
            const float back = strtof(actual.c_str(), nullptr);
            report("float " + name + " round trip", bitsOf(back) == bitsOf(value), actual + " reads back as another value");
        }
    }

    void checkDouble(const std::string& name, double value, const std::string& text) {
        const double decoded = BuilderTestAccess::decodeDouble(oracleDouble(value));
        const bool same = std::isnan(value) ? std::isnan(decoded) : bitsOf(decoded) == bitsOf(value);
        report("double " + name + " decode", same, "got bits " + std::to_string(bitsOf(decoded)) + ", expected " + std::to_string(bitsOf(value)));
        const std::string actual = Builder::floatingPointToString(decoded);
        report("double " + name + " text", actual == text, "expected " + text + ", got " + actual);
        if (std::isfinite(value)) {
            const double back = strtod(actual.c_str(), nullptr);
            report("double " + name + " round trip", bitsOf(back) == bitsOf(value), actual + " reads back as another value");
        }
    }
}

int main() {
    // Anchors from SELECT DUMP(TO_BINARY_FLOAT(1)) / DUMP(TO_BINARY_FLOAT(-1)) on Oracle
    report("float 1 bytes", BuilderTestAccess::decodeFloat({191, 128, 0, 0}) == 1.0F, "191,128,0,0 is not 1");
    report("float -1 bytes", BuilderTestAccess::decodeFloat({64, 127, 255, 255}) == -1.0F, "64,127,255,255 is not -1");
    report("double 1 bytes", BuilderTestAccess::decodeDouble({191, 240, 0, 0, 0, 0, 0, 0}) == 1.0, "191,240,0,... is not 1");

    checkFloat("1", 1.0F, "1");
    checkFloat("1.5", 1.5F, "1.5");
    checkFloat("-2.25", -2.25F, "-2.25");
    checkFloat("0.1", 0.1F, "0.1");
    checkFloat("16777216", 16777216.0F, "16777216");
    checkFloat("16777215", 16777215.0F, "16777215");
    checkFloat("subnormal 1e-40", 1.0e-40F, "1e-40");
    checkFloat("-subnormal 1e-40", -1.0e-40F, "-1e-40");
    checkFloat("min subnormal", std::numeric_limits<float>::denorm_min(), "1e-45");
    checkFloat("min normal", std::numeric_limits<float>::min(), "1.1754944e-38");
    checkFloat("max", std::numeric_limits<float>::max(), "3.4028235e+38");
    checkFloat("0", 0.0F, "0");
    checkFloat("-0", -0.0F, "-0");
    checkFloat("inf", std::numeric_limits<float>::infinity(), "inf");
    checkFloat("-inf", -std::numeric_limits<float>::infinity(), "-inf");
    checkFloat("nan", std::numeric_limits<float>::quiet_NaN(), "nan");

    checkDouble("1", 1.0, "1");
    checkDouble("-2.25", -2.25, "-2.25");
    checkDouble("0.1", 0.1, "0.1");
    checkDouble("9007199254740992", 9007199254740992.0, "9007199254740992");
    checkDouble("9007199254740993 rounded", 9007199254740993.0, "9007199254740992");
    checkDouble("3.14159265358979", 3.14159265358979, "3.14159265358979");
    checkDouble("subnormal 4.9e-324", 4.9e-324, "5e-324");
    checkDouble("-subnormal 1e-310", -1e-310, "-1e-310");
    checkDouble("min normal", std::numeric_limits<double>::min(), "2.2250738585072014e-308");
    checkDouble("max", std::numeric_limits<double>::max(), "1.7976931348623157e+308");
    checkDouble("1e125", 1e125, "1e+125");
    checkDouble("0", 0.0, "0");
    checkDouble("-0", -0.0, "-0");
    checkDouble("inf", std::numeric_limits<double>::infinity(), "inf");
    checkDouble("-inf", -std::numeric_limits<double>::infinity(), "-inf");
    checkDouble("nan", std::numeric_limits<double>::quiet_NaN(), "nan");

    if (failures > 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
