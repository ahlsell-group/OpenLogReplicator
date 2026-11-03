/* Test the state files on disk: atomic replace, leftovers of an interrupted write
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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "../common/Ctx.h"
#include "../common/exception/RuntimeException.h"
#include "../state/StateDisk.h"

// StateDisk::write replaces a file through <name>.json.tmp and a rename, so a crash leaves the old or the new content
// and at most a .tmp file, which list() does not return and dropTemporary() removes.
using namespace OpenLogReplicator;

namespace {
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    void write(StateDisk& state, const std::string& name, const std::string& content) {
        std::ostringstream ss;
        ss << content;
        state.write(name, Scn::none(), ss);
    }

    std::string read(StateDisk& state, const std::string& name) {
        std::string content;
        if (!state.read(name, 1024, content))
            return "<missing>";
        return content;
    }

    bool tmpLeft(const std::string& dir) {
        for (const auto& entry: std::filesystem::directory_iterator(dir))
            if (entry.path().extension() == ".tmp")
                return true;
        return false;
    }
}

int main() {
    Ctx ctx;

    char pattern[] = "/tmp/olr-state-disk-XXXXXX";
    const char* created = mkdtemp(pattern);
    if (created == nullptr) {
        std::cerr << "FAIL mkdtemp\n";
        return 1;
    }
    const std::string dir(created);
    StateDisk state(&ctx, dir);

    write(state, "TEST-chkpt", R"({"scn":1})");
    check("write creates the file", read(state, "TEST-chkpt") == R"({"scn":1})");
    write(state, "TEST-chkpt", R"({"scn":22})");
    check("write replaces the content", read(state, "TEST-chkpt") == R"({"scn":22})");
    write(state, "TEST-chkpt", R"({"s":3})");
    check("a shorter content leaves no tail of the old one", read(state, "TEST-chkpt") == R"({"s":3})");
    check("no temporary file is left after a write", !tmpLeft(dir));

    // A write that can't create its temporary file fails and leaves the old file as it was
    std::filesystem::create_directory(dir + "/TEST-chkpt.json.tmp");
    bool failed = false;
    try {
        write(state, "TEST-chkpt", R"({"scn":4})");
    } catch (const RuntimeException& ex) {
        failed = ex.code == 10006;
    }
    check("a failed write throws 10006", failed);
    check("a failed write keeps the old content", read(state, "TEST-chkpt") == R"({"s":3})");
    std::filesystem::remove(dir + "/TEST-chkpt.json.tmp");

    // A write that can't replace its target (a directory) fails and removes its temporary file
    std::filesystem::create_directory(dir + "/TEST-blocked.json");
    failed = false;
    try {
        write(state, "TEST-blocked", R"({"scn":5})");
    } catch (const RuntimeException& ex) {
        failed = ex.code == 10074;
    }
    check("a failed rename throws 10074", failed);
    check("a failed rename leaves no temporary file", !std::filesystem::exists(dir + "/TEST-blocked.json.tmp"));
    std::filesystem::remove(dir + "/TEST-blocked.json");

    // Leftovers of writes interrupted by a crash
    std::ofstream(dir + "/TEST-chkpt-100.json.tmp") << R"({"scn":)";
    std::ofstream(dir + "/TEST-position-100.json.tmp").close();
    std::ofstream(dir + "/OTHER-chkpt-100.json.tmp") << "{}";
    std::set<std::string> names;
    state.list(names);
    check("list() returns only .json files", names == std::set<std::string>{"TEST-chkpt"});

    state.dropTemporary("TEST-chkpt");
    state.dropTemporary("TEST-position-");
    check("dropTemporary() removes the leftovers of its prefix",
          !std::filesystem::exists(dir + "/TEST-chkpt-100.json.tmp") && !std::filesystem::exists(dir + "/TEST-position-100.json.tmp"));
    check("dropTemporary() keeps other files", std::filesystem::exists(dir + "/OTHER-chkpt-100.json.tmp") &&
          std::filesystem::exists(dir + "/TEST-chkpt.json"));

    state.drop("TEST-chkpt");
    check("drop removes the file", !std::filesystem::exists(dir + "/TEST-chkpt.json"));

    std::filesystem::remove_all(dir);
    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
