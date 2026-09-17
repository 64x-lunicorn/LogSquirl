import { defineCollection } from 'astro:content';
import { z } from 'astro/zod';
import { docsLoader } from '@astrojs/starlight/loaders';
import { docsSchema } from '@astrojs/starlight/schema';

// A release page's `release` frontmatter (#313); src/releases.mjs reads and
// checks it for the sidebar, the release overview and the home page.
const release = z
  .object({
    // YAML reads 22.06 as a number; releases.mjs checks the text as written.
    version: z.union([z.string(), z.number()]),
    date: z.union([z.date(), z.string()]),
    channel: z.enum(['stable', 'beta', 'legacy']),
    label: z.string().optional(),
  })
  .optional();

export const collections = {
  docs: defineCollection({
    loader: docsLoader(),
    schema: docsSchema({ extend: z.object({ release }) }),
  }),
};
