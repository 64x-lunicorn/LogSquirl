# SessionStart hook: warn once per session about missing setup, missing CONTEXT.md,
# and a review that has fallen behind. Silent when the repo is in good shape.
$ErrorActionPreference = 'SilentlyContinue'

$reviewThreshold = 20

$root = git rev-parse --show-toplevel 2>$null
if (-not $root) { $root = (Get-Location).Path }
Set-Location -LiteralPath $root

$notes = [System.Collections.Generic.List[string]]::new()

if (-not (Test-Path -LiteralPath '.github/unisquirl.md')) {
    $notes.Add('This repo has no .github/unisquirl.md, so it was never set up. Before running any skill that reads or writes issues, tell the user to run /setup.')
}

if (-not (Test-Path -LiteralPath 'CONTEXT.md') -and -not (Test-Path -LiteralPath 'CONTEXT-MAP.md')) {
    $notes.Add('This repo has no CONTEXT.md, so there is no agreed vocabulary to write in. Tell the user to run /setup, which creates it.')
}

if (Test-Path -LiteralPath '.github/.unisquirl-review') {
    $last = (Get-Content -LiteralPath '.github/.unisquirl-review' -TotalCount 1).Trim()
    if ($last) {
        git cat-file -e "$last^{commit}" 2>$null
        if ($LASTEXITCODE -eq 0) {
            $behind = [int](git rev-list --count "$last..HEAD" 2>$null)
            if ($behind -ge $reviewThreshold) {
                $notes.Add("$behind commits since the last review. Suggest /code-review (against $last), or /improve-codebase-architecture if the user wants the bigger picture.")
            }
        }
    }
}

if ($notes.Count -eq 0) { exit 0 }

$body = ($notes | ForEach-Object { "- $_" }) -join "`n"
@{ systemMessage = "Unisquirl checks:`n$body" } | ConvertTo-Json -Compress
exit 0
