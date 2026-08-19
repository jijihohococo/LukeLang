const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

function resolveLukePath(workspaceFolder) {
  const config = vscode.workspace.getConfiguration("lukelang");
  const configured = (config.get("lukePath") || "").trim();
  if (configured && fs.existsSync(configured)) {
    return configured;
  }

  const roots = [];
  if (workspaceFolder) {
    roots.push(workspaceFolder.uri.fsPath);
  } else if (vscode.workspace.workspaceFolders) {
    for (const folder of vscode.workspace.workspaceFolders) {
      roots.push(folder.uri.fsPath);
    }
  }

  for (const root of roots) {
    const candidate = path.join(root, "vm", "build", "luke");
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }

  return configured || "luke";
}

function resolveServerCwd(workspaceFolder) {
  const roots = [];
  if (workspaceFolder) {
    roots.push(workspaceFolder.uri.fsPath);
  } else if (vscode.workspace.workspaceFolders?.length) {
    roots.push(vscode.workspace.workspaceFolders[0].uri.fsPath);
  }

  for (const root of roots) {
    const vmDir = path.join(root, "vm");
    if (fs.existsSync(path.join(vmDir, "build", "luke"))) {
      return vmDir;
    }
  }
  return undefined;
}

function ensureLukeBinary(workspaceFolder) {
  const lukePath = resolveLukePath(workspaceFolder);
  if (lukePath === "luke" || fs.existsSync(lukePath)) {
    return lukePath;
  }
  return null;
}

module.exports = {
  resolveLukePath,
  resolveServerCwd,
  ensureLukeBinary
};
