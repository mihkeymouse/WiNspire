const config = {
  title: 'WiNspire',
  tagline: 'Windows on the TI-Nspire CX II',
  url: process.env.WINSPIRE_SITE_URL || 'https://winspire.github.io',
  baseUrl: process.env.WINSPIRE_BASE_URL || '/',
  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'throw',
  organizationName: 'MalikIdreesHasanKhan',
  projectName: 'WiNspire',
  presets: [
    [
      'classic',
      {
        docs: {
          routeBasePath: '/',
          sidebarPath: require.resolve('./sidebars.js')
        },
        blog: false,
        theme: {
          customCss: [
            require.resolve('./src/css/custom.css'),
            require.resolve('./src/css/generated-theme.css')
          ]
        }
      }
    ]
  ],
  themeConfig: {
    colorMode: {defaultMode: 'light', respectPrefersColorScheme: false},
    prism: {
      theme: require('prism-react-renderer').themes.vsLight,
      darkTheme: require('prism-react-renderer').themes.vsDark
    },
    navbar: {
      logo: {
        alt: 'WiNspire',
        src: 'img/winspire-wordmark.svg',
        srcDark: 'img/winspire-wordmark-dark.svg'
      },
      items: [
        {href: 'https://github.com/mihkeymouse/WiNspire', label: 'GitHub', position: 'left'}
      ]
    }
  }
};

module.exports = config;
