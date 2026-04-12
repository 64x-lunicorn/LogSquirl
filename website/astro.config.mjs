import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

export default defineConfig({
  site: 'https://logsquirl.lunicorn-lab.de',
  integrations: [
    starlight({
      title: 'LogSquirl',
      logo: {
        src: './src/assets/logsquirl.png',
        alt: 'LogSquirl Logo',
      },
      favicon: '/favicon.png',
      social: [
        { icon: 'github', label: 'GitHub', href: 'https://github.com/64x-lunicorn/LogSquirl' },
        { icon: 'discord', label: 'Discord', href: 'https://discord.gg/DruNyQftzB' },
      ],
      customCss: ['./src/styles/custom.css'],
      sidebar: [
        {
          label: 'About',
          items: [
            { label: 'Getting Involved', slug: 'getting-involved' },
            { label: 'Legal Notice', slug: 'legal-notice' },
            { label: 'Privacy Policy', slug: 'privacy-policy' },
          ],
        },
        {
          label: 'Releases',
          items: [
            { label: 'Overview', slug: 'news' },
            { label: 'v26.04 (Beta 2)', slug: 'news/release-26-04' },
            { label: 'v26.03 (Beta)', slug: 'news/release-26-03' },
            { label: 'v22.06', slug: 'news/release-22-06' },
            { label: 'v20.12', slug: 'news/release-20-12' },
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
      },
    }),
  ],
});
