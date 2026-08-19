# LukeLang official icons

Place these files here (from your local machine):

| File | Use |
| --- | --- |
| `lukelang128x128.png` | **VS Code Marketplace extension icon** (`package.json`) |
| `lukelang256x256.png` | High-res previews / store assets |
| `lukelang500x500.png` | README + `assets/lukelang-logo.png` repo branding |

After copying, from repo root:

```bash
git add tools/vscode/lukelang/icons/
git commit -m "Add official LukeLang extension icons"
git push origin main
```

Then tell the cloud agent: **icons pushed**.
