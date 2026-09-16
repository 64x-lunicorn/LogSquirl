#!/usr/bin/env python3
"""Summarize a ctest JUnit report for the GitHub job summary (#217).

Usage: junit_summary.py <junit.xml> <title>

Writes a Markdown summary listing the failed test cases by name to
$GITHUB_STEP_SUMMARY (stdout when unset) and one error annotation per failed
case. Standard library only, so it runs on every runner without a setup step.
It never fails the job: the ctest step does that.
"""

import os
import sys
import xml.etree.ElementTree as ET

# GitHub shows only the first annotations of a step; the summary lists all.
MAX_ANNOTATIONS = 10


def escape_markdown(text):
    return text.replace("`", "'").replace("|", "\\|").replace("\n", " ")


def escape_annotation(text):
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def main():
    junit_file, title = sys.argv[1], sys.argv[2]
    lines = []

    root = None
    if os.path.isfile(junit_file):
        try:
            root = ET.parse(junit_file).getroot()
        except ET.ParseError as error:
            print(f"::warning::Unreadable JUnit report {junit_file}: {error}")

    if root is None:
        lines.append(f"### Tests: {title}")
        lines.append("")
        lines.append(f"No readable JUnit report at `{junit_file}`: the tests did not run to the end.")
    else:
        cases = root.iter("testcase")
        passed, failed, disabled = 0, [], 0
        for case in cases:
            name = case.get("name", "?")
            status = case.get("status", "")
            if status == "disabled":
                disabled += 1
            elif status == "notrun":
                # A test ctest could not start fails the ctest run as well.
                skipped = case.find("skipped")
                message = skipped.get("message", "") if skipped is not None else ""
                failed.append((name, f"Not run: {message}" if message else "Not run"))
            elif case.find("failure") is not None:
                failed.append((name, case.find("failure").get("message", "Failed")))
            else:
                passed += 1

        verdict = "failed" if failed else "passed"
        lines.append(f"### Tests: {title} {verdict}")
        lines.append("")
        lines.append(f"{passed} passed, {len(failed)} failed, {disabled} disabled.")
        if failed:
            lines.append("")
            lines.append("| Failed test | Result |")
            lines.append("| --- | --- |")
            for name, message in failed:
                lines.append(f"| `{escape_markdown(name)}` | {escape_markdown(message)} |")
            for name, message in failed[:MAX_ANNOTATIONS]:
                print(f"::error title=Test failed ({escape_annotation(title)})::"
                      f"{escape_annotation(name)}: {escape_annotation(message)}")

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    text = "\n".join(lines) + "\n"
    if summary:
        with open(summary, "a", encoding="utf-8") as out:
            out.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
