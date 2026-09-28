// Tests for src/documentation.mjs (#587): DOCUMENTATION.md split into the
// website's Documentation pages, and its links rewritten. Run: npm test
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';

import { DOCUMENTATION_FILE, REPO_BLOB_URL, splitDocumentation, writeDocumentationPages } from '../src/documentation.mjs';

const doc = `# LogSquirl documentation

## Table of Contents

1. [Getting started](#Getting-started)

## Getting started

LogSquirl opens log files. It is fast.

### Installing

See [the dashboard](#the-dashboard) and [archives](#archives).

#### Archives

Opened archives.

### The Dashboard

The first tab.

## Settings

### File

#### Archives

Settings for archives, not the ones in [Getting started](#archives).
The second one is [this one](#archives-1), the [dashboard](#The-Dashboard) is elsewhere.

\`\`\`sh
## not a section
[not a link](#nowhere)
\`\`\`

## Links

![A chart](images/chart.png) and [the build guide](BUILD.md#linux), [the web](https://example.org/#x).
`;

const pages = () => splitDocumentation(doc, { imagePrefix: '../../../../../' }).pages;

test('one page per ## section, without the title and the table of contents', () => {
  assert.deepEqual(
    pages().map((page) => [page.title, page.slug]),
    [
      ['Getting started', 'getting-started'],
      ['Settings', 'settings'],
      ['Links', 'links'],
    ],
  );
});

test('a page keeps its content, with its headings one level up', () => {
  const [start] = pages();
  assert.match(start.markdown, /^LogSquirl opens log files\./);
  assert.match(start.markdown, /^## Installing$/m);
  assert.match(start.markdown, /^### Archives$/m);
  assert.doesNotMatch(start.markdown, /^#### /m);
});

test('a ## line in a code block is not a section, and its links stay as written', () => {
  const settings = pages()[1];
  assert.match(settings.markdown, /^## not a section$/m);
  assert.match(settings.markdown, /\[not a link\]\(#nowhere\)/);
});

test('an anchor leads to the page that has the heading', () => {
  const [start, settings] = pages();
  assert.match(start.markdown, /\[the dashboard\]\(#the-dashboard\)/);
  assert.match(start.markdown, /\[archives\]\(#archives\)/);
  // GitHub counts a repeated heading over the whole file; the website per page.
  assert.match(settings.markdown, /\[Getting started\]\(\/docs\/getting-started\/#archives\)/);
  assert.match(settings.markdown, /\[this one\]\(#archives\)/);
  assert.match(settings.markdown, /\[dashboard\]\(\/docs\/getting-started\/#the-dashboard\)/);
});

test('an anchor of a ## section leads to its page', () => {
  const text = `# T\n\n## First part\n\nText.\n\n## Second part\n\nBack to [the first](#First-part).\n`;
  const [, second] = splitDocumentation(text).pages;
  assert.match(second.markdown, /\[the first\]\(\/docs\/first-part\/\)/);
});

test('an anchor that no heading has fails', () => {
  assert.throws(() => splitDocumentation('# T\n\n## A\n\nSee [b](#b).\n'), /section "A": no heading has the anchor "#b"/);
});

test('images come from the repository, other files from GitHub, web links stay', () => {
  const links = pages()[2];
  assert.match(links.markdown, /!\[A chart\]\(\.\.\/\.\.\/\.\.\/\.\.\/\.\.\/images\/chart\.png\)/);
  assert.ok(links.markdown.includes(`[the build guide](${REPO_BLOB_URL}BUILD.md#linux)`));
  assert.match(links.markdown, /\[the web\]\(https:\/\/example\.org\/#x\)/);
});

test('a page is described by its first sentence, or by its title', () => {
  const [start, settings] = pages();
  assert.equal(start.description, 'LogSquirl opens log files.');
  assert.equal(settings.description, 'The Settings section of the LogSquirl documentation.');
});

test('two sections of the same name fail', () => {
  assert.throws(() => splitDocumentation('## A\n\nx\n\n## A\n\ny\n'), /two sections are named "A"/);
});

test('the pages are written with their title and replace what was there', () => {
  const dir = mkdtempSync(join(tmpdir(), 'docs-'));
  const source = join(dir, 'DOCUMENTATION.md');
  writeFileSync(source, doc);
  const out = join(dir, 'out');
  writeDocumentationPages({ source, dir: out });
  writeFileSync(join(out, 'stale.md'), 'left from an earlier build');
  writeDocumentationPages({ source, dir: out });
  assert.deepEqual(readdirSync(out).sort(), ['getting-started.md', 'index.md', 'links.md', 'settings.md']);
  const page = readFileSync(join(out, 'getting-started.md'), 'utf8');
  assert.match(page, /^---\ntitle: "Getting started"\ndescription: "LogSquirl opens log files\."\n/);
  assert.match(page, /Edit DOCUMENTATION\.md instead/);
  assert.match(readFileSync(join(out, 'index.md'), 'utf8'), /- \[Settings\]\(\/docs\/settings\/\)/);
  assert.ok(!existsSync(join(out, 'stale.md')));
});

test('DOCUMENTATION.md itself splits, and every page has content', () => {
  const { pages: real } = splitDocumentation(readFileSync(DOCUMENTATION_FILE, 'utf8'));
  assert.ok(real.length >= 5);
  assert.ok(real.some((page) => page.slug === 'getting-started'));
  for (const page of real) assert.ok(page.markdown.trim().length > 0, page.title);
});
