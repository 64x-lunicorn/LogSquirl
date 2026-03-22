#!/usr/bin/env bash
# fix_macos_frameworks.sh — Repair Qt framework bundle symlinks after macdeployqt
#
# macdeployqt (especially Qt 6) copies files instead of creating the required
# versioned symlink structure. Without proper symlinks codesign fails with
# "bundle format is ambiguous (could be app or framework)".
#
# We rebuild the canonical layout for every framework inside the app bundle:
#   Versions/Current -> A  (symlink)
#   QtFoo            -> Versions/Current/QtFoo  (symlink)
#   Resources        -> Versions/Current/Resources  (symlink)
#
# Usage: fix_macos_frameworks.sh <path-to-app-bundle>
#   e.g.  fix_macos_frameworks.sh ./output/logsquirl.app

set -euo pipefail

APP="${1:?Usage: $0 <path-to-.app>}"
FWDIR="$APP/Contents/Frameworks"

if [ ! -d "$FWDIR" ]; then
  echo "No Frameworks directory in $APP — nothing to fix."
  exit 0
fi

for framework in "$FWDIR"/*.framework; do
  [ -d "$framework" ] || continue
  fw_name=$(basename "$framework" .framework)
  echo "Fixing framework: $fw_name"

  # Ensure Versions/A exists with the binary
  if [ ! -d "$framework/Versions/A" ]; then
    mkdir -p "$framework/Versions/A"
    # Move the top-level binary into Versions/A if it exists
    if [ -f "$framework/$fw_name" ] && [ ! -L "$framework/$fw_name" ]; then
      mv "$framework/$fw_name" "$framework/Versions/A/$fw_name"
    fi
  fi

  # Move Resources into Versions/A if it is a real directory at top level
  if [ -d "$framework/Resources" ] && [ ! -L "$framework/Resources" ]; then
    if [ -d "$framework/Versions/A/Resources" ]; then
      rm -rf "$framework/Versions/A/Resources"
    fi
    mv "$framework/Resources" "$framework/Versions/A/Resources"
  fi

  # Move _CodeSignature into Versions/A if present at top level
  if [ -d "$framework/_CodeSignature" ] && [ ! -L "$framework/_CodeSignature" ]; then
    rm -rf "$framework/Versions/A/_CodeSignature"
    mv "$framework/_CodeSignature" "$framework/Versions/A/_CodeSignature"
  fi

  # Replace Versions/Current real directory with symlink -> A
  if [ -d "$framework/Versions/Current" ] && [ ! -L "$framework/Versions/Current" ]; then
    rm -rf "$framework/Versions/Current"
  fi
  if [ ! -L "$framework/Versions/Current" ]; then
    ln -s A "$framework/Versions/Current"
  fi

  # Replace top-level binary with symlink -> Versions/Current/<name>
  if [ -f "$framework/$fw_name" ] && [ ! -L "$framework/$fw_name" ]; then
    rm "$framework/$fw_name"
  fi
  if [ ! -L "$framework/$fw_name" ]; then
    ln -s "Versions/Current/$fw_name" "$framework/$fw_name"
  fi

  # Replace top-level Resources with symlink -> Versions/Current/Resources
  if [ ! -L "$framework/Resources" ]; then
    ln -s "Versions/Current/Resources" "$framework/Resources"
  fi

  echo "  OK: $(ls -la "$framework/$fw_name" | tail -1)"
done

echo "=== Framework structure verification ==="
for framework in "$FWDIR"/*.framework; do
  fw_name=$(basename "$framework" .framework)
  echo "$fw_name:"
  echo "  Current: $(readlink "$framework/Versions/Current" || echo 'NOT A SYMLINK')"
  echo "  Binary:  $(readlink "$framework/$fw_name" || echo 'NOT A SYMLINK')"
  echo "  Res:     $(readlink "$framework/Resources" || echo 'NOT A SYMLINK')"
done
