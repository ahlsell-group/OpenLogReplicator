# Dockerfile for OpenLogReplicator
# Copyright (C) 2018-2026 Adam Leszczynski (aleszczynski@bersler.com)
#
# This file is part of OpenLogReplicator
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
#
# OpenLogReplicator Dockerfile for the test harness (tests/)
# --------------------------------------------------------------
# Based on the upstream image Dockerfile. The build context is always the repository root, so
# the image can be built from the local checkout (OPENLOGREPLICATOR_VERSION=local, the default)
# or from a GitHub repository at a tag, branch or commit:
#
#   docker build -f tests/docker/olr/Dockerfile -t olrt/olr:local .
#   docker build -f tests/docker/olr/Dockerfile -t olrt/olr:2.0.0 \
#       --build-arg OPENLOGREPLICATOR_REPO=bersler/OpenLogReplicator --build-arg OPENLOGREPLICATOR_VERSION=2.0.0 .
#
# A version is tried as tag v<version> first, then as any ref (branch or commit).
# `make -C tests image` and `make -C tests image-ref REF=<repo>@<ref>` wrap these.

ARG IMAGE=debian
ARG VERSION=13.0
FROM ${IMAGE}:${VERSION} AS builder

# "local" builds the checkout in the build context; anything else is downloaded from GitHub
ARG OPENLOGREPLICATOR_REPO=ahlsell-group/OpenLogReplicator
ARG OPENLOGREPLICATOR_VERSION=local
ARG ARCH=x86_64
ARG GIDOLR=1001
ARG UIDOLR=1001
ARG GIDORA=54322
ARG BUILD_TYPE=Release
ARG WITHKAFKA=1
ARG WITHPROMETHEUS
ARG WITHORACLE=1
ARG WITHPROTOBUF=1

LABEL org.opencontainers.image.authors="Adam Leszczynski <aleszczynski@bersler.com>"

ENV LC_ALL=C
ENV LANG=en_US.UTF-8
ENV ORACLE_MAJOR=23
ENV ORACLE_MINOR=26
ENV PROTOBUF_VERSION_DIR=21.12
# latest is 29.3
ENV PROTOBUF_VERSION=3.21.12
ENV RAPIDJSON_VERSION=1.1.0
ENV LIBRDKAFKA_VERSION=2.13.0
ENV PROMETHEUS_VERSION=1.3.0
ENV OPENLOGREPLICATOR_VERSION=${OPENLOGREPLICATOR_VERSION}
ENV LD_LIBRARY_PATH=/opt/instantclient_${ORACLE_MAJOR}_${ORACLE_MINOR}:/opt/librdkafka/lib:/opt/prometheus/lib:/opt/protobuf/lib
ENV BUILDARGS="-DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DWITH_RAPIDJSON=/opt/rapidjson -S ../ -B ./"
ENV BUILDARGS="${BUILDARGS}${WITHKAFKA:+ -DWITH_RDKAFKA=/opt/librdkafka}"
ENV BUILDARGS="${BUILDARGS}${WITHPROMETHEUS:+ -DWITH_PROMETHEUS=/opt/prometheus}"
ENV BUILDARGS="${BUILDARGS}${WITHORACLE:+ -DWITH_OCI=/opt/instantclient_${ORACLE_MAJOR}_${ORACLE_MINOR}}"
ENV BUILDARGS="${BUILDARGS}${WITHPROTOBUF:+ -DWITH_PROTOBUF=/opt/protobuf}"
ENV COMPILEKAFKA="${WITHKAFKA:+1}"
ENV COMPILEPROMETHEUS="${WITHPROMETHEUS:+1}"
ENV COMPILEORACLE="${WITHORACLE:+1}"
ENV COMPILEPROTOBUF="${WITHPROTOBUF:+1}"
ENV DEBIAN_FRONTEND=noninteractive

COPY tests/docker/olr/run.sh /opt
# only the sources the build needs (keeps tests/results and build directories out of the context)
COPY CMakeLists.txt config.h.in /tmp/local/OpenLogReplicator/
COPY src /tmp/local/OpenLogReplicator/src
COPY proto /tmp/local/OpenLogReplicator/proto
COPY scripts /tmp/local/OpenLogReplicator/scripts

RUN set -eu ; \
    if [ -r /etc/centos-release ]; then \
        yum -y install autoconf automake diffutils file gcc gcc-c++ libaio libaio-devel libasan libubsan libnsl libtool make patch tar unzip wget zlib-devel git curl ; \
    fi ; \
    if [ -r /etc/debian_version ]; then \
        apt-get update ; \
        if [ "$(cat /etc/debian_version)" = "12.0" ]; then \
            apt-get -y install file gcc g++ libaio1 libasan8 libubsan1 libtool libz-dev make patch unzip wget cmake git curl ; \
        elif [ "$(cat /etc/debian_version)" = "13.0" ]; then \
            apt-get -y install file gcc g++ libaio1t64 libasan8 libubsan1 libtool libz-dev make patch unzip wget cmake git curl ; \
            ln -s libaio.so.1t64 /usr/lib/x86_64-linux-gnu/libaio.so.1 ; \
        fi ; \
    fi ; \
    cd /opt ; \
    wget https://github.com/Tencent/rapidjson/archive/refs/tags/v${RAPIDJSON_VERSION}.tar.gz ; \
    tar xzvf v${RAPIDJSON_VERSION}.tar.gz ; \
    rm v${RAPIDJSON_VERSION}.tar.gz ; \
    ln -s rapidjson-${RAPIDJSON_VERSION} rapidjson ; \
    if [ "${RAPIDJSON_VERSION}" = "1.1.0" ]; then \
        cd rapidjson ; \
        wget https://github.com/Tencent/rapidjson/commit/3b2441b87f99ab65f37b141a7b548ebadb607b96.diff ; \
        patch -p1 < 3b2441b87f99ab65f37b141a7b548ebadb607b96.diff ; \
        rm 3b2441b87f99ab65f37b141a7b548ebadb607b96.diff ; \
        cd .. ; \
    fi ; \
    if [ "${COMPILEORACLE}" != "" ]; then \
        cd /opt ; \
        wget https://download.oracle.com/otn_software/linux/instantclient/${ORACLE_MAJOR}${ORACLE_MINOR}000/instantclient-basic-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0.zip ; \
        unzip -o instantclient-basic-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0.zip ; \
        rm instantclient-basic-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0.zip ; \
        rm -rf META-INF ; \
        wget https://download.oracle.com/otn_software/linux/instantclient/${ORACLE_MAJOR}${ORACLE_MINOR}000/instantclient-sdk-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0.zip ; \
        unzip -o instantclient-sdk-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0.zip ; \
        rm instantclient-sdk-linux.x64-${ORACLE_MAJOR}.${ORACLE_MINOR}.0.0.0.zip ; \
        rm -rf META-INF ; \
        cd /opt/instantclient_${ORACLE_MAJOR}_${ORACLE_MINOR} ; \
    fi ; \
    if [ "${COMPILEKAFKA}" != "" ]; then \
        cd /opt ; \
        wget https://github.com/confluentinc/librdkafka/archive/refs/tags/v${LIBRDKAFKA_VERSION}.tar.gz ; \
        tar xzvf v${LIBRDKAFKA_VERSION}.tar.gz ; \
        rm v${LIBRDKAFKA_VERSION}.tar.gz ; \
        cd /opt/librdkafka-${LIBRDKAFKA_VERSION} ; \
        ./configure --prefix=/opt/librdkafka ; \
        make ; \
        make install ; \
    fi ; \
    if [ "${COMPILEPROMETHEUS}" != "" ]; then \
        cd /opt ; \
        wget https://github.com/jupp0r/prometheus-cpp/releases/download/v${PROMETHEUS_VERSION}/prometheus-cpp-with-submodules.tar.gz ; \
        tar xzvf prometheus-cpp-with-submodules.tar.gz ; \
        rm prometheus-cpp-with-submodules.tar.gz ; \
        cd /opt/prometheus-cpp-with-submodules ; \
        mkdir _build ; \
        cd _build ; \
        cmake .. -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX:PATH=/opt/prometheus -DENABLE_PUSH=OFF -DENABLE_COMPRESSION=OFF ; \
        cmake --build . --parallel 4 ; \
        ctest -V ; \
        cmake --install . ; \
    fi ; \
    if [ "${COMPILEPROTOBUF}" != "" ]; then \
        cd /opt ; \
        wget https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOBUF_VERSION_DIR}/protobuf-cpp-${PROTOBUF_VERSION}.tar.gz ; \
        tar xzvf protobuf-cpp-${PROTOBUF_VERSION}.tar.gz ; \
        rm protobuf-cpp-${PROTOBUF_VERSION}.tar.gz ; \
        cd /opt/protobuf-${PROTOBUF_VERSION} ; \
        ./configure --prefix=/opt/protobuf ; \
        make ; \
        make install ; \
    fi ; \
    cd /opt ; \
    if [ "${OPENLOGREPLICATOR_VERSION}" = "local" ]; then \
        cp -r /tmp/local/OpenLogReplicator OpenLogReplicator-src ; \
    else \
        wget -O olr-src.tar.gz https://github.com/${OPENLOGREPLICATOR_REPO}/archive/refs/tags/v${OPENLOGREPLICATOR_VERSION}.tar.gz \
            || wget -O olr-src.tar.gz https://github.com/${OPENLOGREPLICATOR_REPO}/archive/${OPENLOGREPLICATOR_VERSION}.tar.gz ; \
        mkdir OpenLogReplicator-src ; \
        tar xzf olr-src.tar.gz --strip-components=1 -C OpenLogReplicator-src ; \
        rm olr-src.tar.gz ; \
    fi ; \
    cd /opt/OpenLogReplicator-src ; \
    if [ "${COMPILEPROTOBUF}" != "" ]; then \
        cd proto ; \
        /opt/protobuf/bin/protoc OraProtoBuf.proto --cpp_out=. ; \
        mv OraProtoBuf.pb.cc ../src/common/OraProtoBuf.pb.cpp ; \
        mv OraProtoBuf.pb.h ../src/common/OraProtoBuf.pb.h ; \
        cd .. ; \
    fi ; \
    mkdir cmake-build-${BUILD_TYPE}-${ARCH} ; \
    cd cmake-build-${BUILD_TYPE}-${ARCH} ; \
    cmake ${BUILDARGS} ; \
    cmake --build ./ --target OpenLogReplicator -j ; \
    mkdir /opt/OpenLogReplicator ; \
    mkdir /opt/OpenLogReplicator/log ; \
    mkdir /opt/OpenLogReplicator/tmp ; \
    mkdir /opt/OpenLogReplicator/scripts ; \
    mv ./OpenLogReplicator /opt/OpenLogReplicator ; \
    cp -p /opt/OpenLogReplicator-src/scripts/gencfg.sql /opt/OpenLogReplicator/scripts/gencfg.sql ; \
    mkdir /home/user1 ; \
    groupadd -g ${GIDOLR} user1 ; \
    if [ "${GIDOLR}" != "${GIDORA}" ]; then \
        groupadd -g ${GIDORA} oracle ; \
        useradd -u ${UIDOLR} user1 -g user1 -G oracle -d /home/user1 ; \
    else \
        useradd -u ${UIDOLR} user1 -g user1 -d /home/user1 ; \
    fi ; \
    chown -R user1:user1 /home/user1 ; \
    chown -R user1:user1 /opt/OpenLogReplicator ; \
    rm -rf /tmp/local ; \
    if [ "${BUILD_TYPE}" != "Debug" ]; then \
        rm -rf /opt/OpenLogReplicator-src /opt/rapidjson /opt/rapidjson-${RAPIDJSON_VERSION} ; \
        if [ "${COMPILEKAFKA}" != "" ]; then \
            rm -rf /opt/librdkafka-${LIBRDKAFKA_VERSION} ; \
        fi ; \
        if [ "${COMPILEPROTOBUF}" != "" ]; then \
            rm -rf /opt/protobuf-${PROTOBUF_VERSION} ; \
        fi ; \
        if [ "${COMPILEPROMETHEUS}" != "" ]; then \
            rm -rf /opt/prometheus-cpp-with-submodules ; \
        fi ; \
        if [ -r /etc/centos-release ]; then \
            yum -y remove autoconf automake file gcc gcc-c++ libaio-devel libtool make patch unzip wget zlib-devel git ; \
            yum -y autoremove ; \
            yum clean all ; \
        rm -rf /var/cache/yum ; \
        fi ; \
        if [ -r /etc/debian_version ]; then \
            apt-get -y remove file gcc g++ libtool libz-dev make unzip wget git ; \
            apt-get -y autoremove ; \
            apt-get clean ; \
            rm -rf /var/lib/apt/lists/* ; \
        fi ; \
    fi

USER user1:oracle
RUN set -eu ; \
    export LD_LIBRARY_PATH=/opt/instantclient_${ORACLE_MAJOR}_${ORACLE_MINOR}:/opt/librdkafka/lib:/opt/prometheus/lib:/opt/protobuf/lib ; \
    /opt/OpenLogReplicator/OpenLogReplicator --version

WORKDIR /opt/OpenLogReplicator
ENTRYPOINT ["/opt/run.sh"]
