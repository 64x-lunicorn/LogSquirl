# The user documentation is DOCUMENTATION.md, shown on the website one page per section

Until #587 the user documentation existed twice. `DOCUMENTATION.md` at the repository root is what the build turns into `documentation.html` and embeds in the app (Help → Documentation), and what the README linked to. The GitHub wiki held a second copy in 24 pages, written once in April 2026 and not touched since; the website linked to it (the home page's "LogSquirl Wiki" card, Getting Involved, including the Plugin System and Plugin Development pages). The two had drifted: by September `DOCUMENTATION.md` described the Dashboard, time search, the Table View's elapsed-time column, standard input, the Command Palette, themes and `logsquirl_grep`, the wiki none of them, and the wiki described things the app does not have (Chocolatey and Scoop packages, a `.pkg` installer, `Ctrl+L` for follow). The website itself had no documentation, so its search found none.

## Decision

1. **`DOCUMENTATION.md` is the only source of the user documentation.** It stays a single file: the app embeds it whole, and GitHub shows it with working anchors. A change to the documentation is a change to this file, reviewed in a pull request like the code it describes.
2. **The website generates its Documentation section from it, one page per `##` section**, under `/docs/<section>/` with an overview at `/docs/`. `website/src/documentation.mjs` splits the file when the site is built or served (called from `astro.config.mjs`), writes the pages into `website/src/content/docs/docs/` and returns them for the sidebar. The pages are not checked in (`website/.gitignore`), so nothing can drift from the source. On the way a page's headings move up one level, and links are rewritten: an anchor goes to the page that has the heading (GitHub counts repeated headings over the whole file, the website per page, and the split translates between the two), a relative image to the image in the repository, any other relative link to the file on GitHub. An anchor no heading has fails the build, as does any broken internal link (`starlight-links-validator`). Starlight's Pagefind search indexes the generated pages like any other.
3. **The wiki holds no documentation.** Its Home page points to the website's Documentation; the other pages are reduced to one line linking the matching section, so old links still lead somewhere.
4. **Links to the documentation point to the website**: the README, the release notes and the website's own pages. The plugin developer guide stays `docs/plugin-sdk.md`, linked on GitHub; it is documentation for developers, not for users.

## Considered Options

- **The wiki as the source**, and the repository points to it. Rejected: the wiki is not reviewed, not versioned with the code, cannot be embedded in the app at build time, and is the copy that had fallen behind.
- **`DOCUMENTATION.md` split into one file per topic** in a `docs/` folder. Rejected for now: the app would have to concatenate them for Help → Documentation, and every anchor in the README and in the file itself would change. Splitting at build time gives the website its pages without touching the source.
- **The website shows the whole file as one page.** Rejected: a thousand lines on one page, and a search hit would only ever lead to its top.
- **The generated pages are checked in** and refreshed by a script. Rejected: a checked-in copy is exactly the second source this decision removes; nothing would stop an edit to the copy.

## Consequences

- A page's URL is its `##` heading. Renaming a section moves its page; the link check catches the website's own links to it, not the README's or external ones, so a section is renamed only with a look at who links to it.
- The website is deployed with each release (#315, #350), so the Documentation section shows the guide as of the latest release, which is the version users run. A change to `DOCUMENTATION.md` on master shows up on the website with the next release.
- A pull request that changes `DOCUMENTATION.md` runs CI Build's Website job as well as the app build (`.github/workflows/ci-build.yml`), so a broken anchor is found before the merge.
- `website/scripts/documentation.test.mjs` pins the split and the link rewriting.
