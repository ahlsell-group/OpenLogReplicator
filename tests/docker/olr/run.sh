#!/bin/sh
# Start script for Docker
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

cd /opt/OpenLogReplicator
FLAG_FILE=/opt/OpenLogReplicator/log/.olr_started.flag
if [ -x /opt/bin/OpenLogReplicator ]; then
    OLR_EXEC=/opt/bin/OpenLogReplicator
else
    OLR_EXEC=./OpenLogReplicator
fi

if [ ! -f "$FLAG_FILE" ]; then
    # first execution, provided arguments for startup
    touch "$FLAG_FILE"
    ${OLR_EXEC} "$@" 2>&1 | tee -a /opt/OpenLogReplicator/log/OpenLogReplicator.err
else
    # subsequent execution, start normally
    ${OLR_EXEC} 2>&1 | tee -a /opt/OpenLogReplicator/log/OpenLogReplicator.err
fi
