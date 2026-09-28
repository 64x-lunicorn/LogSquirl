// The website's Documentation section, generated from DOCUMENTATION.md (#587).
//
// DOCUMENTATION.md at the repository root is the only source of the user
// documentation (docs/adr/0012). The app embeds it whole (Help ->
// Documentation); the website shows it as one page per `##` section, under
// /docs/. The pages are written into src/content/docs/docs/ when the site is
// built or served (astro.config.mjs), and are not checked in, so a change to
// DOCUMENTATION.md shows up on the next build and cannot drift.
//
// On the way, a page's headings move up one level (its `##` is the page
// title), and the links are rewritten for the website:
//
//   (#anchor)          -> /docs/<page>/#anchor, the page that has the heading;
//                         an anchor no heading has fails the build
//   (images/x.png)     -> the image in the repository, for Astro to process
//   (CONTRIBUTING.md)  -> the file on GitHub
//
// Anchors are GitHub's, which counts a repeated heading across the whole
// file (`archives`, `archives-1`); the website counts per page, so a link is
// translated from the one to the other.

import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import GithubSlugger from 'github-slugger';

const HERE = dirname(fileURLToPath(import.meta.url));
export const REPO_ROOT = join(HERE, '..', '..');
export const DOCUMENTATION_FILE = join(REPO_ROOT, 'DOCUMENTATION.md');
export const DOCS_DIR = join(HERE, 'content', 'docs', 'docs');
export const REPO_BLOB_URL = 'https://github.com/64x-lunicorn/LogSquirl/blob/master/';

const HEADING = /^(#{1,6})[ \t]+(.+?)[ \t]*#*[ \t]*$/;
const FENCE = /^[ \t]*(```|~~~)/;
const IMAGE_EXTENSION = /\.(png|jpe?g|gif|svg|webp|avif)$/i;
// A link or an image: `[text](target "title")` or `![alt](target)`.
const LINK = /(!?)\[([^\]]*)\]\(([^)\s]+)((?:\s+"[^"]*")?)\)/g;

/** The lines of `text`, each with whether it is inside a fenced code block. */
function linesOf(text) {
  let fence = null;
  return text.replace(/\r\n/g, '\n').split('\n').map((line) => {
    const opener = FENCE.exec(line);
    if (opener && (fence === null || opener[1] === fence)) {
      fence = fence === null ? opener[1] : null;
      return { line, code: true };
    }
    return { line, code: fence !== null };
  });
}

function plainText(markdown) {
  return markdown
    .replace(/!?\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/[*`]/g, '')
    .replace(/\s+/g, ' ')
    .trim();
}

/** The first sentence of the paragraph a page opens with, if it opens with one. */
function descriptionOf(title, body) {
  const paragraph = [];
  for (const { line, code } of body) {
    if (paragraph.length === 0 && line.trim() === '') continue;
    if (code || line.trim() === '' || /^\s*([#|>]|[-*+] |\d+\. |<!--)/.test(line)) break;
    paragraph.push(line);
  }
  const text = plainText(paragraph.join(' '));
  if (!text) return `The ${title} section of the LogSquirl documentation.`;
  const sentence = /^.+?[.:](?=\s|$)/.exec(text)?.[0] ?? text;
  return sentence.endsWith(':') ? `${sentence.slice(0, -1)}.` : sentence;
}

/**
 * Splits the text of DOCUMENTATION.md into its pages, one per `##` section,
 * leaving out the title and the table of contents.
 *
 * Returns `{ pages: [{ title, slug, description, markdown }] }`, in the
 * order of the file; `markdown` is the page body with its links rewritten.
 */
export function splitDocumentation(text, { base = 'docs', imagePrefix = '', blobUrl = REPO_BLOB_URL } = {}) {
  const documentSlugger = new GithubSlugger();
  const sections = [];
  let current = null;
  for (const entry of linesOf(text)) {
    const heading = entry.code ? null : HEADING.exec(entry.line);
    if (heading) {
      const level = heading[1].length;
      const documentSlug = documentSlugger.slug(plainText(heading[2]));
      if (level === 1) {
        current = null;
        continue;
      }
      if (level === 2) {
        current = { title: heading[2], documentSlug, lines: [], headings: [] };
        sections.push(current);
        continue;
      }
      current?.headings.push({ documentSlug, text: heading[2] });
      current?.lines.push({ ...entry, line: `${'#'.repeat(level - 1)} ${heading[2]}` });
      continue;
    }
    current?.lines.push(entry);
  }

  const pages = sections
    .filter((section) => !/^(table of )?contents$/i.test(section.title))
    .map((section) => ({ ...section, slug: new GithubSlugger().slug(plainText(section.title)) }));
  const seenSlugs = new Set();
  for (const page of pages) {
    if (seenSlugs.has(page.slug)) throw new Error(`DOCUMENTATION.md: two sections are named "${page.title}"`);
    seenSlugs.add(page.slug);
  }

  // Where each of GitHub's anchors lands on the website.
  const anchors = new Map();
  for (const page of pages) {
    anchors.set(page.documentSlug, { page, local: null });
    const pageSlugger = new GithubSlugger();
    for (const heading of page.headings) anchors.set(heading.documentSlug, { page, local: pageSlugger.slug(plainText(heading.text)) });
  }

  const rewrite = (page) => (match, bang, label, target, title) => {
    let url = target;
    if (target.startsWith('#')) {
      const anchor = decodeURIComponent(target.slice(1)).toLowerCase();
      const place = anchors.get(anchor);
      if (!place) throw new Error(`DOCUMENTATION.md, section "${page.title}": no heading has the anchor "${target}"`);
      const pageUrl = `/${base}/${place.page.slug}/`;
      if (place.local === null) url = pageUrl;
      else url = place.page === page ? `#${place.local}` : `${pageUrl}#${place.local}`;
    } else if (!/^([a-z][a-z0-9+.-]*:|\/)/i.test(target)) {
      const path = target.replace(/^\.\//, '');
      url = IMAGE_EXTENSION.test(path.split('#')[0]) ? `${imagePrefix}${path}` : `${blobUrl}${path}`;
    }
    return `${bang}[${label}](${url}${title})`;
  };

  return {
    pages: pages.map((page) => {
      const body = page.lines;
      while (body.length && body[0].line.trim() === '') body.shift();
      while (body.length && body.at(-1).line.trim() === '') body.pop();
      const markdown = body.map(({ line, code }) => (code ? line : line.replace(LINK, rewrite(page)))).join('\n');
      return { title: page.title, slug: page.slug, description: descriptionOf(page.title, body), markdown };
    }),
  };
}

function frontmatter(fields) {
  const lines = Object.entries(fields).map(([key, value]) => `${key}: ${JSON.stringify(value)}`);
  return `---\n${lines.join('\n')}\n---\n`;
}

const GENERATED = '<!-- Generated from DOCUMENTATION.md by website/src/documentation.mjs. Edit DOCUMENTATION.md instead. -->';

/**
 * Writes the Documentation section into `dir` (replacing what is there) and
 * returns its pages, for the sidebar.
 */
export function writeDocumentationPages({ source = DOCUMENTATION_FILE, dir = DOCS_DIR } = {}) {
  const imagePrefix = `${relative(dir, REPO_ROOT).split('\\').join('/')}/`;
  const { pages } = splitDocumentation(readFileSync(source, 'utf8'), { imagePrefix });
  if (pages.length === 0) throw new Error(`${source}: no "##" sections`);

  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const editUrl = `${REPO_BLOB_URL}DOCUMENTATION.md`;
  for (const page of pages) {
    const head = frontmatter({ title: page.title, description: page.description, editUrl });
    writeFileSync(join(dir, `${page.slug}.md`), `${head}\n${GENERATED}\n\n${page.markdown}\n`);
  }
  const list = pages.map((page) => `- [${page.title}](/docs/${page.slug}/): ${page.description}`).join('\n');
  const overview = frontmatter({
    title: 'Documentation',
    description: 'The LogSquirl user guide: opening, searching and charting log files, the settings, the keys and the command line.',
    editUrl,
  });
  writeFileSync(
    join(dir, 'index.md'),
    `${overview}\n${GENERATED}\n\nThe user guide of LogSquirl, one page per topic. The same guide ships with the app, under\n**Help → Documentation**.\n\n${list}\n`,
  );
  return pages;
}
