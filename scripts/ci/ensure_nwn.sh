#!/usr/bin/env bash
set -euo pipefail

# Ensure NWN dedicated data exists at $NWN_ROOT.
# This is used by CI and can be run locally.

NWN_ROOT="${NWN_ROOT:-$(pwd)/nwn}"
ZIP_URL="https://nwn.beamdog.net/downloads/nwnee-dedicated-8193.34.zip"
ZIP_NAME="${ZIP_NAME:-nwnee-dedicated-8193.34.zip}"

if [[ -d "${NWN_ROOT}" ]] && [[ -n "$(ls -A "${NWN_ROOT}" 2>/dev/null || true)" ]]; then
  exit 0
fi

mkdir -p "${NWN_ROOT}"

if [[ ! -f "${ZIP_NAME}" ]]; then
  curl -L "${ZIP_URL}" -o "${ZIP_NAME}"
fi

unzip -q "${ZIP_NAME}" -d "${NWN_ROOT}"
