// Removes the release pages whose release is not published on GitHub yet from
// the checkout the deploy builds (#315), so the page, its overview and home
// page cards and its sidebar entry go live together once CI Release has
// published the release and deployed the website again. A local preview and
// the pull request build keep every page.
//
//   node scripts/leave-out-unpublished.mjs <file with one published tag per line>
import { readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';

import { NEWS_DIR, readReleases, unpublishedReleases } from '../src/releases.mjs';

const tags = readFileSync(process.argv[2], 'utf8').split('\n').map((line) => line.trim()).filter(Boolean);
for (const release of unpublishedReleases(readReleases(), tags)) {
  rmSync(join(NEWS_DIR, release.file));
  console.log(`Left out ${release.file}: v${release.version} is not published yet.`);
}
