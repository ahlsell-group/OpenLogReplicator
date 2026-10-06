#!/bin/bash
# FRA inside the shared volume, so OLR containers see archived redo on the same path.
set -e
mkdir -p /opt/oracle/oradata/fra
chmod 775 /opt/oracle/oradata/fra
