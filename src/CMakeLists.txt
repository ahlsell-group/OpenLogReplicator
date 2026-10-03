# Copyright (C) 2018-2026 Adam Leszczynski (aleszczynski@bersler.com)
#
# This file is part of OpenLogReplicator.
#
# This program is free software: you can redistribute it and/or
# modify it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public
# License along with this program; see the file LICENSE;
# If not, see <http://www.gnu.org/licenses/>.

list(APPEND ListCommon
        common/ClockHW.cpp
        common/Ctx.cpp
        common/DbLob.cpp
        common/DbTable.cpp
        common/LobCtx.cpp
        common/LobData.cpp
        common/LobKey.cpp
        common/MemoryManager.cpp
        common/Thread.cpp
        common/TimeZone.cpp
        common/XmlCtx.cpp
        common/exception/BootException.cpp
        common/exception/ConfigurationException.cpp
        common/exception/DataException.cpp
        common/exception/NetworkException.cpp
        common/exception/RedoLogException.cpp
        common/exception/RuntimeException.cpp
        common/expression/BoolValue.cpp
        common/expression/Expression.cpp
        common/expression/StringValue.cpp
        common/expression/Token.cpp
        common/metrics/Metrics.cpp
        common/types/Data.cpp)

list(APPEND ListReplicator
        replicator/Replicator.cpp
        replicator/ReplicatorBatch.cpp)

list(APPEND ListLocales
        locales/CharacterSet.cpp
        locales/CharacterSet16bit.cpp
        locales/CharacterSet7bit.cpp
        locales/CharacterSet8bit.cpp
        locales/CharacterSetAL16UTF16.cpp
        locales/CharacterSetAL32UTF8.cpp
        locales/CharacterSetJA16EUC.cpp
        locales/CharacterSetJA16EUCTILDE.cpp
        locales/CharacterSetJA16SJIS.cpp
        locales/CharacterSetJA16SJISTILDE.cpp
        locales/CharacterSetKO16KSCCS.cpp
        locales/CharacterSetUTF8.cpp
        locales/CharacterSetZHS16GBK.cpp
        locales/CharacterSetZHS32GB18030.cpp
        locales/CharacterSetZHT16HKSCS31.cpp
        locales/CharacterSetZHT32EUC.cpp
        locales/CharacterSetZHT32TRIS.cpp
        locales/Locales.cpp)

list(APPEND ListBuilder
        builder/Builder.cpp
        builder/BuilderJson.cpp
        builder/SystemTransaction.cpp)

list(APPEND ListParser
        parser/Parser.cpp
        parser/Transaction.cpp
        parser/TransactionBuffer.cpp)

list(APPEND ListReader
        reader/Reader.cpp
        reader/ReaderFilesystem.cpp)

list(APPEND ListMetadata
        metadata/Checkpoint.cpp
        metadata/Metadata.cpp
        metadata/Schema.cpp
        metadata/Serializer.cpp
        metadata/SerializerJson.cpp)

list(APPEND ListState
        state/State.cpp
        state/StateDisk.cpp)

list(APPEND ListWriter
        writer/Writer.cpp
        writer/WriterDiscard.cpp
        writer/WriterFile.cpp)

if (WITH_OCI)
    list(APPEND ListReplicator
            replicator/DatabaseConnection.cpp
            replicator/DatabaseEnvironment.cpp
            replicator/DatabaseStatement.cpp
            replicator/ReplicatorOnline.cpp)
endif ()

if (WITH_RDKAFKA)
    list(APPEND ListWriter
            writer/WriterKafka.cpp)
endif ()

if (WITH_PROMETHEUS)
    list(APPEND ListCommon
            common/metrics/MetricsPrometheus.cpp)
endif ()

if (WITH_PROTOBUF)
    list(APPEND ListCommon
            common/OraProtoBuf.pb.cpp)
    list(APPEND ListBuilder
            builder/BuilderProtobuf.cpp)
    list(APPEND ListStream
            stream/Stream.cpp
            stream/StreamNetwork.cpp)
    list(APPEND ListWriter
            writer/WriterStream.cpp)

    if (WITH_ZEROMQ)
        list(APPEND ListStream
                stream/StreamZeroMQ.cpp)
    endif ()
endif ()

add_library(LibCommon OBJECT ${ListCommon})
add_library(LibReplicator OBJECT ${ListReplicator})
add_library(LibLocales OBJECT ${ListLocales})
add_library(LibBuilder OBJECT ${ListBuilder})
add_library(LibParser OBJECT ${ListParser})
add_library(LibReader OBJECT ${ListReader})
add_library(LibMetadata OBJECT ${ListMetadata})
add_library(LibState OBJECT ${ListState})
add_library(LibWriter OBJECT ${ListWriter})

target_sources(OpenLogReplicator PUBLIC OpenLogReplicator.cpp main.cpp)
target_link_libraries(OpenLogReplicator LibCommon)
target_link_libraries(OpenLogReplicator LibReplicator)
target_link_libraries(OpenLogReplicator LibLocales)
target_link_libraries(OpenLogReplicator LibBuilder)
target_link_libraries(OpenLogReplicator LibParser)
target_link_libraries(OpenLogReplicator LibReader)
target_link_libraries(OpenLogReplicator LibMetadata)
target_link_libraries(OpenLogReplicator LibState)
target_link_libraries(OpenLogReplicator LibWriter)

if (WITH_PROTOBUF)
    add_library(LibStream ${ListStream})
    target_link_libraries(OpenLogReplicator LibStream)

    target_sources(StreamClient PUBLIC StreamClient.cpp)
    target_link_libraries(StreamClient LibCommon)
    target_link_libraries(StreamClient LibStream)
endif ()

if (WITH_TESTS)
    add_executable(TimeZoneTest
            test/TimeZoneTest.cpp
            common/TimeZone.cpp
            common/types/Data.cpp
            common/exception/DataException.cpp
            common/exception/RuntimeException.cpp)
    add_test(NAME TimeZoneTest COMMAND TimeZoneTest)

    add_executable(BuilderScnTest test/BuilderScnTest.cpp)
    target_include_directories(BuilderScnTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(BuilderScnTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(BuilderScnTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(BuilderScnTest static_rdkafka)
        else ()
            target_link_libraries(BuilderScnTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(BuilderScnTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(BuilderScnTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(BuilderScnTest static_protobuf)
        else ()
            target_link_libraries(BuilderScnTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(BuilderScnTest zmq)
        endif ()
    endif ()
    add_test(NAME BuilderScnTest COMMAND BuilderScnTest)

    add_executable(XidTest
            test/XidTest.cpp
            common/exception/DataException.cpp)
    target_include_directories(XidTest PUBLIC "${PROJECT_BINARY_DIR}")
    add_test(NAME XidTest COMMAND XidTest)

    add_executable(BuilderStartScnTest test/BuilderStartScnTest.cpp)
    target_include_directories(BuilderStartScnTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(BuilderStartScnTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(BuilderStartScnTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(BuilderStartScnTest static_rdkafka)
        else ()
            target_link_libraries(BuilderStartScnTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(BuilderStartScnTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(BuilderStartScnTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(BuilderStartScnTest static_protobuf)
        else ()
            target_link_libraries(BuilderStartScnTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(BuilderStartScnTest zmq)
        endif ()
    endif ()
    add_test(NAME BuilderStartScnTest COMMAND BuilderStartScnTest)
    add_executable(BuilderMsgIdxTest test/BuilderMsgIdxTest.cpp)
    target_include_directories(BuilderMsgIdxTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(BuilderMsgIdxTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(BuilderMsgIdxTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(BuilderMsgIdxTest static_rdkafka)
        else ()
            target_link_libraries(BuilderMsgIdxTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(BuilderMsgIdxTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(BuilderMsgIdxTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(BuilderMsgIdxTest static_protobuf)
        else ()
            target_link_libraries(BuilderMsgIdxTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(BuilderMsgIdxTest zmq)
        endif ()
    endif ()
    add_test(NAME BuilderMsgIdxTest COMMAND BuilderMsgIdxTest)

    add_executable(CheckpointMinTranTest test/CheckpointMinTranTest.cpp)
    target_include_directories(CheckpointMinTranTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(CheckpointMinTranTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(CheckpointMinTranTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(CheckpointMinTranTest static_rdkafka)
        else ()
            target_link_libraries(CheckpointMinTranTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(CheckpointMinTranTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(CheckpointMinTranTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(CheckpointMinTranTest static_protobuf)
        else ()
            target_link_libraries(CheckpointMinTranTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(CheckpointMinTranTest zmq)
        endif ()
    endif ()
    add_test(NAME CheckpointMinTranTest COMMAND CheckpointMinTranTest)

    add_executable(OpCode0501SuppLogTest test/OpCode0501SuppLogTest.cpp)
    target_include_directories(OpCode0501SuppLogTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(OpCode0501SuppLogTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(OpCode0501SuppLogTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(OpCode0501SuppLogTest static_rdkafka)
        else ()
            target_link_libraries(OpCode0501SuppLogTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(OpCode0501SuppLogTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(OpCode0501SuppLogTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(OpCode0501SuppLogTest static_protobuf)
        else ()
            target_link_libraries(OpCode0501SuppLogTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(OpCode0501SuppLogTest zmq)
        endif ()
    endif ()
    add_test(NAME OpCode0501SuppLogTest COMMAND OpCode0501SuppLogTest)

    add_executable(NumberDecodeTest test/NumberDecodeTest.cpp)
    target_include_directories(NumberDecodeTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(NumberDecodeTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(NumberDecodeTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(NumberDecodeTest static_rdkafka)
        else ()
            target_link_libraries(NumberDecodeTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(NumberDecodeTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(NumberDecodeTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(NumberDecodeTest static_protobuf)
        else ()
            target_link_libraries(NumberDecodeTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(NumberDecodeTest zmq)
        endif ()
    endif ()
    add_test(NAME NumberDecodeTest COMMAND NumberDecodeTest)

    add_executable(BinaryFloatDecodeTest test/BinaryFloatDecodeTest.cpp)
    target_include_directories(BinaryFloatDecodeTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(BinaryFloatDecodeTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(BinaryFloatDecodeTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(BinaryFloatDecodeTest static_rdkafka)
        else ()
            target_link_libraries(BinaryFloatDecodeTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(BinaryFloatDecodeTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(BinaryFloatDecodeTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(BinaryFloatDecodeTest static_protobuf)
        else ()
            target_link_libraries(BinaryFloatDecodeTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(BinaryFloatDecodeTest zmq)
        endif ()
    endif ()
    add_test(NAME BinaryFloatDecodeTest COMMAND BinaryFloatDecodeTest)

    add_executable(CheckpointIntervalTest test/CheckpointIntervalTest.cpp)
    target_include_directories(CheckpointIntervalTest PUBLIC "${PROJECT_BINARY_DIR}")
    add_test(NAME CheckpointIntervalTest COMMAND CheckpointIntervalTest)

    add_executable(DirectLoadTest test/DirectLoadTest.cpp)
    target_include_directories(DirectLoadTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(DirectLoadTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(DirectLoadTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(DirectLoadTest static_rdkafka)
        else ()
            target_link_libraries(DirectLoadTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(DirectLoadTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(DirectLoadTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(DirectLoadTest static_protobuf)
        else ()
            target_link_libraries(DirectLoadTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(DirectLoadTest zmq)
        endif ()
    endif ()
    add_test(NAME DirectLoadTest COMMAND DirectLoadTest)

    add_executable(WriterContinueGapTest test/WriterContinueGapTest.cpp)
    target_include_directories(WriterContinueGapTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(WriterContinueGapTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(WriterContinueGapTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(WriterContinueGapTest static_rdkafka)
        else ()
            target_link_libraries(WriterContinueGapTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(WriterContinueGapTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(WriterContinueGapTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(WriterContinueGapTest static_protobuf)
        else ()
            target_link_libraries(WriterContinueGapTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(WriterContinueGapTest zmq)
        endif ()
    endif ()
    add_test(NAME WriterContinueGapTest COMMAND WriterContinueGapTest)

    add_executable(BuilderDeleteNullsTest test/BuilderDeleteNullsTest.cpp)
    target_include_directories(BuilderDeleteNullsTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(BuilderDeleteNullsTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(BuilderDeleteNullsTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(BuilderDeleteNullsTest static_rdkafka)
        else ()
            target_link_libraries(BuilderDeleteNullsTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(BuilderDeleteNullsTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(BuilderDeleteNullsTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(BuilderDeleteNullsTest static_protobuf)
        else ()
            target_link_libraries(BuilderDeleteNullsTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(BuilderDeleteNullsTest zmq)
        endif ()
    endif ()
    add_test(NAME BuilderDeleteNullsTest COMMAND BuilderDeleteNullsTest)

    add_executable(BuilderXidTest test/BuilderXidTest.cpp)
    target_include_directories(BuilderXidTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(BuilderXidTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(BuilderXidTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(BuilderXidTest static_rdkafka)
        else ()
            target_link_libraries(BuilderXidTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(BuilderXidTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(BuilderXidTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(BuilderXidTest static_protobuf)
        else ()
            target_link_libraries(BuilderXidTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(BuilderXidTest zmq)
        endif ()
    endif ()
    add_test(NAME BuilderXidTest COMMAND BuilderXidTest)

    add_executable(BuilderStartScnOutputTest test/BuilderStartScnOutputTest.cpp)
    target_include_directories(BuilderStartScnOutputTest PUBLIC "${PROJECT_BINARY_DIR}")
    target_link_libraries(BuilderStartScnOutputTest LibCommon LibReplicator LibLocales LibBuilder LibParser LibReader LibMetadata LibState LibWriter Threads::Threads)
    if (WITH_OCI)
        target_link_libraries(BuilderStartScnOutputTest clntshcore nnz clntsh)
    endif ()
    if (WITH_RDKAFKA)
        if (WITH_STATIC)
            target_link_libraries(BuilderStartScnOutputTest static_rdkafka)
        else ()
            target_link_libraries(BuilderStartScnOutputTest rdkafka++ rdkafka)
        endif ()
    endif ()
    if (WITH_PROMETHEUS)
        target_link_libraries(BuilderStartScnOutputTest prometheus-cpp-core prometheus-cpp-pull)
    endif ()
    if (WITH_PROTOBUF)
        target_link_libraries(BuilderStartScnOutputTest LibStream)
        if (WITH_STATIC)
            target_link_libraries(BuilderStartScnOutputTest static_protobuf)
        else ()
            target_link_libraries(BuilderStartScnOutputTest protobuf)
        endif ()
        if (WITH_ZEROMQ)
            target_link_libraries(BuilderStartScnOutputTest zmq)
        endif ()
    endif ()
    add_test(NAME BuilderStartScnOutputTest COMMAND BuilderStartScnOutputTest)

    add_test(NAME ExitCodeTest COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test/ExitCodeTest.sh $<TARGET_FILE:OpenLogReplicator>)
endif ()
