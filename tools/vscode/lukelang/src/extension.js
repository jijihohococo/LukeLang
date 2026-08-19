const fs = require("fs");
const vscode = require("vscode");
const {
  LanguageClient,
  TransportKind
} = require("vscode-languageclient/node");
const {
  resolveLukePath,
  resolveServerCwd,
  ensureLukeBinary
} = require("./lukePath");

const KEYWORDS = [
  "SPEAK",
  "SAY",
  "YELL",
  "MY NAME IS",
  "REMEMBER",
  "SECRET REMEMBER",
  "THE",
  "THIS IS FUNCTION",
  "GIVE BACK",
  "IF",
  "ELSE",
  "END IF",
  "WHEN",
  "END WHEN",
  "WHEN REACTIVE",
  "END WHEN REACTIVE",
  "BEGIN REACTIVE BATCH",
  "END REACTIVE BATCH",
  "IMPORT",
  "ASK",
  "WITH",
  "BIND",
  "WATCH",
  "PUSH WATCH",
  "BLUEPRINT",
  "CLASS",
  "END BLUEPRINT",
  "END CLASS",
  "METHOD",
  "WHEN BORN",
  "END BORN",
  "TEST",
  "MAKE SURE",
  "NUMBER",
  "INTEGER",
  "TEXT",
  "FLAG",
  "JSON",
  "LIST",
  "MAP",
  "REQUEST",
  "SERVER",
  "DATABASE"
];

/** @type {LanguageClient | undefined} */
let client;

async function startLanguageClient(context) {
  const config = vscode.workspace.getConfiguration("lukelang");
  if (!config.get("enableLsp", true)) {
    return;
  }

  const folder = vscode.workspace.workspaceFolders?.[0];
  const lukePath = ensureLukeBinary(folder);
  if (!lukePath) {
    vscode.window.showWarningMessage(
      "LukeLang LSP: could not find vm/build/luke. Build with: cd vm && make"
    );
    return;
  }

  const serverOptions = {
    command: lukePath,
    args: ["LSP"],
    transport: TransportKind.stdio,
    options: {
      cwd: resolveServerCwd(folder)
    }
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "lukelang" }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.luke")
    }
  };

  client = new LanguageClient(
    "lukelangLsp",
    "LukeLang Language Server",
    serverOptions,
    clientOptions
  );
  context.subscriptions.push(client);
  await client.start();
}

function registerKeywordCompletion(context) {
  const provider = vscode.languages.registerCompletionItemProvider(
    { language: "lukelang" },
    {
      provideCompletionItems() {
        return KEYWORDS.map((kw) => {
          const item = new vscode.CompletionItem(
            kw,
            vscode.CompletionItemKind.Keyword
          );
          item.insertText = kw;
          return item;
        });
      }
    }
  );
  context.subscriptions.push(provider);
}

function registerDebugAdapter(context) {
  const config = vscode.workspace.getConfiguration("lukelang");
  if (!config.get("enableDebug", true)) {
    return;
  }

  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("lukelang", {
      createDebugAdapterDescriptor(session) {
        const folder = session.workspaceFolder;
        const lukePath = ensureLukeBinary(folder);
        if (!lukePath) {
          throw new Error(
            "LukeLang debug: vm/build/luke not found. Run: cd vm && make"
          );
        }
        return new vscode.DebugAdapterExecutable(lukePath, ["DAP"], {
          cwd: resolveServerCwd(folder)
        });
      }
    })
  );

  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider("lukelang", {
      resolveDebugConfigurationWithSubstitutedVariables(folder, config) {
        if (!config.type || config.type !== "lukelang") {
          return config;
        }
        if (!config.request) {
          config.request = "launch";
        }
        if (!config.name) {
          config.name = "Debug Luke";
        }
        if (!config.program) {
          const active = vscode.window.activeTextEditor;
          if (active?.document.languageId === "lukelang") {
            config.program = active.document.uri.fsPath;
          }
        }
        if (!config.program) {
          vscode.window.showErrorMessage(
            "LukeLang debug: open a .luke file or set launch.json program"
          );
          return null;
        }
        const lukePath = ensureLukeBinary(folder);
        if (!lukePath || (lukePath !== "luke" && !fs.existsSync(lukePath))) {
          vscode.window.showErrorMessage(
            "LukeLang debug: vm/build/luke not found. Run: cd vm && make"
          );
          return null;
        }
        if (config.stopOnEntry === undefined) {
          config.stopOnEntry = true;
        }
        return config;
      }
    })
  );
}

function activate(context) {
  registerKeywordCompletion(context);
  registerDebugAdapter(context);
  startLanguageClient(context).catch((err) => {
    vscode.window.showErrorMessage(`LukeLang LSP failed to start: ${err.message}`);
  });
}

function deactivate() {
  if (client) {
    return client.stop();
  }
}

module.exports = {
  activate,
  deactivate
};
