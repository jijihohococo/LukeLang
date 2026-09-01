import { defineConfig } from "vite";

export default defineConfig({
  base: "/ops/",
  build: {
    outDir: "../../../site/ops",
    emptyOutDir: true,
  },
  server: {
    proxy: {
      "/api": "http://127.0.0.1:8820",
    },
  },
});
