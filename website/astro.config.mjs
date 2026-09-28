import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import starlightLinksValidator from 'starlight-links-validator';
import { readReleases } from './src/releases.mjs';

export default defineConfig({
  site: 'https://logsquirl.lunicorn-lab.de',
  vite: {
    resolve: {
      alias: {
        '@assets': '/src/assets',
      },
    },
  },
  integrations: [
    starlight({
      title: 'LogSquirl',
      logo: {
        src: './src/assets/logsquirl.png',
        alt: 'LogSquirl Logo',
      },
      favicon: '/favicon.png',
      // The card a shared link shows (#582); scripts/og-image.py draws it.
      head: [
        { tag: 'meta', attrs: { property: 'og:image', content: 'https://logsquirl.lunicorn-lab.de/og.png' } },
        { tag: 'meta', attrs: { property: 'og:image:width', content: '1200' } },
        { tag: 'meta', attrs: { property: 'og:image:height', content: '630' } },
        { tag: 'meta', attrs: { property: 'og:image:alt', content: 'LogSquirl, a fast, smart log file explorer' } },
        { tag: 'meta', attrs: { name: 'twitter:image', content: 'https://logsquirl.lunicorn-lab.de/og.png' } },
        { tag: 'link', attrs: { rel: 'apple-touch-icon', href: '/apple-touch-icon.png' } },
      ],
      social: [
        { icon: 'github', label: 'GitHub', href: 'https://github.com/64x-lunicorn/LogSquirl' },
      ],
      customCss: ['./src/styles/smyck.css', './src/styles/custom.css', './src/styles/releases.css'],
      // A link to a page or heading that does not exist fails the build, on
      // pull requests as well as in the deploy (#311).
      plugins: [starlightLinksValidator()],
      sidebar: [
        {
          label: 'About',
          items: [
            { label: 'Getting Involved', slug: 'getting-involved' },
            { label: 'Legal Notice', slug: 'legal-notice' },
            { label: 'Privacy Policy', slug: 'privacy-policy' },
            { label: 'Code Signing Policy', slug: 'code-signing-policy' },
          ],
        },
        {
          label: 'Releases',
          items: [
            { label: 'Overview', slug: 'news' },
            // One entry per release page, newest first (#313).
            ...readReleases().map((release) => ({ label: release.label, slug: release.slug })),
          ],
        },
        {
          label: 'Articles',
          items: [
            { label: 'Automatic Crash Reporting', slug: 'articles/crash-reporting' },
            { label: 'Switching to Hyperscan', slug: 'articles/hyperscan' },
            { label: 'Allocation Matters', slug: 'articles/allocation' },
            { label: 'Combining Search Expressions', slug: 'articles/boolean-combination' },
          ],
        },
      ],
      components: {
        Footer: './src/components/Footer.astro',
        // The release pages open with a Smyck terminal window and style their notes.
        PageTitle: './src/components/PageTitle.astro',
        MarkdownContent: './src/components/MarkdownContent.astro',
      },
    }),
  ],
});
