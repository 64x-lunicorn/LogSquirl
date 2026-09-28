// Code blocks in the SMYCK colors (src/styles/smyck.css): a dark and a light
// syntax theme, framed in Starlight's own colors.
import { defineEcConfig, ExpressiveCodeTheme } from '@astrojs/starlight/expressive-code';

function smyck(name, type, colors) {
  return new ExpressiveCodeTheme({
    name,
    type,
    colors: { 'editor.background': colors.background, 'editor.foreground': colors.text },
    tokenColors: [
      { scope: ['comment', 'punctuation.definition.comment'], settings: { foreground: colors.comment, fontStyle: 'italic' } },
      { scope: ['string', 'string.quoted', 'string.regexp'], settings: { foreground: colors.string } },
      { scope: ['constant.numeric', 'constant.language', 'constant.character'], settings: { foreground: colors.number } },
      { scope: ['keyword', 'storage', 'keyword.operator.logical'], settings: { foreground: colors.keyword } },
      { scope: ['entity.name.function', 'support.function', 'entity.name.command'], settings: { foreground: colors.function } },
      { scope: ['variable.parameter', 'variable.other', 'support.variable'], settings: { foreground: colors.variable } },
      { scope: ['entity.name.type', 'entity.name.class', 'support.type', 'entity.name.tag'], settings: { foreground: colors.type } },
      { scope: ['invalid'], settings: { foreground: colors.invalid } },
    ],
  });
}

export default defineEcConfig({
  themes: [
    smyck('smyck', 'dark', {
      background: '#1b1b1b',
      text: '#f7f7f7',
      comment: '#989898',
      string: '#cdee69',
      number: '#ffe377',
      keyword: '#9cd9f0',
      function: '#77dfd8',
      variable: '#fbb1f9',
      type: '#c8a0d1',
      invalid: '#e09690',
    }),
    smyck('smyck-light', 'light', {
      background: '#f7f7f7',
      text: '#1b1b1b',
      comment: '#5d5d5d',
      string: '#46601a',
      number: '#6b5a14',
      keyword: '#207483',
      function: '#1a5e6b',
      variable: '#8a3f87',
      type: '#6b4a73',
      invalid: '#8e3a2e',
    }),
  ],
  useStarlightUiThemeColors: true,
});
