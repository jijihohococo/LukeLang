# LukeLang VS Code Extension (Official)

This extension adds:

- `.luke` language registration
- syntax highlighting
- snippets for common LukeLang blocks
- keyword/type auto-suggest completions

## Local development install

From repository root:

```bash
cd tools/vscode/lukelang
```

Then in VS Code:

1. Open this folder.
2. Press `F5` to launch an Extension Development Host.
3. Open a `.luke` file in the host window.

## Packaging (optional)

If you want to package a `.vsix`:

```bash
npm install -g @vscode/vsce
vsce package
```

## Notes on LSP/DAP

LukeLang already ships protocol servers in the CLI:

- `luke LSP`
- `luke DAP`

This first extension iteration focuses on language assets and completion.
LSP/DAP client wiring can be layered next by launching `vm/build/luke LSP` and
`vm/build/luke DAP` from a workspace-aware client adapter.
