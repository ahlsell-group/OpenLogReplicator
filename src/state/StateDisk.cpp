/* Base class for state in files on disk
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

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#include "../common/Ctx.h"
#include "../common/exception/RuntimeException.h"
#include "StateDisk.h"

namespace OpenLogReplicator {
    StateDisk::StateDisk(Ctx* newCtx, std::string newPath):
            State(newCtx),
            path(std::move(newPath)) {}

    void StateDisk::list(std::set<std::string>& namesList) const {
        DIR* dir = opendir(path.c_str());
        if (dir == nullptr)
            throw RuntimeException(10012, "directory: " + path + " - can't read");

        const dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            const std::string dName(ent->d_name);
            if (dName == "." || dName == "..")
                continue;

            struct stat fileStat{};
            const std::string fileName(ent->d_name);

            const std::string fullName(path + "/" + ent->d_name);
            if (stat(fullName.c_str(), &fileStat) != 0) {
                ctx->warning(10003, "file: " + fileName + " - get metadata returned: " + strerror(errno));
                continue;
            }

            if (S_ISDIR(fileStat.st_mode))
                continue;

            const std::string suffix(".json");
            if (fileName.length() < suffix.length() || fileName.substr(fileName.length() - suffix.length(), fileName.length()) != suffix)
                continue;

            const std::string fileBase(fileName.substr(0, fileName.length() - suffix.length()));
            namesList.insert(fileBase);
        }
        closedir(dir);
    }

    bool StateDisk::read(const std::string& name, uint64_t maxSize, std::string& in) {
        const std::string fileName(path + "/" + name + ".json");
        struct stat fileStat{};
        if (stat(fileName.c_str(), &fileStat) != 0) {
            ctx->warning(10003, "file: " + fileName + " - get metadata returned: " + strerror(errno));
            return false;
        }
        if (static_cast<uint64_t>(fileStat.st_size) > maxSize || fileStat.st_size == 0)
            throw RuntimeException(10004, "file: " + fileName + " - wrong size: " + std::to_string(fileStat.st_size));

        in.resize(fileStat.st_size);
        std::ifstream inputStream;
        inputStream.open(fileName.c_str(), std::ios::in);

        if (!inputStream.is_open())
            throw RuntimeException(10001, "file: " + fileName + " - open for read returned: " + strerror(errno));

        inputStream.read(in.data(), fileStat.st_size);
        if (!inputStream)
            throw RuntimeException(10001, "file: " + fileName + " - read returned: " + strerror(errno));

        inputStream.close();
        return true;
    }

    // Written to a temporary file, synced and renamed over the old one: a crash leaves either the old or the new content
    void StateDisk::write(const std::string& name, Scn scn __attribute__((unused)), const std::ostringstream& out) {
        const std::string fileName(path + "/" + name + ".json");
        const std::string tmpName(fileName + TEMPORARY_SUFFIX);
        const std::string content(out.str());

        const int fd = open(tmpName.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (fd == -1)
            throw RuntimeException(10006, "file: " + tmpName + " - open for writing returned: " + strerror(errno));

        size_t written = 0;
        while (written < content.length()) {
            const ssize_t bytes = ::write(fd, content.data() + written, content.length() - written);
            if (bytes == -1 && errno == EINTR)
                continue;
            if (bytes <= 0) {
                const int err = errno;
                close(fd);
                unlink(tmpName.c_str());
                throw RuntimeException(10007, "file: " + tmpName + " - " + std::to_string(written) + " bytes written instead of " +
                                              std::to_string(content.length()) + ", code returned: " + strerror(err));
            }
            written += static_cast<size_t>(bytes);
        }

        if (fsync(fd) != 0) {
            const int err = errno;
            close(fd);
            unlink(tmpName.c_str());
            throw RuntimeException(10073, "file: " + tmpName + " - sync returned: " + strerror(err));
        }
        if (close(fd) != 0) {
            const int err = errno;
            unlink(tmpName.c_str());
            throw RuntimeException(10073, "file: " + tmpName + " - close returned: " + strerror(err));
        }

        if (rename(tmpName.c_str(), fileName.c_str()) != 0) {
            const int err = errno;
            unlink(tmpName.c_str());
            throw RuntimeException(10074, "file: " + tmpName + " - rename to: " + fileName + " returned: " + strerror(err));
        }
        syncDirectory();
    }

    void StateDisk::drop(const std::string& name) {
        const std::string fileName(path + "/" + name + ".json");
        if (unlink(fileName.c_str()) != 0)
            throw RuntimeException(10010, "file: " + fileName + " - delete returned: " + strerror(errno));
        syncDirectory();
    }

    void StateDisk::dropTemporary(const std::string& prefix) {
        DIR* dir = opendir(path.c_str());
        if (dir == nullptr)
            throw RuntimeException(10012, "directory: " + path + " - can't read");

        std::set<std::string> names;
        const std::string suffix(std::string(".json") + TEMPORARY_SUFFIX);
        const dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            const std::string fileName(ent->d_name);
            if (fileName.length() >= prefix.length() + suffix.length() && fileName.compare(0, prefix.length(), prefix) == 0 &&
                fileName.compare(fileName.length() - suffix.length(), suffix.length(), suffix) == 0)
                names.insert(fileName);
        }
        closedir(dir);

        for (const std::string& fileName: names) {
            const std::string fullName(path + "/" + fileName);
            if (unlink(fullName.c_str()) != 0)
                ctx->warning(10010, "file: " + fullName + " - delete returned: " + strerror(errno));
            else
                ctx->info(0, "deleted unfinished state file from previous execution: " + fullName);
        }
    }

    // A rename or unlink is durable only once the directory is synced
    void StateDisk::syncDirectory() const {
        const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd == -1)
            throw RuntimeException(10073, "directory: " + path + " - sync returned: " + strerror(errno));
        if (fsync(fd) != 0) {
            const int err = errno;
            close(fd);
            throw RuntimeException(10073, "directory: " + path + " - sync returned: " + strerror(err));
        }
        close(fd);
    }
}
