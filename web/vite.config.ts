import { defineConfig } from 'vite';

// The firmware embeds exactly three files (index.html, app.js, app.css), so the
// build must emit those fixed names in a single bundle.
// `DEVICE=192.168.1.45 npm run dev` proxies /ws to a real board for UI work.
const device = process.env.DEVICE ?? '192.168.1.45';

export default defineConfig({
  base: '/',
  build: {
    target: 'es2020',
    outDir: 'dist',
    cssCodeSplit: false,
    assetsInlineLimit: 1_000_000,
    chunkSizeWarningLimit: 800,
    rollupOptions: {
      output: {
        entryFileNames: 'app.js',
        assetFileNames: (info) => (info.name?.endsWith('.css') ? 'app.css' : 'assets/[name][extname]'),
        inlineDynamicImports: true,
      },
    },
  },
  server: {
    proxy: { '/ws': { target: `ws://${device}`, ws: true } },
  },
});
