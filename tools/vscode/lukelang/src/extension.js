const vscode = require("vscode");

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

function activate(context) {
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

function deactivate() {}

module.exports = {
  activate,
  deactivate
};
