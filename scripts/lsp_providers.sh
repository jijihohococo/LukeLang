#!/usr/bin/env bash
# Prove LSP tasks 8–11: rich hover, providers, context completion, didChange re-diagnose.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUKE="${LUKE:-$ROOT/vm/build/luke}"
if [[ ! -x "$LUKE" ]]; then
  echo "lsp_providers: missing luke at $LUKE (make -C vm first)" >&2
  exit 1
fi

python3 - "$LUKE" <<'PY'
import json, subprocess, sys

luke = sys.argv[1]

def req(obj):
    b = json.dumps(obj).encode()
    return f"Content-Length: {len(b)}\r\n\r\n".encode() + b

def parse_messages(raw: bytes):
    out = []
    i = 0
    while i < len(raw):
        if raw.startswith(b"Content-Length:", i):
            end = raw.find(b"\r\n\r\n", i)
            header = raw[i:end].decode()
            n = int(header.split(":")[1].strip())
            body = raw[end+4:end+4+n]
            out.append(json.loads(body))
            i = end + 4 + n
        else:
            i += 1
    return out

SRC = """THIS IS FUNCTION add WITH a AS NUMBER, b AS NUMBER GIVES BACK NUMBER DO
  GIVE BACK ADD a AND b
END FUNCTION

REMEMBER price AS 100
THE total IS price MULTIPLIED BY price

SPEAK ASK add WITH price, total
"""

BAD = "MY NAME IS n AS NOMBER SET TO 1\nSPEAK n\n"
GOOD = "MY NAME IS n AS NUMBER SET TO 1\nSPEAK n\n"

uri = "file:///tmp/luke_lsp_providers.luke"
uri2 = "file:///tmp/luke_lsp_incremental.luke"

msgs = b"".join([
    req({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}),
    req({"jsonrpc":"2.0","method":"initialized","params":{}}),
    req({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{
        "textDocument":{"uri":uri,"languageId":"luke","version":1,"text":SRC}}}),
    req({"jsonrpc":"2.0","id":10,"method":"textDocument/hover","params":{
        "textDocument":{"uri":uri},"position":{"line":0,"character":18}}}),
    req({"jsonrpc":"2.0","id":11,"method":"textDocument/hover","params":{
        "textDocument":{"uri":uri},"position":{"line":4,"character":10}}}),
    req({"jsonrpc":"2.0","id":20,"method":"textDocument/documentSymbol","params":{
        "textDocument":{"uri":uri}}}),
    req({"jsonrpc":"2.0","id":21,"method":"textDocument/references","params":{
        "textDocument":{"uri":uri},"position":{"line":4,"character":10},"context":{"includeDeclaration":True}}}),
    req({"jsonrpc":"2.0","id":22,"method":"textDocument/rename","params":{
        "textDocument":{"uri":uri},"position":{"line":4,"character":10},"newName":"cost"}}),
    req({"jsonrpc":"2.0","id":23,"method":"textDocument/signatureHelp","params":{
        "textDocument":{"uri":uri},"position":{"line":7,"character":18}}}),
    req({"jsonrpc":"2.0","id":24,"method":"textDocument/formatting","params":{
        "textDocument":{"uri":uri},"options":{"tabSize":2,"insertSpaces":True}}}),
    req({"jsonrpc":"2.0","id":25,"method":"textDocument/semanticTokens/full","params":{
        "textDocument":{"uri":uri}}}),
    req({"jsonrpc":"2.0","id":26,"method":"textDocument/codeAction","params":{
        "textDocument":{"uri":uri},
        "range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}},
        "context":{"diagnostics":[]}}}),
    # 10 completion after ASK (cursor on space after ASK)
    req({"jsonrpc":"2.0","id":30,"method":"textDocument/completion","params":{
        "textDocument":{"uri":uri},"position":{"line":7,"character":10}}}),
    # after "AS " on function header → type keywords
    req({"jsonrpc":"2.0","id":31,"method":"textDocument/completion","params":{
        "textDocument":{"uri":uri},"position":{"line":0,"character":31}}}),
    req({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{
        "textDocument":{"uri":uri2,"languageId":"luke","version":1,"text":BAD}}}),
    req({"jsonrpc":"2.0","method":"textDocument/didChange","params":{
        "textDocument":{"uri":uri2,"version":2},
        "contentChanges":[{"text":GOOD}]}}),
    req({"jsonrpc":"2.0","method":"textDocument/didChange","params":{
        "textDocument":{"uri":uri2,"version":3},
        "contentChanges":[{"text":BAD}]}}),
    req({"jsonrpc":"2.0","id":2,"method":"shutdown","params":None}),
    req({"jsonrpc":"2.0","method":"exit"}),
])

raw = subprocess.check_output([luke, "LSP"], input=msgs, timeout=15)
msgs_out = parse_messages(raw)

by_id = {m.get("id"): m for m in msgs_out if "id" in m}
diags = [m for m in msgs_out if m.get("method") == "textDocument/publishDiagnostics"]

caps = by_id[1]["result"]["capabilities"]
assert caps.get("hoverProvider") is True
assert caps.get("documentSymbolProvider") is True
assert caps.get("referencesProvider") is True
assert caps.get("renameProvider") is True
assert caps.get("documentFormattingProvider") is True
assert "signatureHelpProvider" in caps
assert "semanticTokensProvider" in caps
assert "codeActionProvider" in caps
assert caps.get("completionProvider")

h = by_id[10]["result"]["contents"]["value"]
assert "add" in h and "function" in h.lower()
assert "NUMBER" in h
assert "WITH" in h or "GIVES BACK" in h

h2 = by_id[11]["result"]["contents"]["value"]
assert "price" in h2 and ("cell" in h2.lower() or "NUMBER" in h2)

syms = by_id[20]["result"]
names = {s["name"] for s in syms}
assert "add" in names and "price" in names
assert any(s["name"] == "add" and "GIVES BACK" in (s.get("detail") or "") for s in syms)

refs = by_id[21]["result"]
assert len(refs) >= 2

ren = by_id[22]["result"]["changes"][uri]
assert len(ren) >= 2
assert all(e["newText"] == "cost" for e in ren)

sh = by_id[23]["result"]
assert sh and sh["signatures"] and "add" in sh["signatures"][0]["label"]
assert len(sh["signatures"][0]["parameters"]) == 2

fmt = by_id[24]["result"]
assert fmt and isinstance(fmt, list) and fmt and "newText" in fmt[0]

sem = by_id[25]["result"]["data"]
assert isinstance(sem, list) and len(sem) >= 5

acts = by_id[26]["result"]
assert any(a.get("title") == "Format document" for a in acts)

comp = by_id[30]["result"]
items = comp["items"] if isinstance(comp, dict) else comp
labels = [i["label"] for i in items]
assert "add" in labels, labels[:20]
add_items = [i for i in items if i["label"] == "add"]
assert add_items

comp2 = by_id[31]["result"]
items2 = comp2["items"] if isinstance(comp2, dict) else comp2
labels2 = [i["label"] for i in items2]
assert "NUMBER" in labels2, labels2[:20]

uri2_diags = [d for d in diags if d["params"]["uri"] == uri2]
assert len(uri2_diags) >= 3
assert len(uri2_diags[0]["params"]["diagnostics"]) >= 1, "bad open should diagnose"
assert len(uri2_diags[1]["params"]["diagnostics"]) == 0, "good change should clear diags"
assert len(uri2_diags[2]["params"]["diagnostics"]) >= 1, "bad again should re-diagnose"

print("lsp_providers_ok=1")
PY

echo "lsp_providers_ok=1"
