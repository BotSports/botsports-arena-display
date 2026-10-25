#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CURRENT_VERSION=$(grep -E '^#define[[:space:]]+FIRMWARE_VERSION' config.h | sed -E 's/.*"([^"]+)".*/\1/')
IFS='.' read -r MAJOR MINOR PATCH <<< "${CURRENT_VERSION%%-*}"

BUMP_ARG="${1:-patch}"

case "$BUMP_ARG" in
  major)
    NEW_VERSION="$((MAJOR + 1)).0.0"
    ;;
  minor)
    NEW_VERSION="${MAJOR}.$((MINOR + 1)).0"
    ;;
  patch)
    NEW_VERSION="${MAJOR}.${MINOR}.$((PATCH + 1))"
    ;;
  [0-9]*.[0-9]*.[0-9]*)
    NEW_VERSION="${BUMP_ARG#v}"
    NEW_VERSION="${NEW_VERSION#V}"
    ;;
  *)
    echo "Usage: ./release.sh [patch|minor|major|X.Y.Z]" >&2
    exit 1
    ;;
esac

TAG="Release-V${NEW_VERSION}"

echo "=================================================="
echo " Preparing Release: ${CURRENT_VERSION} -> ${NEW_VERSION} (${TAG})"
echo "=================================================="

# 1. Update FIRMWARE_VERSION in config.h and fallback labels in wifi_portal.cpp
sed -i -E "s/^#define[[:space:]]+FIRMWARE_VERSION[[:space:]]+\"[^\"]+\"/#define FIRMWARE_VERSION    \"${NEW_VERSION}\"/" config.h
sed -i -E "s/(id=\"fw_version_badge\"[^>]*>)v[^<]+/\1v${NEW_VERSION}/" wifi_portal.cpp
sed -i -E "s/(id=\"ota_ver_label\"[^>]*>)v[^<]+/\1v${NEW_VERSION}/" wifi_portal.cpp

# 2. Commit version bump if working tree has changes
git add config.h wifi_portal.cpp
if ! git diff --cached --quiet; then
  git commit -m "chore(release): bump firmware version to v${NEW_VERSION}"
fi

# 3. Create annotated tag Release-VX.X.X
git tag -a "$TAG" -m "Release v${NEW_VERSION}"

echo "Created commit and tag: ${TAG}"
echo "To push and trigger the GitHub Actions release build, run:"
echo "  git push origin main --follow-tags"
