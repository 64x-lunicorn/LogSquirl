// Tests for src/releases.mjs (#313, #315): reading the release pages and
// choosing which of them the deployed website leaves out. Run: npm test
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';

import { currentReleases, readReleases, unpublishedReleases } from '../src/releases.mjs';

function newsDir(pages) {
  const dir = mkdtempSync(join(tmpdir(), 'news-'));
  for (const [name, release] of Object.entries(pages)) {
    const fields = Object.entries(release).map(([key, value]) => `  ${key}: ${value}`).join('\n');
    writeFileSync(join(dir, name), `---\ntitle: T\ndescription: About ${name}\nrelease:\n${fields}\n---\n\nBody\n`);
  }
  return dir;
}

const pages = {
  'release-22-06.md': { version: '22.06', date: '2022-06', channel: 'legacy' },
  'release-26-07.md': { version: '26.07.0', date: '2026-07-13', channel: 'stable' },
  'release-26-10.md': { version: '26.10.0-beta1', date: '2026-09-17', channel: 'beta' },
  'release-26-11.md': { version: '26.11.0-beta1', date: '2026-10-20', channel: 'beta' },
};

test('releases are read newest first, with their label and slug', () => {
  const releases = readReleases(newsDir(pages));
  assert.deepEqual(releases.map((r) => r.version), ['26.11.0-beta1', '26.10.0-beta1', '26.07.0', '22.06']);
  assert.equal(releases[0].label, 'v26.11.0-beta1');
  assert.equal(releases[0].slug, 'news/release-26-11');
});

test('the current releases are the latest stable and a newer beta', () => {
  const { stable, beta } = currentReleases(readReleases(newsDir(pages)));
  assert.equal(stable.version, '26.07.0');
  assert.equal(beta.version, '26.11.0-beta1');
});

test('a beta older than the latest stable is not current', () => {
  const later = { ...pages, 'release-26-12.md': { version: '26.12.0', date: '2026-12-01', channel: 'stable' } };
  assert.equal(currentReleases(readReleases(newsDir(later))).beta, null);
});

test('a page whose release has no published GitHub release is left out', () => {
  const releases = readReleases(newsDir(pages));
  const left = unpublishedReleases(releases, ['v26.07.0', 'v26.10.0-beta1']);
  assert.deepEqual(left.map((r) => r.file), ['release-26-11.md']);
});

test('legacy releases are never left out', () => {
  const releases = readReleases(newsDir(pages));
  assert.deepEqual(unpublishedReleases(releases, ['v26.07.0', 'v26.10.0-beta1', 'v26.11.0-beta1']), []);
});

test('an empty list of published releases is an error, not an empty website', () => {
  assert.throws(() => unpublishedReleases(readReleases(newsDir(pages)), []), /no published releases/);
});

test('a malformed release frontmatter names the page', () => {
  const broken = { 'release-26-07.md': { version: 'latest', date: '2026-07-13', channel: 'stable' } };
  assert.throws(() => readReleases(newsDir(broken)), /news\/release-26-07\.md: release\.version "latest"/);
});
