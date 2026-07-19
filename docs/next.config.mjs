import nextra from 'nextra'

// GitHub Pages serves this project site at <owner>.github.io/cxxprobe/, so
// every asset and internal link needs the /cxxprobe prefix in CI. Locally
// (plain `next dev` / `next build`) there's no prefix, so paths stay at `/`.
const basePath = process.env.GITHUB_ACTIONS ? '/cxxprobe' : ''

const withNextra = nextra({
  search: {
    codeblocks: false
  }
})

/** @type {import('next').NextConfig} */
export default withNextra({
  output: 'export',
  images: {
    unoptimized: true
  },
  basePath,
  assetPrefix: basePath ? `${basePath}/` : undefined,
  trailingSlash: true
})
