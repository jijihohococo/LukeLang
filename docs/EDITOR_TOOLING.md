# Editor Tooling (VS Code + protocol servers)

LukeLang runtime already provides protocol servers:

- `luke LSP` (hover, symbols, references, rename, formatting, semantic tokens, completion)
- `luke DAP` (debug adapter over gdb backend)

## Official VS Code extension

Path: `tools/vscode/lukelang/`

Capabilities:

- `.luke` language registration + syntax highlighting
- snippets + keyword/type completion
- **LSP client** auto-starts `vm/build/luke LSP` (or `lukelang.lukePath`)
- **DAP debug** launches `vm/build/luke DAP` with launch configs

Settings:

- `lukelang.lukePath` — override binary path
- `lukelang.enableLsp` — toggle LSP
- `lukelang.enableDebug` — toggle debug adapter

## Package `.vsix`

From repo root:

```bash
bash scripts/vscode_extension_package.sh
```

Output: `tools/vscode/lukelang/dist/*.vsix`

Install locally in VS Code: **Extensions → … → Install from VSIX…**

## Dev loop

```bash
cd tools/vscode/lukelang
npm install
```

Open that folder in VS Code and press `F5` (Extension Development Host).

Repo root also includes sample debug configs in `.vscode/launch.json`.

## Protocol smoke tests

```bash
cd vm && make
bash scripts/lsp_providers.sh
bash scripts/dap_handshake.sh
bash scripts/vscode_extension_package.sh
```
