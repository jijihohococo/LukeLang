# LukeLang for VS Code

Official editor support for [LukeLang](https://lukelang.org) — Myanmar's first official
programming language: reactive-native, full-stack, compiling to native C and WebAssembly.

Handles both `.lk` and `.luke`, which are syntax v2 by default.

- **Syntax highlighting** — v2 operators and braces, with the conversational v1 phrases still
  recognised for files built with `--syntax=1`
- **Language server** (`luke LSP`) — hover, diagnostics, go to definition, document symbols,
  rename, formatting, completions
- **Debugging** (`luke DAP`) — gdb-backed breakpoints on your own source lines, plus a Reactive
  scope that shows cells and their dependency edges
- **Snippets** — `fn`, `struct`, `signal`, `derived`, `effect on`, `watch … from db`,
  `push watch`, and the rest of the v2 surface

## Requirements

The extension drives the `luke` compiler, so you need it built:

```bash
git clone https://github.com/lucasdmarshall/LukeLang.git
cd LukeLang/vm && make
```

The extension auto-detects `vm/build/luke` in the workspace. Point
`lukelang.lukePath` at the binary if it lives elsewhere.

Debugging additionally needs `gdb` on your `PATH`.

## Settings

| Setting | Default | What it does |
| --- | --- | --- |
| `lukelang.lukePath` | `""` | Path to the `luke` binary; empty means auto-detect |
| `lukelang.enableLsp` | `true` | Start `luke LSP` for `.lk` / `.luke` files |
| `lukelang.enableDebug` | `true` | Enable `luke DAP` debug sessions |

## Debugging a file

Launch type `lukelang`:

```json
{
  "type": "lukelang",
  "request": "launch",
  "name": "Debug Luke",
  "program": "${file}",
  "stopOnEntry": true
}
```

The repository's `.vscode/launch.json` has worked examples.

## Links

- [lukelang.org](https://lukelang.org) — documentation, examples, downloads
- [Getting started](https://lukelang.org/docs/getting-started/)
- [Editor tooling](https://lukelang.org/docs/editor-tooling/)
- [Report an issue](https://github.com/lucasdmarshall/LukeLang/issues)

## Contributing to the extension

```bash
cd tools/vscode/lukelang
npm install
npm run check      # syntax-check the sources
npm run package    # build an installable .vsix
```

Press `F5` in VS Code to launch an Extension Development Host. From the repository root,
`bash scripts/vscode_extension_package.sh` does the install-check-package sequence in one step
and is what CI runs.

Built by **Kaung Myat San**. MIT licensed.
