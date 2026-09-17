// The releases the website shows, read from the release pages (#313).
//
// A release page is src/content/docs/news/release-*.md, and its frontmatter
// is the only place a release's version is written:
//
//   release:
//     version: 26.10.0-beta1   # the release name, the tag without its "v"
//     date: 2026-09-17         # YYYY-MM-DD, or YYYY-MM for a legacy release
//     channel: beta            # stable, beta, or legacy (klogg era)
//     label: v26.03 (Beta)     # optional; defaults to "v" + version
//
// The sidebar (astro.config.mjs), the release overview and the home page
// (ReleaseCards.astro) all take the releases from here, so they cannot
// disagree. A page with missing or malformed release frontmatter fails the
// build, naming the file.

import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

export const NEWS_DIR = join(process.cwd(), 'src', 'content', 'docs', 'news');

const CHANNELS = ['stable', 'beta', 'legacy'];
const VERSION = /^[0-9]+\.[0-9]+(\.[0-9]+(-(alpha|beta|rc)\.?[0-9]+)?)?$/;
const DATE = /^[0-9]{4}-[0-9]{2}(-[0-9]{2})?$/;

function frontmatterOf(file, text) {
  const match = /^---\n([\s\S]*?)\n---\n/.exec(text);
  if (!match) throw new Error(`${file}: no frontmatter`);
  const lines = match[1].split('\n');
  const top = (key) => lines.find((line) => line.startsWith(`${key}: `))?.slice(key.length + 2).trim();
  const start = lines.indexOf('release:');
  if (start < 0) throw new Error(`${file}: no "release:" frontmatter (version, date, channel)`);
  const release = {};
  for (const line of lines.slice(start + 1)) {
    const field = /^ {2}([a-z]+): (.+)$/.exec(line);
    if (!field) break;
    release[field[1]] = field[2].trim();
  }
  return { title: top('title'), description: top('description'), release };
}

export function readReleases(dir = NEWS_DIR) {
  const releases = readdirSync(dir)
    .filter((name) => /^release-.+\.md$/.test(name))
    .map((name) => {
      const file = join('news', name);
      const { description, release } = frontmatterOf(file, readFileSync(join(dir, name), 'utf8'));
      if (!VERSION.test(release.version ?? '')) {
        throw new Error(`${file}: release.version "${release.version}" is not a release name such as 26.10.0-beta1`);
      }
      if (!DATE.test(release.date ?? '')) {
        throw new Error(`${file}: release.date "${release.date}" is not YYYY-MM-DD or YYYY-MM`);
      }
      if (!CHANNELS.includes(release.channel)) {
        throw new Error(`${file}: release.channel "${release.channel}" is not one of ${CHANNELS.join(', ')}`);
      }
      if (!description) throw new Error(`${file}: no description`);
      return {
        file: name,
        slug: `news/${name.replace(/\.md$/, '')}`,
        version: release.version,
        date: release.date,
        channel: release.channel,
        label: release.label ?? `v${release.version}`,
        description,
      };
    });
  const versions = new Set();
  for (const release of releases) {
    if (versions.has(release.version)) throw new Error(`news/${release.file}: version ${release.version} has two pages`);
    versions.add(release.version);
  }
  // Newest first; a legacy date without a day sorts before its month's releases.
  return releases.sort((a, b) => b.date.localeCompare(a.date));
}

// The latest stable release, and the newest beta when it is newer than that.
export function currentReleases(releases) {
  const stable = releases.find((release) => release.channel === 'stable') ?? null;
  const beta = releases.find((release) => release.channel === 'beta') ?? null;
  return { stable, beta: beta && (!stable || beta.date > stable.date) ? beta : null };
}

// The release pages the deployed website leaves out (#315): a LogSquirl
// release whose tag has no published GitHub release yet. Its preparation is
// merged before CI Build has run and the tag is pushed, and the page would
// otherwise link to a release that does not exist. Legacy (klogg) releases
// were published elsewhere and always stay.
export function unpublishedReleases(releases, publishedTags) {
  if (publishedTags.length === 0) {
    throw new Error('no published releases given: refusing to leave out every release page');
  }
  const published = new Set(publishedTags);
  return releases.filter((release) => release.channel !== 'legacy' && !published.has(`v${release.version}`));
}
