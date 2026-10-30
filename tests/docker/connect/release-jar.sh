#!/usr/bin/env bash
# Publishes a patched Debezium Oracle connector jar as a release asset on Aangbaeck/debezium, where the `connect`
# job in .github/workflows/olr-tests.yml downloads it from. Prints the command and its sha256; runs it with --yes.
#   docker/connect/release-jar.sh <version, e.g. 3.7.0.Final-ahlsell.8> <jar> <commit on Aangbaeck/debezium> [--yes]
# Then set DBZ_PATCHED, DBZ_PATCHED_URL and DBZ_PATCHED_SHA256 in the workflow to the printed values.
set -euo pipefail
[ $# -ge 3 ] || { sed -n '2,5p' "$0"; exit 2; }
version=$1 jar=$2 commit=$3
repo=Aangbaeck/debezium
tag="v$version"
sha=$(sha256sum "$jar" | cut -d' ' -f1)
name=$(basename "$jar")
cmd=(gh release create "$tag" "$jar" --repo "$repo" --target "$commit" --title "$tag"
     --notes "Debezium Oracle connector $version built from $commit. $name sha256 $sha")
printf '%q ' "${cmd[@]}"; echo
echo "DBZ_PATCHED: $version"
echo "DBZ_PATCHED_URL: https://github.com/$repo/releases/download/$tag/$name"
echo "DBZ_PATCHED_SHA256: $sha"
if [ "${4:-}" = --yes ]; then "${cmd[@]}"; fi
