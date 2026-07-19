import { Footer, Layout, Navbar } from 'nextra-theme-docs'
import { Head } from 'nextra/components'
import { getPageMap } from 'nextra/page-map'
import 'nextra-theme-docs/style.css'
import './globals.css'

export const metadata = {
  metadataBase: new URL('https://algorithms-mathematics-society.github.io/cxxprobe/'),
  title: {
    default: 'cxxprobe',
    template: '%s — cxxprobe'
  },
  description:
    'A professional C++ evaluation framework for coding contests and interviews — sandboxed execution, judging, and batch evaluation.',
  applicationName: 'cxxprobe docs',
  appleWebApp: {
    title: 'cxxprobe docs'
  }
}

const logo = (
  <span
    style={{
      display: 'flex',
      alignItems: 'center',
      gap: '0.5rem',
      fontWeight: 700,
      fontSize: '1.05rem',
      letterSpacing: '-0.02em'
    }}
  >
    <svg
      width="22"
      height="22"
      viewBox="0 0 24 24"
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      aria-hidden="true"
    >
      <rect width="24" height="24" rx="6" fill="currentColor" opacity="0.12" />
      <path
        d="M7 8.5L4.5 12L7 15.5"
        stroke="currentColor"
        strokeWidth="1.6"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <path
        d="M17 8.5L19.5 12L17 15.5"
        stroke="currentColor"
        strokeWidth="1.6"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <path
        d="M13.5 6.5L10.5 17.5"
        stroke="currentColor"
        strokeWidth="1.6"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
    cxxprobe
  </span>
)

const navbar = (
  <Navbar
    logo={logo}
    projectLink="https://github.com/Algorithms-Mathematics-Society/cxxprobe"
  />
)

const footer = (
  <Footer>
    <span>
      MIT {new Date().getFullYear()} ©{' '}
      <a href="https://github.com/Algorithms-Mathematics-Society" target="_blank" rel="noreferrer">
        Algorithms &amp; Mathematics Society
      </a>
      . Built with{' '}
      <a href="https://nextra.site" target="_blank" rel="noreferrer">
        Nextra
      </a>
      .
    </span>
  </Footer>
)

export default async function RootLayout({ children }) {
  return (
    <html lang="en" dir="ltr" suppressHydrationWarning>
      <Head
        faviconGlyph="🧪"
        color={{
          hue: { light: 189, dark: 189 },
          saturation: 85,
          lightness: { light: 34, dark: 62 }
        }}
      />
      <body>
        <Layout
          navbar={navbar}
          pageMap={await getPageMap()}
          docsRepositoryBase="https://github.com/Algorithms-Mathematics-Society/cxxprobe/tree/main/docs"
          editLink="Edit this page on GitHub"
          sidebar={{ defaultMenuCollapseLevel: 1, autoCollapse: true }}
          footer={footer}
        >
          {children}
        </Layout>
      </body>
    </html>
  )
}
