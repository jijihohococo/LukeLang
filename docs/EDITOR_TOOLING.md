# Editor Tooling (VS Code + protocol servers)

LukeLang runtime already provides protocol servers:

- `luke LSP` (hover, symbols, references, rename, formatting, semantic tokens, completion)
- `luke DAP` (debug adapter over gdb backend)

## Official VS Code language assets

This repo now includes an official VS Code extension scaffold at:

- `tools/vscode/lukelang/`

Current capabilities:

- `.luke` language registration
- syntax highlighting
- snippets
- keyword/type completion suggestions

## Run the protocol checks

From repo root:

```bash
cd vm && make
bash ../scripts/lsp_providers.sh
bash ../scripts/dap_handshake.sh
```

## Extension dev loop

```bash
cd tools/vscode/lukelang
```

Open that folder in VS Code and press `F5` to run an Extension Development Host.

## Next layer (planned)

- Wire an LSP client to launch `vm/build/luke LSP` automatically.
- Wire a DAP client to launch `vm/build/luke DAP` for `.luke` debug sessions.
- Add semantic token parity tests against `scripts/lsp_providers.sh`.
