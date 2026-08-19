# LukeLang VS Code Extension (Official)

Official editor support for `.luke` files:

- syntax highlighting
- snippets
- keyword completions
- LSP (`luke LSP`) — hover, diagnostics, rename, format, semantic tokens
- DAP debug (`luke DAP`) — gdb-backed breakpoints + Reactive scope

## Prerequisites

Build the Luke toolchain in the workspace:

```bash
cd vm && make
```

The extension auto-detects `vm/build/luke`. Override with setting `lukelang.lukePath`.

Debugging requires `gdb` on your PATH.

## Local development

```bash
npm install
npm run check
```

Press `F5` in VS Code to launch an Extension Development Host.

## Package installable `.vsix`

From repository root:

```bash
bash scripts/vscode_extension_package.sh
```

Or from this folder:

```bash
npm run package
```

## Debug a `.luke` file

Use launch type `lukelang`:

```json
{
  "type": "lukelang",
  "request": "launch",
  "name": "Debug Luke",
  "program": "${file}",
  "stopOnEntry": true
}
```

See repo root `.vscode/launch.json` for examples.
