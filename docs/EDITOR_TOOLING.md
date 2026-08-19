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

## Publish to Marketplace

Prerequisites:

1. Create publisher at https://marketplace.visualstudio.com/manage (e.g. `lukelang`)
2. Create a PAT with **Marketplace → Manage** scope
3. Add repo secret `VSCE_PAT` (or export locally)

Then:

```bash
export VSCE_PAT=your_token_here
bash scripts/vscode_extension_publish.sh
```

Or trigger GitHub Action: **Publish VS Code Extension** (workflow_dispatch).

Official icon: `icons/lukelang128x128.png` (bundled in `.vsix`).

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
