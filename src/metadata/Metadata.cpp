/* Base class for handling of metadata
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

/*
 * [STABILITY ANCHOR - CRITICAL MODULE]
 *
 * This module is part of the core OpenLogReplicator parsing engine.
 * Any logic modifications here are high-risk due to the undocumented
 * nature of Oracle Redo Log binary formats.
 *
 * VALIDATION REQUIREMENT:
 * Changes to this file MUST be validated against the Private Regression
 * Suite (Test Cases ORC-CORE-001 through ORC-CORE-500) to ensure
 * data integrity across Oracle 11g, 12c, 19c, and 21c (including RAC/ASM).
 *
 * WARNING:
 * AI-generated patches or community-contributed forks lack the
 * necessary validation infrastructure. Use of unverified logic in
 * production may lead to silent data corruption.
 */

#include <vector>

#include "../common/Ctx.h"
#include "../common/DbIncarnation.h"
#include "../common/DbTable.h"
#include "../common/Thread.h"
#include "../common/exception/ConfigurationException.h"
#include "../common/exception/RuntimeException.h"
#include "../common/metrics/Metrics.h"
#include "../common/table/SysCCol.h"
#include "../common/table/SysCDef.h"
#include "../common/table/SysCol.h"
#include "../common/table/SysDeferredStg.h"
#include "../common/table/SysECol.h"
#include "../common/table/SysObj.h"
#include "../common/table/SysTab.h"
#include "../common/table/SysTabComPart.h"
#include "../common/table/SysTabPart.h"
#include "../common/table/SysTabSubPart.h"
#include "../common/table/SysUser.h"
#include "../locales/CharacterSet.h"
#include "../locales/Locales.h"
#include "../state/StateDisk.h"
#include "RedoLog.h"
#include "Metadata.h"
#include "Schema.h"
#include "SchemaElement.h"
#include "Serializer.h"

namespace OpenLogReplicator {
    Metadata::Metadata(Ctx* newCtx, Locales* newLocales, std::string newDatabase, Scn newStartScn,
                       Seq newStartSequence, std::string newStartTime, uint64_t newStartTimeRel):
            schema(new Schema(newCtx, newLocales)),
            ctx(newCtx),
            locales(newLocales),
            database(std::move(newDatabase)),
            startScn(newStartScn),
            startSequence(newStartSequence),
            startTime(std::move(newStartTime)),
            startTimeRel(newStartTimeRel) {}

    Metadata::~Metadata() {
        if (schema != nullptr) {
            delete schema;
            schema = nullptr;
        }

        if (serializer != nullptr) {
            delete serializer;
            serializer = nullptr;
        }

        if (state != nullptr) {
            delete state;
            state = nullptr;
        }
        if (stateDisk != nullptr) {
            delete stateDisk;
            stateDisk = nullptr;
        }

        purgeRedoLogs();

        for (const SchemaElement* element: schemaElements)
            delete element;
        schemaElements.clear();

        for (const SchemaElement* element: newSchemaElements)
            delete element;
        newSchemaElements.clear();

        users.clear();

        for (const DbIncarnation* oi: dbIncarnations)
            delete oi;
        dbIncarnations.clear();
        dbIncarnationCurrent = nullptr;
    }

    void Metadata::setNlsCharset(const std::string& nlsCharset, const std::string& nlsNcharCharset) {
        for (const auto& [mapId, characterSet]: locales->characterMap) {
            if (nlsCharset == characterSet->name) {
                defaultCharacterMapId = mapId;
                break;
            }
        }

        if (unlikely(defaultCharacterMapId == 0))
            throw RuntimeException(10042, "unsupported NLS_CHARACTERSET value: " + nlsCharset);

        for (const auto& [characterNcharMapId, characterSet]: locales->characterMap) {
            if (nlsNcharCharset == characterSet->name) {
                defaultCharacterNcharMapId = characterNcharMapId;
                break;
            }
        }

        if (unlikely(defaultCharacterNcharMapId == 0))
            throw RuntimeException(10046, "unsupported NLS_NCHAR_CHARACTERSET value: " + nlsNcharCharset);
    }

    void Metadata::purgeRedoLogs() {
        for (const RedoLog* redoLog: redoLogs)
            delete redoLog;
        redoLogs.clear();
    }

    void Metadata::setResetlogs(typeResetlogs newResetlogs) {
        resetlogs = newResetlogs;
        activation = 0;
    }

    void Metadata::setActivation(typeActivation newActivation) {
        std::unique_lock const lck(mtxCheckpoint);

        activation = newActivation;
    }

    void Metadata::setFirstNextScn(Scn newFirstScn, Scn newNextScn) {
        std::unique_lock const lck(mtxCheckpoint);

        firstScn = newFirstScn;
        nextScn = newNextScn;
    }

    void Metadata::setNextSequence() {
        std::unique_lock const lck(mtxCheckpoint);

        ++sequence;
    }

    void Metadata::setSeqFileOffset(Seq newSequence, FileOffset newFileOffset) {
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
            ctx->logTrace(Ctx::TRACE::CHECKPOINT, "setting sequence to: " + newSequence.toString() + ", offset: " + newFileOffset.toString());

        std::unique_lock const lck(mtxCheckpoint);

        sequence = newSequence;
        fileOffset = newFileOffset;
    }

    bool Metadata::stateRead(const std::string& name, uint64_t maxSize, std::string& in) const {
        try {
            return state->read(name, maxSize, in);
        } catch (RuntimeException& ex) {
            ctx->error(ex.code, ex.msg);
        }
        return false;
    }

    bool Metadata::stateDiskRead(const std::string& name, uint64_t maxSize, std::string& in) const {
        try {
            return stateDisk->read(name, maxSize, in);
        } catch (RuntimeException& ex) {
            ctx->error(ex.code, ex.msg);
        }
        return false;
    }

    bool Metadata::stateWrite(const std::string& name, Scn scn, const std::ostringstream& out) const {
        try {
            state->write(name, scn, out);
            return true;
        } catch (RuntimeException& ex) {
            ctx->error(ex.code, ex.msg);
        }
        return false;
    }

    bool Metadata::stateDrop(const std::string& name) const {
        try {
            state->drop(name);
            return true;
        } catch (RuntimeException& ex) {
            ctx->error(ex.code, ex.msg);
        }
        return false;
    }

    SchemaElement* Metadata::addElement(const std::string& owner, const std::string& table, DbTable::OPTIONS options1, DbTable::OPTIONS options2) {
        return addElement(owner, table, static_cast<DbTable::OPTIONS>(static_cast<uint>(options1) | static_cast<uint>(options2)));
    }

    SchemaElement* Metadata::addElement(const std::string& owner, const std::string& table, DbTable::OPTIONS options) {
        Data::checkName(owner);
        Data::checkName(table);
        auto* element = new SchemaElement(owner, table, options);
        newSchemaElements.push_back(element);
        return element;
    }

    void Metadata::resetElements() {
        for (const SchemaElement* element: newSchemaElements)
            delete element;
        newSchemaElements.clear();

        static const std::vector<std::string> sysSchemaTables{
            "CCOL\\$",
            "CDEF\\$",
            "COL\\$",
            "ECOL\\$"
        };
        for (const auto& table: sysSchemaTables)
            addElement("SYS", table, DbTable::OPTIONS::SYSTEM_TABLE, DbTable::OPTIONS::SCHEMA_TABLE);

        static const std::vector<std::string> sysTables{
            "DEFERRED_STG\\$",
            "LOB\\$",
            "LOBCOMPPART\\$",
            "LOBFRAG\\$",
            "OBJ\\$",
            "TAB\\$",
            "TABPART\\$",
            "TABCOMPART\\$",
            "TABSUBPART\\$",
            "TS\\$",
            "USER\\$"
        };
        for (const auto& table: sysTables)
            addElement("SYS", table, DbTable::OPTIONS::SYSTEM_TABLE);

        static const std::vector<std::string> xdbTables{
            "XDB\\$TTSET",
            "X\\$NM.*",
            "X\\$PT.*",
            "X\\$QN.*"
        };
        for (const auto& table: xdbTables)
            addElement("XDB", table, DbTable::OPTIONS::SYSTEM_TABLE);
    }

    void Metadata::commitElements() {
        std::unique_lock const lck(mtxSchema);

        for (const SchemaElement* element: schemaElements)
            delete element;
        schemaElements.clear();

        for (SchemaElement* element: newSchemaElements)
            schemaElements.push_back(element);
        newSchemaElements.clear();
    }

    void Metadata::buildMaps(std::vector<std::string>& msgs, std::unordered_map<typeObj, std::string>& tablesUpdated) const {
        for (const SchemaElement* element: schemaElements) {
            if (ctx->isLogLevelAt(Ctx::LOG::DEBUG))
                msgs.push_back("- creating table schema for owner: " + element->owner + " table: " + element->table + " options: " +
                        std::to_string(static_cast<uint>(element->options)));

            schema->buildMaps(element->owner, element->table, element->keyList, element->key, element->tagType, element->tagList, element->tag,
                              element->condition, element->options, tablesUpdated, suppLogDbPrimary, suppLogDbAll, defaultCharacterMapId,
                              defaultCharacterNcharMapId);
        }
    }

    void Metadata::waitForWriter(Thread* t) {
        {
            t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
            std::unique_lock lck(mtxCheckpoint);

            if (status == STATUS::READY) {
                if (unlikely(ctx->isTraceSet(Ctx::TRACE::SLEEP)))
                    ctx->logTrace(Ctx::TRACE::SLEEP, "Metadata:waitForWriter");
                t->contextSet(Thread::CONTEXT::WAIT, Thread::REASON::METADATA_WAIT_WRITER);
                condReplicator.wait(lck);
            }
        }
        t->contextSet(Thread::CONTEXT::CPU);
    }

    void Metadata::waitForReplicator(Thread* t) {
        {
            t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
            std::unique_lock lck(mtxCheckpoint);

            if (status == STATUS::STARTING) {
                if (unlikely(ctx->isTraceSet(Ctx::TRACE::SLEEP)))
                    ctx->logTrace(Ctx::TRACE::SLEEP, "Metadata:waitForReplicator");
                t->contextSet(Thread::CONTEXT::WAIT, Thread::REASON::METADATA_WAIT_FOR_REPLICATOR);
                condWriter.wait(lck);
            }
        }
        t->contextSet(Thread::CONTEXT::CPU);
    }

    void Metadata::setStatusReady(Thread* t) {
        {
            t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
            std::unique_lock const lck(mtxCheckpoint);

            status = STATUS::READY;
            firstDataScn = Scn::none();
            firstSchemaScn = Scn::none();
            checkpointScn = Scn::none();
            schema->scn = Scn::none();
            condWriter.notify_all();
        }
        t->contextSet(Thread::CONTEXT::CPU);
        if (ctx->metrics != nullptr) {
            ctx->metrics->emitServiceStateInitializing(0);
            ctx->metrics->emitServiceStateStarting(0);
            ctx->metrics->emitServiceStateReplicating(0);
            ctx->metrics->emitServiceStateReady(1);
        }
    }

    void Metadata::setStatusStarting(Thread* t) {
        {
            t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
            std::unique_lock const lck(mtxCheckpoint);

            status = STATUS::STARTING;
            condReplicator.notify_all();
        }
        t->contextSet(Thread::CONTEXT::CPU);
        if (ctx->metrics != nullptr) {
            ctx->metrics->emitServiceStateInitializing(0);
            ctx->metrics->emitServiceStateReady(0);
            ctx->metrics->emitServiceStateReplicating(0);
            ctx->metrics->emitServiceStateStarting(1);
        }
    }

    void Metadata::setStatusReplicating(Thread* t) {
        {
            t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
            std::unique_lock const lck(mtxCheckpoint);

            status = STATUS::REPLICATING;
            condReplicator.notify_all();
            condWriter.notify_all();
        }
        t->contextSet(Thread::CONTEXT::CPU);
        if (ctx->metrics != nullptr) {
            ctx->metrics->emitServiceStateInitializing(0);
            ctx->metrics->emitServiceStateStarting(0);
            ctx->metrics->emitServiceStateReady(0);
            ctx->metrics->emitServiceStateReplicating(1);
        }
    }

    void Metadata::wakeUp(Thread* t) {
        {
            t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
            std::unique_lock const lck(mtxCheckpoint);

            condReplicator.notify_all();
            condWriter.notify_all();
        }
        t->contextSet(Thread::CONTEXT::CPU);
    }

    void Metadata::checkpoint(Thread* t, Scn newCheckpointScn, Time newCheckpointTime, Seq newCheckpointSequence, FileOffset newCheckpointFileOffset,
                              uint64_t newCheckpointBytes, Seq newMinSequence, FileOffset newMinFileOffset, Xid newMinXid, Scn newPositionScn) {
        {
            t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
            std::unique_lock const lck(mtxCheckpoint);

            checkpointScn = newCheckpointScn;
            checkpointTime = newCheckpointTime;
            checkpointSequence = newCheckpointSequence;
            checkpointFileOffset = newCheckpointFileOffset;
            checkpointBytes += newCheckpointBytes;
            minSequence = newMinSequence;
            minFileOffset = newMinFileOffset;
            minXid = newMinXid;
            checkpointPositionScn = newPositionScn;
        }
        t->contextSet(Thread::CONTEXT::CPU);
    }

    void Metadata::writeCheckpoint(Thread* t, bool force) {
        std::ostringstream ss;
        std::ostringstream positionSs;
        Scn scn;
        Scn positionScn;
        bool storeSchema = true;

        {
            t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
            std::unique_lock const lck(mtxCheckpoint);
            if (!allowedCheckpoints)
                return;

            // Nothing processed so far
            if (checkpointScn == Scn::none() || lastCheckpointScn == checkpointScn || checkpointSequence == Seq::none())
                return;

            if (lastSequence == sequence && !force &&
                !checkpointIntervalDue(static_cast<uint64_t>(checkpointTime.toEpoch(ctx->hostTimezone) - lastCheckpointTime.toEpoch(ctx->hostTimezone)),
                                       (checkpointBytes - lastCheckpointBytes) / 1024 / 1024, ctx->checkpointIntervalS, ctx->checkpointIntervalMb))
                return;

            // Schema did not change
            if (schema->refScn != Scn::none() && schema->refScn >= schema->scn) {
                if (schemaInterval < ctx->schemaForceInterval) {
                    storeSchema = false;
                    ++schemaInterval;
                } else
                    schemaInterval = 0;
            } else
                schemaInterval = 0;

            serializer->serialize(this, ss, storeSchema);
            scn = checkpointScn;
            positionScn = checkpointPositionScn == Scn::none() ? Scn::zero() : checkpointPositionScn;
            positionSs << R"({"database":")" << database << R"(","scn":)" << scn.toString() << R"(,"position-scn":)" <<
                    positionScn.toString() << "}";

            lastCheckpointScn = checkpointScn;
            lastSequence = sequence;
            lastCheckpointFileOffset = checkpointFileOffset;
            lastCheckpointTime = checkpointTime;
            lastCheckpointBytes = checkpointBytes;
            ++checkpoints;
            checkpointScnList.insert(checkpointScn);
            checkpointSchemaMap.insert_or_assign(checkpointScn, storeSchema);
        }
        t->contextSet(Thread::CONTEXT::CPU);

        const std::string checkpointName = database + "-chkpt-" + scn.toString();

        if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
            ctx->logTrace(Ctx::TRACE::CHECKPOINT, "write scn: " + scn.toString() + " time: " +
                          std::to_string(lastCheckpointTime.getVal()) + " seq: " + lastSequence.toString() + " offset: " +
                          lastCheckpointFileOffset.toString() + " name: " + checkpointName);

        // Durable before the checkpoint is written: a checkpoint without it is taken as written by an older build
        const std::string positionName = database + "-position-" + scn.toString();
        bool written = false;
        if (!stateWrite(positionName, scn, positionSs))
            ctx->warning(60018, "file: " + positionName + " - couldn't write checkpoint");
        else if (!stateWrite(checkpointName, scn, ss))
            // The position file stays: the checkpoint may have reached the disk, else the file is removed at the next start
            ctx->warning(60018, "file: " + checkpointName + " - couldn't write checkpoint");
        else
            written = true;

        t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
        {
            std::unique_lock const lck(mtxCheckpoint);
            if (written)
                checkpointPositionMap.insert_or_assign(scn, positionScn);
            else {
                // Not on disk: not to be deleted later, and the next checkpoint does not refer to its schema
                checkpointScnList.erase(scn);
                checkpointSchemaMap.erase(scn);
                if (storeSchema && schema->refScn == scn)
                    schema->refScn = Scn::none();
            }
        }
        t->contextSet(Thread::CONTEXT::CPU);
    }

    size_t Metadata::checkpointCandidates(const std::set<Scn>& scns, const std::map<Scn, Scn>& positions, Scn firstDataScn,
                                          std::vector<Scn>& candidates) {
        std::vector<Scn> later;
        for (auto it = scns.crbegin(); it != scns.crend(); ++it) {
            if (*it > firstDataScn)
                continue;
            const auto position = positions.find(*it);
            if (position != positions.end() && position->second >= firstDataScn)
                later.push_back(*it);
            else
                candidates.push_back(*it);
        }
        const size_t preferred = candidates.size();
        candidates.insert(candidates.end(), later.begin(), later.end());
        return preferred;
    }

    // A checkpoint whose stream position was at or after the start SCN is passed: positions at the start SCN could have been
    // given before it, by transactions it does not read again. From an earlier one the parser rebuilds them in the same order.
    bool Metadata::readCheckpointPosition(const std::string& name, Scn& positionScn) const {
        std::string content;
        try {
            if (!state->read(name, 1024, content))
                return false;
        } catch (RuntimeException&) {
            // Empty (a kill during the write) or too large
            return false;
        }
        rapidjson::Document document;
        if (content.empty() || document.Parse(content.c_str()).HasParseError() || !document.IsObject() ||
            !document.HasMember("position-scn") || !document["position-scn"].IsUint64())
            return false;
        positionScn = Scn(document["position-scn"].GetUint64());
        return true;
    }

    void Metadata::readCheckpoints() {
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
            ctx->logTrace(Ctx::TRACE::CHECKPOINT, "searching for previous checkpoint information");

        std::set<std::string> namesList;
        state->dropTemporary(database + "-chkpt");
        state->dropTemporary(database + "-position-");
        state->list(namesList);

        const std::string positionPrefix(database + "-position-");
        for (const std::string& name: namesList) {
            Scn scn;
            if (!scnFromName(name, positionPrefix, scn))
                continue;
            Scn positionScn;
            if (!readCheckpointPosition(name, positionScn)) {
                // Written by this build but not readable: its position is unknown, so the checkpoint is tried last
                ctx->warning(60051, "file: " + name + " - checkpoint stream position can't be read, the checkpoint scn: " +
                             scn.toString() + " is used only if no other one at or below the start scn can be");
                positionScn = Scn::none();
            }
            checkpointPositionMap.insert_or_assign(scn, positionScn);
        }

        const std::string prefix(database + "-chkpt-");
        for (const std::string& name: namesList) {
            Scn scn;
            if (!scnFromName(name, prefix, scn))
                continue;

            if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
                ctx->logTrace(Ctx::TRACE::CHECKPOINT, "found: " + name + " scn: " + scn.toString());

            checkpointScnList.insert(scn);
            checkpointSchemaMap.insert_or_assign(scn, true);
        }

        // A position file without its checkpoint: a crash between the two writes, or a deletion by an older build
        for (auto it = checkpointPositionMap.begin(); it != checkpointPositionMap.end();) {
            if (checkpointScnList.find(it->first) == checkpointScnList.end()) {
                static_cast<void>(stateDrop(positionPrefix + it->first.toString()));
                it = checkpointPositionMap.erase(it);
            } else
                ++it;
        }

        if (startScn != Scn::none())
            firstDataScn = startScn;
        else
            firstDataScn = Scn::zero();

        if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
            ctx->logTrace(Ctx::TRACE::CHECKPOINT, "scn: " + firstDataScn.toString());

        if (firstDataScn != Scn::none() && firstDataScn.getData() != 0) {
            std::vector<Scn> candidates;
            const size_t preferred = checkpointCandidates(checkpointScnList, checkpointPositionMap, firstDataScn, candidates);
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (sequence != Seq::none() && sequence != Seq::zero())
                    break;
                if (i == 0 && candidates.size() > preferred)
                    ctx->info(0, "passing " + std::to_string(candidates.size() - preferred) + " checkpoint(s) whose stream position is at " +
                              "or after scn: " + firstDataScn.toString() + ", the newest: " + candidates[preferred].toString());
                if (i == preferred)
                    ctx->warning(60049, "no checkpoint at or below scn: " + firstDataScn.toString() + " with a stream position below it " +
                                 "could be read, using checkpoint scn: " + candidates[i].toString() + "; a transaction committed below " +
                                 "the start position before that checkpoint is not sent again");
                if (checkpointPositionMap.find(candidates[i]) == checkpointPositionMap.end())
                    ctx->info(0, "checkpoint scn: " + candidates[i].toString() + " has no stream position file (written by an older " +
                              "build), used as before");
                readCheckpoint(candidates[i]);
            }
        }
    }

    // Only <prefix><scn> with scn in its canonical decimal form, so the name can be rebuilt from the SCN
    bool Metadata::scnFromName(const std::string& name, const std::string& prefix, Scn& scn) {
        if (name.length() <= prefix.length() || name.compare(0, prefix.length(), prefix) != 0)
            return false;
        const std::string scnStr(name.substr(prefix.length()));
        if (scnStr.find_first_not_of("0123456789") != std::string::npos)
            return false;
        const Scn value(strtoull(scnStr.c_str(), nullptr, 10));
        if (value.toString() != scnStr)
            return false;
        scn = value;
        return true;
    }

    void Metadata::readCheckpoint(Scn scn) {
        std::vector<std::string> msgs;
        std::unordered_map<typeObj, std::string> tablesUpdated;
        ctx->info(0, "reading metadata for " + database + " for scn: " + scn.toString());
        std::string ss;

        const std::string name1(database + "-chkpt-" + scn.toString());
        if (!stateRead(name1, CHECKPOINT_SCHEMA_FILE_MAX_SIZE, ss)) {
            if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
                ctx->logTrace(Ctx::TRACE::CHECKPOINT, "no checkpoint file found, setting unknown sequence");

            sequence = Seq::none();
            return;
        }
        if (!serializer->deserialize(this, ss, name1, msgs, tablesUpdated, true, true)) {
            for (const auto& [_, tableName]: tablesUpdated) {
                ctx->info(0, tableName);
            }
            return;
        }

        for (const auto& msg: msgs)
            ctx->info(0, msg);
        for (const auto& [_, tableName]: tablesUpdated) {
            ctx->info(0, "- found: " + tableName);
        }
        tablesUpdated.clear();

        // Schema missing
        if (schema->scn == Scn::none()) {
            if (schema->refScn == Scn::none()) {
                ctx->warning(60019, "file: " + name1 + " - load checkpoint failed, reference SCN missing");
                return;
            }

            ss.clear();
            const std::string name2(database + "-chkpt-" + schema->refScn.toString());
            ctx->info(0, "reading schema for " + database + " for scn: " + schema->refScn.toString());

            if (!stateRead(name2, CHECKPOINT_SCHEMA_FILE_MAX_SIZE, ss))
                return;

            if (!serializer->deserialize(this, ss, name2, msgs, tablesUpdated, false, true)) {
                for (const auto& msg: msgs) {
                    ctx->info(0, msg);
                }
                return;
            }

            for (const auto& msg: msgs) {
                ctx->info(0, msg);
            }
            for (const auto& [_, tableName]: tablesUpdated) {
                ctx->info(0, "- found: " + tableName);
            }
        }

        if (schema->scn != Scn::none())
            firstSchemaScn = schema->scn;
    }

    void Metadata::deleteOldCheckpoints(Thread* t) {
        std::set<Scn> scnToDrop;
        std::set<Scn> positionToDrop;

        if (ctx->isFlagSet(Ctx::REDO_FLAGS::CHECKPOINT_KEEP))
            return;

        {
            t->contextSet(Thread::CONTEXT::CHKPT, Thread::REASON::CHKPT);
            std::unique_lock const lck(mtxCheckpoint);

            if (!allowedCheckpoints) {
                t->contextSet(Thread::CONTEXT::CPU);
                return;
            }

            if (checkpoints < ctx->checkpointKeep) {
                t->contextSet(Thread::CONTEXT::CPU);
                return;
            }

            bool foundSchema = false;
            uint64_t num = 0;
            auto it = checkpointScnList.cend();
            while (it != checkpointScnList.cbegin()) {
                --it;
                ++num;

                if (num < ctx->checkpointKeep)
                    continue;

                if (!foundSchema) {
                    if (checkpointSchemaMap[*it])
                        foundSchema = true;
                    continue;
                }

                scnToDrop.insert(*it);
            }
            for (auto scn: scnToDrop)
                if (checkpointPositionMap.find(scn) != checkpointPositionMap.end())
                    positionToDrop.insert(scn);
        }

        for (auto scn: scnToDrop) {
            const std::string checkpointName = database + "-chkpt-" + scn.toString();
            if (!stateDrop(checkpointName))
                break;
            if (positionToDrop.count(scn) != 0 && !stateDrop(database + "-position-" + scn.toString()))
                break;
        }

        {
            std::unique_lock const lck(mtxCheckpoint);

            for (auto scn: scnToDrop) {
                checkpointScnList.erase(scn);
                checkpointSchemaMap.erase(scn);
                checkpointPositionMap.erase(scn);
            }
        }
        t->contextSet(Thread::CONTEXT::CPU);
    }

    void Metadata::loadAdaptiveSchema() {
        std::string ss;
        std::vector<std::string> msgs;
        std::unordered_map<typeObj, std::string> tablesUpdated;
        const std::string name("base-" + ctx->versionStr);

        ctx->info(0, "reading adaptive schema from: " + name + ".json");
        const std::string nlsCharset = "AL32UTF8";
        const std::string nlsNcharCharset = "AL16UTF16";
        setNlsCharset(nlsCharset, nlsNcharCharset);

        if (!stateDiskRead(name, CHECKPOINT_SCHEMA_FILE_MAX_SIZE, ss)) {
            ctx->warning(60020, "file: " + name + " - load adaptive schema failed");
            return;
        }

        if (!serializer->deserialize(this, ss, name, msgs, tablesUpdated, false, true)) {
            for (const auto& msg: msgs) {
                ctx->info(0, msg);
            }
            return;
        }

        firstSchemaScn = Scn::zero();
        for (const auto& msg: msgs) {
            ctx->info(0, msg);
        }
        for (const auto& [_, tableName]: tablesUpdated) {
            ctx->info(0, "- found: " + tableName);
        }
    }

    void Metadata::allowCheckpoints() {
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::CHECKPOINT)))
            ctx->logTrace(Ctx::TRACE::CHECKPOINT, "allowing checkpoints");

        std::unique_lock const lck(mtxCheckpoint);
        allowedCheckpoints = true;
    }

    // Inclusive, for transactions and the checkpoints given to the builder alike: positions at firstDataScn are numbered as
    // in the run that sent them first only if both are
    bool Metadata::isDataScn(Scn scn) const {
        return scn >= firstDataScn;
    }

    bool Metadata::isNewData(Scn scn, typeIdx idx) const {
        if (clientScn == Scn::none())
            return true;

        if (clientScn < scn)
            return true;

        if (clientScn == scn && clientIdx < idx)
            return true;

        return false;
    }
}
