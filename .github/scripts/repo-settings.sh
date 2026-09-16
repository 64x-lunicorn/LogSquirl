#!/usr/bin/env bash
# The repository settings the workflows rely on but cannot declare themselves
# (#201): read-only GITHUB_TOKEN by default, no pull requests created or
# approved by workflows, an explicit allowlist of third-party actions, SHA
# pinning required (#202), and protected release tags.
#
# Usage:
#   .github/scripts/repo-settings.sh check    # report drift, exit 1 on any
#   .github/scripts/repo-settings.sh apply    # make the settings match
#
# Needs `gh` authenticated as a repository admin. Adding a third-party action
# to a workflow means adding it to ALLOWED_ACTIONS here and re-running apply;
# otherwise the job using it fails with "action is not allowed".
set -euo pipefail

REPO=${REPO:-64x-lunicorn/LogSquirl}
mode=${1:-check}

# GitHub-owned actions (actions/*, github/*) are allowed separately.
ALLOWED_ACTIONS=(
  anchore/sbom-action@*
  apple-actions/import-codesign-certs@*
  aquasecurity/trivy-action@*
  dawidd6/action-download-artifact@*
  docker/build-push-action@*
  docker/login-action@*
  docker/setup-buildx-action@*
  ilammy/msvc-dev-cmd@*
  jurplel/install-qt-action@*
  mozilla-actions/sccache-action@*
  ossf/scorecard-action@*
  SamKirkland/FTP-Deploy-Action@*
  sigstore/cosign-installer@*
  softprops/action-gh-release@*
  zizmorcore/zizmor-action@*
)

TAG_RULESET_NAME="Protect release tags"

drift=0
report() { # name, expected, actual
  if [ "$2" = "$3" ]; then
    echo "ok     $1"
  else
    echo "DRIFT  $1: expected $2, got $3"
    drift=1
  fi
}

allowed_json() {
  printf '%s\n' "${ALLOWED_ACTIONS[@]}" | LC_ALL=C sort | jq -R . | jq -cs .
}

check() {
  local perms workflow selected ruleset_id
  perms=$(gh api "repos/$REPO/actions/permissions")
  report "actions allowed_actions" selected "$(jq -r .allowed_actions <<<"$perms")"
  report "actions sha_pinning_required" true "$(jq -r .sha_pinning_required <<<"$perms")"

  workflow=$(gh api "repos/$REPO/actions/permissions/workflow")
  report "default_workflow_permissions" read "$(jq -r .default_workflow_permissions <<<"$workflow")"
  report "can_approve_pull_request_reviews" false "$(jq -r .can_approve_pull_request_reviews <<<"$workflow")"

  if [ "$(jq -r .allowed_actions <<<"$perms")" = selected ]; then
    selected=$(gh api "repos/$REPO/actions/permissions/selected-actions")
    report "github_owned_allowed" true "$(jq -r .github_owned_allowed <<<"$selected")"
    report "verified_allowed" false "$(jq -r .verified_allowed <<<"$selected")"
    report "patterns_allowed" "$(allowed_json)" "$(jq -c '.patterns_allowed | sort' <<<"$selected")"
  fi

  ruleset_id=$(gh api "repos/$REPO/rulesets" --jq ".[] | select(.name == \"$TAG_RULESET_NAME\") | .id")
  if [ -z "$ruleset_id" ]; then
    report "tag ruleset" present missing
  else
    local ruleset
    ruleset=$(gh api "repos/$REPO/rulesets/$ruleset_id")
    report "tag ruleset enforcement" active "$(jq -r .enforcement <<<"$ruleset")"
    report "tag ruleset target" '["refs/tags/v*"]' "$(jq -c .conditions.ref_name.include <<<"$ruleset")"
    report "tag ruleset rules" '["creation","deletion","update"]' "$(jq -c '[.rules[].type] | sort' <<<"$ruleset")"
  fi
}

apply() {
  # Order matters: the allowlist exists before "selected" takes effect.
  gh api -X PUT "repos/$REPO/actions/permissions" \
    -F enabled=true -f allowed_actions=selected -F sha_pinning_required=true > /dev/null
  jq -n --argjson patterns "$(allowed_json)" \
    '{github_owned_allowed: true, verified_allowed: false, patterns_allowed: $patterns}' |
    gh api -X PUT "repos/$REPO/actions/permissions/selected-actions" --input - > /dev/null
  gh api -X PUT "repos/$REPO/actions/permissions/workflow" \
    -f default_workflow_permissions=read -F can_approve_pull_request_reviews=false > /dev/null

  # Only repository admins (role id 5) may create release tags; nobody may move
  # or delete one, so a published release keeps pointing at what was attested.
  local body ruleset_id
  body=$(jq -n --arg name "$TAG_RULESET_NAME" '{
    name: $name,
    target: "tag",
    enforcement: "active",
    bypass_actors: [{actor_id: 5, actor_type: "RepositoryRole", bypass_mode: "always"}],
    conditions: {ref_name: {include: ["refs/tags/v*"], exclude: []}},
    rules: [{type: "creation"}, {type: "update"}, {type: "deletion"}]
  }')
  ruleset_id=$(gh api "repos/$REPO/rulesets" --jq ".[] | select(.name == \"$TAG_RULESET_NAME\") | .id")
  if [ -z "$ruleset_id" ]; then
    gh api -X POST "repos/$REPO/rulesets" --input - <<<"$body" > /dev/null
  else
    gh api -X PUT "repos/$REPO/rulesets/$ruleset_id" --input - <<<"$body" > /dev/null
  fi
}

case "$mode" in
  check) check ;;
  apply) apply; check ;;
  *) echo "usage: $0 check|apply" >&2; exit 2 ;;
esac
exit $drift
