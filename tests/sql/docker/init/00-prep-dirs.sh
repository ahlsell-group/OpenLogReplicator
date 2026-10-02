#!/bin/bash
# Fast recovery area inside the data volume, so the OLR container sees archived redo at the same path.
set -e
mkdir -p /opt/oracle/oradata/fra
chmod 775 /opt/oracle/oradata/fra
