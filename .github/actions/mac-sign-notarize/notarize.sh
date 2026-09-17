#!/usr/bin/env bash
# Submits a file to Apple's notary service, waits for the verdict, prints the
# notarization log and fails unless the submission was accepted.
#
# Usage: notarize.sh <file> <label>
# Credentials come from the environment only, never from arguments (#204):
#   NOTARY_APPLE_ID, NOTARY_TEAM_ID, NOTARY_PASSWORD
set -euo pipefail

file=$1
label=$2
log="$RUNNER_TEMP/${label}_notary.log"

xcrun notarytool submit --wait \
  --apple-id "$NOTARY_APPLE_ID" \
  --team-id "$NOTARY_TEAM_ID" \
  --password "$NOTARY_PASSWORD" \
  "$file" \
  | tee "$log"

status=$(grep -i "status:" "$log" | tail -1 | awk '{print $NF}')
notary_id=$(awk '$1=="id:"{id=$2} END{print id}' "$log")

# Always fetch the notarization log for diagnostics
xcrun notarytool log \
  --apple-id "$NOTARY_APPLE_ID" \
  --team-id "$NOTARY_TEAM_ID" \
  --password "$NOTARY_PASSWORD" \
  "$notary_id" "$log.json" || true
echo "=== $label notarization log ==="
cat "$log.json" || true

if [ "$status" != "Accepted" ]; then
  echo "::error::$label notarization failed with status: $status"
  exit 1
fi
