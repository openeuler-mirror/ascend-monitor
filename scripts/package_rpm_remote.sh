#!/usr/bin/env bash
set -euo pipefail

REMOTE_HOST="${1:-10.40.125.244}"
REMOTE_DIR="${2:-~/ascend-monitor-rpm}"
PACKAGE_OUT_DIR="${3:-dist-rpm}"

printf '==> Remote host: %s\n' "$REMOTE_HOST"
printf '==> Remote dir : %s\n' "$REMOTE_DIR"

mkdir -p "$PACKAGE_OUT_DIR"

printf '==> Sync project to remote\n'
tar czf - \
  --exclude=.git \
  --exclude=build \
  --exclude=build-debug \
  --exclude="$PACKAGE_OUT_DIR" \
  . | ssh "$REMOTE_HOST" "rm -rf $REMOTE_DIR && mkdir -p $REMOTE_DIR && tar xzf - -C $REMOTE_DIR"

printf '==> Build and package RPM on remote\n'
ssh "$REMOTE_HOST" "cd $REMOTE_DIR && \
  cmake -S . -B build-rpm -DCMAKE_BUILD_TYPE=Release && \
  cmake --build build-rpm -j\$(nproc) && \
  cpack --config build-rpm/CPackConfig.cmake -G RPM"

printf '==> Verify RPM metadata and file list on remote\n'
ssh "$REMOTE_HOST" "cd $REMOTE_DIR/build-rpm && \
  RPM_FILE=\$(ls -1 *.rpm | head -n 1) && \
  echo \"RPM file: \$RPM_FILE\" && \
  rpm -qpi \"\$RPM_FILE\" && \
  rpm -qpl \"\$RPM_FILE\""

printf '==> Download RPM to local output directory\n'
scp "$REMOTE_HOST:$REMOTE_DIR/build-rpm/*.rpm" "$PACKAGE_OUT_DIR/"

printf '==> Done. Local RPM artifacts:\n'
ls -1 "$PACKAGE_OUT_DIR"/*.rpm
