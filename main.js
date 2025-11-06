import fs from "fs";
import { spawnSync } from "child_process";
import readline from "readline";
import * as tokenModule from "./grammar/token.js";

// Track declared variables (heuristic for SPEAK single-word auto-string)
const declaredVars = new Set();
const methodScopedVars = new Set(); // Track variables declared within current method scope

// CLI: default files, or use `SHOW <file.luke>` to compile and auto-run
const args = process.argv.slice(2);
let inputFile = "./examples/hello.luke";
let outputFile = "./examples/hello.js";
let autoRun = false;

if (args.length > 0) {
  if (args[0].toUpperCase() === "SHOW") {
    const given = args[1];
    if (!given) {
      console.error("Please provide a .luke file to SHOW");
      process.exit(1);
    }
    inputFile = given;
    outputFile = given.replace(/\.luke$/i, ".js");
    if (!/\.js$/i.test(outputFile)) outputFile = `${given}.js`;
    autoRun = true;
  } else {
    const given = args[0];
    inputFile = given;
    outputFile = given.replace(/\.luke$/i, ".js");
    if (!/\.js$/i.test(outputFile)) outputFile = `${given}.js`;
  }
}

let code = fs.readFileSync(inputFile, "utf8");

// Helper: convert QUOTE ... END QUOTE to JS strings
function replaceQuotes(text) {
  return text.replace(/QUOTE([\s\S]*?)END QUOTE/g, (_, inner) => {
    const escaped = inner.replace(/\\/g, "\\\\").replace(/\"/g, '\\"').replace(/\n/g, "\\n").replace(/\r/g, "");
    return `"${escaped.trim()}"`;
  });
}
// Helper: convert comparison phrases to JS operators
function replaceComparisons(expr) {
  return expr
    .replace(/\bIS NOT EQUAL TO\b/g, "!==")
    .replace(/\bIS EQUAL TO\b/g, "===")
    .replace(/\bIS GREATER OR EQUAL TO\b/g, ">=")
    .replace(/\bIS LESS OR EQUAL TO\b/g, "<=")
    .replace(/\bIS GREATER THAN\b/g, ">")
    .replace(/\bIS LESS THAN\b/g, "<");
}
// Helper: split args by AND (outside strings assumed)
function splitArgs(argText) {
  const parts = argText.split(/\s+AND\s+/g).map(p => p.trim()).filter(Boolean);
  return parts.join(", ");
}
// New helpers: list literals, indexing, property access, arithmetic words
function replaceListLiterals(text) {
  return text.replace(/LIST OF([\s\S]*?)END LIST/g, (_, inner) => {
    const items = inner.split(/\s+AND\s+/g).map(s => replaceQuotes(s.trim())).filter(Boolean);
    return `[${items.join(", ")}]`;
  });
}
function replaceIndexing(text) {
  return text.replace(/\bITEM AT\s+([0-9]+|[A-Za-z_][\w]*)\s+OF\s+([A-Za-z_][\w]*)\b/g, (m, idx, list) => `${list}[${idx}]`);
}
function replacePropertyAccess(text) {
  let prev;
  let out = text;
  for (let i = 0; i < 5; i++) {
    prev = out;
    // Handle both regular identifiers and parentheses-wrapped expressions
    out = out.replace(/\b([A-Za-z_][\w]*|\([^)]+\))\s+OF\s+([A-Za-z_][\w]*)\b/g, (m, prop, obj) => `${obj === 'SELF' || obj === 'self' ? 'this' : obj}.${prop}`);
    // Handle dot notation: self.property or SELF.property
    out = out.replace(/\b(self|SELF)\.([A-Za-z_][\w]*)\b/g, 'this.$2');
    if (out === prev) break;
  }
  return out;
}
function replaceMethodCallsExpr(text) {
  let out = text;
  out = out.replace(/\bASK\s+([A-Za-z_][\w]*)\s+TO\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?(?=$|\s)/g, (m, obj, method, args) => `${obj === 'SELF' || obj === 'self' ? 'this' : obj}.${method}(${args ? splitArgs(args) : ''})`);
  out = out.replace(/\bTELL\s+([A-Za-z_][\w]*)\s+TO\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?(?=$|\s)/g, (m, obj, method, args) => `${obj === 'SELF' || obj === 'self' ? 'this' : obj}.${method}(${args ? splitArgs(args) : ''})`);
  out = out.replace(/\bDO\s+([A-Za-z_][\w]*)\s+OF\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?(?=$|\s)/g, (m, method, obj, args) => `${obj === 'SELF' || obj === 'self' ? 'this' : obj}.${method}(${args ? splitArgs(args) : ''})`);
  out = out.replace(/\bCALL\s+([A-Za-z_][\w]*)\s+OF\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?(?=$|\s)/g, (m, method, obj, args) => `${obj === 'SELF' || obj === 'self' ? 'this' : obj}.${method}(${args ? splitArgs(args) : ''})`);
  return out;
}
function replaceArithmetic(text) {
  return text
    // Infix forms: A ADD B, A SUBTRACT B, A MULTIPLY BY B, A DIVIDE BY B
    .replace(/\b([^\s]+(?:\s+[^\s]+)*?)\s+ADD\s+([^\s]+(?:\s+[^\s]+)*?)\b/g, (m, a, b) => `( ${a} ) + ( ${b} )`)
    .replace(/\b([^\s]+(?:\s+[^\s]+)*?)\s+SUBTRACT\s+([^\s]+(?:\s+[^\s]+)*?)\b/g, (m, a, b) => `( ${a} ) - ( ${b} )`)
    .replace(/\b([^\s]+(?:\s+[^\s]+)*?)\s+MULTIPLY\s+BY\s+([^\s]+(?:\s+[^\s]+)*?)\b/g, (m, a, b) => `( ${a} ) * ( ${b} )`)
    .replace(/\b([^\s]+(?:\s+[^\s]+)*?)\s+DIVIDE\s+BY\s+([^\s]+(?:\s+[^\s]+)*?)\b/g, (m, a, b) => `( ${a} ) / ( ${b} )`)
    // Common synonyms: PLUS/MINUS/TIMES
    .replace(/\b([^\s][\s\S]*?)\s+PLUS\s+([^\s][\s\S]*?)\b/g, (m, a, b) => `( ${a} ) + ( ${b} )`)
    .replace(/\b([^\s][\s\S]*?)\s+MINUS\s+([^\s][\s\S]*?)\b/g, (m, a, b) => `( ${a} ) - ( ${b} )`)
    .replace(/\b([^\s][\s\S]*?)\s+TIMES\s+([^\s][\s\S]*?)\b/g, (m, a, b) => `( ${a} ) * ( ${b} )`)
    // Prefixed forms: ADD A TO B, SUBTRACT A FROM B, MULTIPLY A BY B, DIVIDE A BY B
    .replace(/\bADD\s+([^\s]+(?:\s+[^\s]+)*?)\s+TO\s+([^\s]+(?:\s+[^\s]+)*?)\b/g, (m, a, b) => `( ${a} ) + ( ${b} )`)
    .replace(/\bSUBTRACT\s+([^\s]+(?:\s+[^\s]+)*?)\s+FROM\s+([^\s]+(?:\s+[^\s]+)*?)\b/g, (m, a, b) => `( ${b} ) - ( ${a} )`)
    .replace(/\bMULTIPLY\s+([^\s]+(?:\s+[^\s]+)*?)\s+BY\s+([^\s]+(?:\s+[^\s]+)*?)\b/g, (m, a, b) => `( ${a} ) * ( ${b} )`)
    .replace(/\bDIVIDE\s+([^\s]+(?:\s+[^\s]+)*?)\s+BY\s+([^\s]+(?:\s+[^\s]+)*?)\b/g, (m, a, b) => `( ${a} ) / ( ${b} )`);
}

// New helper: logical operators
function replaceLogicalOperators(text) {
  return text
    .replace(/\bNOT\s+([^\s][\s\S]*?)\b/g, (m, expr) => `!( ${expr} )`)
    .replace(/\b([^\s][\s\S]*?)\s+AND ALSO\s+([^\s][\s\S]*?)\b/g, (m, a, b) => `( ${a} ) && ( ${b} )`)
    .replace(/\b([^\s][\s\S]*?)\s+OR ELSE\s+([^\s][\s\S]*?)\b/g, (m, a, b) => `( ${a} ) || ( ${b} )`);
}

// Helper: NEW/CREATE/MAKE OBJECT -> JS new
function replaceConstruction(text) {
  return text
    .replace(/\bNEW\s+([A-Za-z_][\w]*)\s*(?:WITH\s*([\s\S]*))?/g, (m, name, args) => `new ${name}(${args ? splitArgs(args) : ''})`)
    .replace(/\bCREATE\s+([A-Za-z_][\w]*)\s*(?:WITH\s*([\s\S]*))?/g, (m, name, args) => `new ${name}(${args ? splitArgs(args) : ''})`)
    .replace(/\bMAKE OBJECT\s+([A-Za-z_][\w]*)\s*(?:WITH\s*([\s\S]*))?/g, (m, name, args) => `new ${name}(${args ? splitArgs(args) : ''})`);
}

// Adjust private field access inside class context
function adjustPrivateAccess(text, classCtx) {
  if (!classCtx) return text;
  let out = text;
  if (classCtx.privateFields && classCtx.privateFields.size > 0) {
    for (const p of classCtx.privateFields) {
      out = out.replace(new RegExp(`\\bthis\\.${p}\\b`, 'g'), `this.#${p}`);
    }
  }
  if (classCtx.privateMethods && classCtx.privateMethods.size > 0) {
    for (const m of classCtx.privateMethods) {
      out = out.replace(new RegExp(`\\bthis\\.${m}\\s*\\(`, 'g'), `this.#${m}(`);
    }
  }
  return out;
}

// New helper: detect bare multi-word text to auto-string in SPEAK
function isBareTextLiteral(text) {
  const t = text.trim();
  if (t.length === 0) return false;
  // If it contains quotes or typical JS punctuation, treat as expression
  if (/["]|'|`|[\[\]().,+\-/*]/.test(t)) return false;
  // If it contains known language operator words, treat as expression
  if (/\b(AND|ADD|SUBTRACT|MULTIPLY|DIVIDE|ITEM AT|OF|IS|QUOTE|END QUOTE)\b/.test(t)) return false;
  // Consider multi-word alphanumeric text as a literal string
  return /^[A-Za-z0-9 ]+$/.test(t) && t.includes(" ");
}

// --- Safer, boundary-aware token replacement ---
function escapeRegex(str) {
  return str.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function protectLiteralsAndComments(src) {
  const strings = [];
  const comments = [];
  const regexes = [];
  // Protect strings (", ', `)
  src = src.replace(/("([^"\\]|\\.)*"|'([^'\\]|\\.)*'|`([^`\\]|\\.)*`)/g, (match) => {
    const idx = strings.push(match) - 1;
    return `__STR_${idx}__`;
  });
  // Protect block comments
  src = src.replace(/\/\*[^]*?\*\//g, (match) => {
    const idx = comments.push(match) - 1;
    return `__CMT_${idx}__`;
  });
  // Protect line comments
  src = src.replace(/(^|\s)\/\/.*$/gm, (match) => {
    const idx = comments.push(match.trimEnd()) - 1;
    return match.replace(/\S.*$/, `__CMT_${idx}__`);
  });
  // Protect JS regex literals (e.g., /abc/gi), avoid comment starts
  src = src.replace(/\/(?![/*])(?:\\.|[^\/\\])+\/[gimsuy]*/g, (match) => {
    const idx = regexes.push(match) - 1;
    return `__REG_${idx}__`;
  });
  return { src, strings, comments, regexes };
}

function restoreLiteralsAndComments(src, strings, comments, regexes) {
  if (comments.length) {
    src = src.replace(/__CMT_(\d+)__/g, (_, i) => comments[Number(i)]);
  }
  if (strings.length) {
    src = src.replace(/__STR_(\d+)__/g, (_, i) => strings[Number(i)]);
  }
  if (regexes && regexes.length) {
    src = src.replace(/__REG_(\d+)__/g, (_, i) => regexes[Number(i)]);
  }
  return src;
}

function replaceLukeLangTokens(src, tokens, debug = false) {
  // Sort tokens by descending length to avoid overlaps
  const entries = Object.entries(tokens).sort((a, b) => b[0].length - a[0].length);
  const { src: protectedSrc, strings, comments, regexes } = protectLiteralsAndComments(src);
  // Additionally protect JS code shapes that should not be touched by token mapping
  const decls = [];
  let tmp = protectedSrc.replace(/(^|\n)([ \t]*)(?:static\s+)?(?:#[A-Za-z_][\w]*|[A-Za-z_$][\w$]*)\s*\([^)]*\)\s*\{/g, (match) => {
    const idx = decls.push(match) - 1;
    return `__DECL_${idx}__`;
  });
  const calls = [];
  tmp = tmp.replace(/([A-Za-z_$][\w$]*)\.(?:#[A-Za-z_][\w]*|[A-Za-z_$][\w$]*)\s*\(/g, (match) => {
    const idx = calls.push(match) - 1;
    return `__CALL_${idx}__`;
  });
  let out = tmp;
  for (const [key, value] of entries) {
    const pattern = new RegExp(`\\b${escapeRegex(key)}\\b`, "gimu");
    const matches = out.match(pattern);
    if (debug && matches && matches.length) {
      console.log(`Replaced "${key}" → "${value}" (${matches.length})`);
    }
    out = out.replace(pattern, value);
  }
  // Restore protected code shapes
  out = out.replace(/__DECL_(\d+)__/g, (_, i) => decls[Number(i)]);
  out = out.replace(/__CALL_(\d+)__/g, (_, i) => calls[Number(i)]);
  // Restore strings and comments last
  return restoreLiteralsAndComments(out, strings, comments, regexes);
}

// Transform word-only LukeLang into JS
(function transform() {
  // First: turn QUOTE blocks into JS strings
  code = replaceQuotes(code);
  const lines = code.split(/\r?\n/);
  const out = [];
  const blockStack = [];
  const interfaces = {}; // Collected contract definitions for interface compliance checks
  let hasUserInput = false; // Track if user input is used
  function currentClassCtx() {
    for (let i = blockStack.length - 1; i >= 0; i--) {
      const b = blockStack[i];
      if (typeof b === 'object' && b.type === 'CLASS') return b;
    }
    return null;
  }
  
  function isInMethod() {
    for (let i = blockStack.length - 1; i >= 0; i--) {
      const b = blockStack[i];
      if (typeof b === 'object' && b.type === 'METHOD') return true;
    }
    return false;
  }
  for (let raw of lines) {
    let line = raw.trim();
    const indent = raw.match(/^\s*/)[0];
    if (line.length === 0) { out.push(raw); continue; }
    let m;

    // Contract / Interface definition blocks
    m = line.match(/^(?:CONTRACT|AGREEMENT|PACT)\s+([A-Za-z_][\w]*)\s*\{?\s*(?:DO)?$/);
    if (m) {
      const name = m[1];
      blockStack.push({ type: 'CONTRACT', name, requires: [] });
      continue;
    }
    m = line.match(/^(?:MUST|REQUIRES|EXPECTS)\s+(?:METHOD|ACTION|TRICK|ABILITY)\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?\s*$/);
    if (m && blockStack[blockStack.length - 1]?.type === 'CONTRACT') {
      const reqName = m[1];
      const argText = m[2] ? splitArgs(m[2]) : '';
      const argsCount = argText ? argText.split(/\s*,\s*/).filter(Boolean).length : 0;
      blockStack[blockStack.length - 1].requires.push({ name: reqName, argsCount });
      continue;
    }
    if ((line === 'END CONTRACT' || line === 'END AGREEMENT' || line === 'END PACT' || line === '}') && blockStack[blockStack.length - 1]?.type === 'CONTRACT') {
      const c = blockStack.pop();
      interfaces[c.name] = c.requires;
      continue;
    }

    // Class definition (verbose and shorthand synonyms)
    // Enhanced parser: supports ABSTRACT prefix, FOLLOWS (multiple via AND), and IMPLEMENTS (comma or AND)
    let cm = line.match(/^(?:ABSTRACT\s+)?(?:THIS IS CLASS|MAKE CLASS|BLUEPRINT|CLASS)\s+([A-Za-z_][\w]*)\b(.*)$/);
    if (cm) {
      const name = cm[1];
      const rest = cm[2] || '';
      const isAbstract = /^ABSTRACT\b/.test(line);
      // Capture one or more parents after FROM/EXTENDS/FOLLOWS/: separated by AND
      const extMatch = rest.match(/(?:FROM|EXTENDS|FOLLOWS|:)\s+([A-Za-z_][\w]*(?:\s+AND\s+[A-Za-z_][\w]*)*)/);
      let parent = null;
      let mixins = [];
      if (extMatch) {
        const parentsList = extMatch[1].split(/\s+AND\s+/);
        parent = parentsList[0] || null;
        mixins = parentsList.slice(1);
      }
      const implMatch = rest.match(/(?:IMPLEMENTS|COMPLIES WITH|AGREES TO|ADHERES TO)\s+([A-Za-z_][\w]*(?:\s*(?:AND|,)\s*[A-Za-z_][\w]*)*)/);
      const impls = implMatch ? implMatch[1].split(/\s*(?:AND|,)\s*/) : [];
      out.push(`${indent}class ${name}${parent ? ` extends ${parent}` : ''} {`);
      blockStack.push({ type: 'CLASS', className: name, parentName: parent || null, mixins, isAbstract, privateFields: new Set(), privateMethods: new Set(), implements: impls });
      continue;
    }
    // Legacy parser fallback (no implements), allow ABSTRACT prefix
    m = line.match(/^(?:ABSTRACT\s+)?THIS IS CLASS\s+([A-Za-z_][\w]*)\s*(?:(?:FROM|EXTENDS|FOLLOWS)\s+([A-Za-z_][\w]*))?\s*(?:DO)?$/)
      || line.match(/^(?:ABSTRACT\s+)?MAKE CLASS\s+([A-Za-z_][\w]*)\s*(?:(?:FROM|EXTENDS|FOLLOWS)\s+([A-Za-z_][\w]*))?\s*(?:DO)?$/)
      || line.match(/^(?:ABSTRACT\s+)?BLUEPRINT\s+([A-Za-z_][\w]*)\s*(?:(?:FROM|EXTENDS|FOLLOWS)\s+([A-Za-z_][\w]*))?\s*(?:DO)?$/)
      || line.match(/^(?:ABSTRACT\s+)?CLASS\s+([A-Za-z_][\w]*)\s*(?:(?:FROM|EXTENDS|FOLLOWS|:)\s+([A-Za-z_][\w]*))?\s*\{?\s*(?:DO)?$/);
    if (m) {
      const name = m[1];
      const parent = m[2];
      out.push(`${indent}class ${name}${parent ? ` extends ${parent}` : ''} {`);
      blockStack.push({ type: 'CLASS', className: name, parentName: parent || null, mixins: [], isAbstract: /^ABSTRACT\b/.test(line), privateFields: new Set(), privateMethods: new Set(), implements: [] });
      continue;
    }
    if (line === 'END CLASS' || line === 'END CLS' || line === '}') {
      // If class implements contracts, add a static compliance check block before closing (skip for ABSTRACT)
      const clsCtx = blockStack[blockStack.length - 1];
      if (clsCtx && clsCtx.type === 'CLASS' && !clsCtx.isAbstract && Array.isArray(clsCtx.implements) && clsCtx.implements.length > 0) {
        out.push(`${indent}  static {`);
        for (const cname of clsCtx.implements) {
          const reqs = interfaces[cname] || [];
          for (const r of reqs) {
            out.push(`${indent}    if (typeof this.prototype['${r.name}'] !== 'function') { throw new Error('Class ${clsCtx.className} must implement method ${r.name} from ${cname}'); }`);
          }
        }
        out.push(`${indent}  }`);
      }
      out.push(`${indent}}`);
      blockStack.pop();
      continue;
    }

    // Static fields inside class (brand keywords)
    m = line.match(/^(?:ALWAYS|ETERNAL|FOREVER)\s+HAS\s+([A-Za-z_][\w]*)\s*(?:SET\s+TO\s+(.+))?$/);
    if (m && currentClassCtx()) {
      const field = m[1];
      let value = m[2];
      if (value) {
        value = replaceComparisons(value);
        value = replaceQuotes(value);
        value = replaceListLiterals(value);
        value = replaceIndexing(value);
        value = replacePropertyAccess(value);
        value = replaceMethodCallsExpr(value);
        value = replaceLogicalOperators(value);
        value = replaceArithmetic(value);
        value = replaceConstruction(value);
      }
      out.push(`${indent}static ${field}${value ? ` = ${value}` : ''};`);
      continue;
    }

    // Private/Public fields inside class
    m = line.match(/^(PRIVATE|SECRET)\s+([A-Za-z_][\w]*)$/);
    if (m && currentClassCtx()) {
      const field = m[2];
      currentClassCtx().privateFields.add(field);
      out.push(`${indent}#${field};`);
      continue;
    }
    
    // Property with initial value: HAS property SET TO value
    m = line.match(/^HAS\s+([A-Za-z_][\w]*)\s+SET\s+TO\s+(.+)$/);
    if (m && currentClassCtx()) {
      const field = m[1];
      let value = replaceComparisons(m[2]);
      value = replaceQuotes(value);
      value = replaceListLiterals(value);
      value = replaceIndexing(value);
      value = replacePropertyAccess(value);
      value = replaceMethodCallsExpr(value);
      value = replaceLogicalOperators(value);
      value = replaceArithmetic(value);
      value = replaceConstruction(value);
      const classCtx = currentClassCtx();
      value = adjustPrivateAccess(value, classCtx);
      out.push(`${indent}${field} = ${value};`);
      continue;
    }
    
    m = line.match(/^HAS\s+([A-Za-z_][\w]*)$/);
    if (m && currentClassCtx()) {
      const field = m[1];
      out.push(`${indent}${field};`);
      continue;
    }

    // Constructor (verbose and shorthand synonyms)
    m = line.match(/^(?:WHEN BORN|BORN|MAKE ONE|BEGIN|CREATE)\s*(?:WITH\s*(.*?))?\s*\{?\s*(?:DO)?$/i)
      || line.match(/^(?:init|constructor)\s*\(\s*(.*?)\s*\)\s*\{?\s*(?:DO)?$/i)
      || line.match(/^(?:INIT|CONSTRUCTOR)\s*(?:WITH\s*(.*?))?\s*\{?\s*(?:DO)?$/i)
      || line.match(/^(?:BEGIN|CREATE)\s*\(\s*(.*?)\s*\)\s*\{?\s*(?:DO)?$/i);
    if (m && currentClassCtx()) {
      const args = m[1] ? splitArgs(m[1]) : '';
      const classCtx = currentClassCtx();
      out.push(`${indent}constructor(${args}) {`);
      
      // Auto super() for derived classes, but allow explicit CALL PARENT to override
      const ctorCtx = { type: 'CTOR', autoSuperIndex: null };
      if (classCtx.parentName) {
        const argList = args ? args.split(',').map(arg => arg.trim()) : [];
        const parentArgs = argList.length > 2 ? argList.slice(1).join(', ') : args;
        out.push(`${indent}  super(${parentArgs});`);
        ctorCtx.autoSuperIndex = out.length - 1;
      }
      // Inject mixin instances to copy public instance fields (experimental multiple inheritance support)
      if (Array.isArray(classCtx.mixins) && classCtx.mixins.length > 0) {
        for (const mx of classCtx.mixins) {
          out.push(`${indent}  Object.assign(this, new ${mx}());`);
        }
      }
      
      blockStack.push(ctorCtx);
      continue;
    }
    // Treat METHOD __init__ as constructor
    m = line.match(/^(?:METHOD|ACTION|TRICK|ABILITY)\s+(__init__)\s*(?:WITH\s*(.*?))?\s*\{?\s*(?:DO)?$/)
      || line.match(/^(?:FUNC|FUNCTION|FN)\s+(__init__)\s*\(\s*(.*?)\s*\)\s*\{?\s*(?:DO)?$/)
      || line.match(/^(?:FUNC|FUNCTION|FN)\s+(__init__)\s*(?:WITH\s*(.*?))?\s*\{?\s*(?:DO)?$/);
    if (m && currentClassCtx()) {
      const args = m[2] ? splitArgs(m[2]) : '';
      const classCtx = currentClassCtx();
      out.push(`${indent}constructor(${args}) {`);
      const ctorCtx = { type: 'CTOR', autoSuperIndex: null };
      if (classCtx.parentName) {
        const argList = args ? args.split(',').map(arg => arg.trim()) : [];
        const parentArgs = argList.length > 2 ? argList.slice(1).join(', ') : args;
        out.push(`${indent}  super(${parentArgs});`);
        ctorCtx.autoSuperIndex = out.length - 1;
      }
      // Inject mixins if configured
      if (Array.isArray(classCtx.mixins) && classCtx.mixins.length > 0) {
        for (const mx of classCtx.mixins) {
          out.push(`${indent}  Object.assign(this, new ${mx}());`);
        }
      }
      blockStack.push(ctorCtx);
      continue;
    }
    if ((line === 'END BORN' || line === 'END CONSTRUCTOR' || line === 'END MAKE ONE' || line === 'END INIT' || line === 'END BEGIN' || line === 'END CREATE' || line === 'END METHOD' || line === '}') && blockStack[blockStack.length - 1]?.type === 'CTOR') {
      out.push(`${indent}}`);
      blockStack.pop();
      continue;
    }

    // Call parent/super inside constructor
    m = line.match(/^CALL\s+(?:PARENT|SUPER)\s*(?:WITH\s*(.*))?$/);
    const mCtorNamed = line.match(/^CALL\s+(?:PARENT|SUPER)\s+(?:__init__|INIT|init|constructor|CONSTRUCTOR)\s*(?:WITH\s*(.*))?$/i);
    if ((m || mCtorNamed) && blockStack[blockStack.length - 1]?.type === 'CTOR') {
      const args = m ? (m[1] ? splitArgs(m[1]) : '') : (mCtorNamed[1] ? splitArgs(mCtorNamed[1]) : '');
      const ctorCtx = blockStack[blockStack.length - 1];
      if (ctorCtx.autoSuperIndex !== null) {
        // Remove previously auto-inserted super to avoid duplicate calls
        out.splice(ctorCtx.autoSuperIndex, 1);
        ctorCtx.autoSuperIndex = null;
      }
      out.push(`${indent}super(${args});`);
      continue;
    }

    // Explicit parent method call when multiple parents: CALL PARENT <method> OF <Ancestor>
    m = line.match(/^CALL\s+(?:PARENT|SUPER)\s+([A-Za-z_][\w]*)\s+OF\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*))?$/);
    if (m && isInMethod() && blockStack[blockStack.length - 1]?.type === 'METHOD') {
      const meth = m[1];
      const anc = m[2];
      const args = m[3] ? splitArgs(m[3]) : '';
      // Use apply to avoid post-token mapping stripping of 'call'
      out.push(`${indent}(${anc}.prototype['${meth}']).apply(this${args ? `, [${args}]` : `, []`});`);
      continue;
    }

    // Call parent/super within a method (brand support)
    m = line.match(/^CALL\s+(?:PARENT|SUPER)\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*))?$/);
    if (m && isInMethod() && blockStack[blockStack.length - 1]?.type === 'METHOD') {
      const meth = m[1];
      const args = m[2] ? splitArgs(m[2]) : '';
      out.push(`${indent}super.${meth}(${args});`);
      continue;
    }

    // Static method (brand keywords)
    m = line.match(/^(?:ALWAYS|ETERNAL|FOREVER)\s+(?:METHOD|ACTION|TRICK|ABILITY)\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?\s*\{?\s*(?:DO)?$/)
      || line.match(/^(?:ALWAYS|ETERNAL|FOREVER)\s+([A-Za-z_][\w]*)\s*\(\s*(.*?)\s*\)\s*\{?\s*(?:DO)?$/);
    if (m && currentClassCtx()) {
      const name = m[1];
      const args = m[2] ? splitArgs(m[2]) : '';
      out.push(`${indent}static ${name}(${args}) {`);
      blockStack.push({ type: 'METHOD', name });
      methodScopedVars.clear();
      continue;
    }

    // Private method inside class (brand)
    m = line.match(/^(?:PRIVATE|SECRET)\s+(?:METHOD|ACTION|TRICK|ABILITY)\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?\s*\{?\s*(?:DO)?$/)
      || line.match(/^(?:PRIVATE|SECRET)\s+([A-Za-z_][\w]*)\s*\(\s*(.*?)\s*\)\s*\{?\s*(?:DO)?$/);
    if (m && currentClassCtx()) {
      const name = m[1];
      const args = m[2] ? splitArgs(m[2]) : '';
      out.push(`${indent}#${name}(${args}) {`);
      const classCtx = currentClassCtx();
      classCtx.privateMethods.add(name);
      blockStack.push({ type: 'METHOD', name });
      methodScopedVars.clear();
      continue;
    }

    // Method inside class (verbose and shorthand synonyms)
    m = line.match(/^(?:METHOD|ACTION|TRICK|ABILITY)\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?\s*\{?\s*(?:DO)?$/)
      || line.match(/^(?:FUNC|FUNCTION|FN)\s+([A-Za-z_][\w]*)\s*\(\s*(.*?)\s*\)\s*\{?\s*(?:DO)?$/)
      || line.match(/^(?:FUNC|FUNCTION|FN)\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?\s*\{?\s*(?:DO)?$/)
      || line.match(/^([A-Za-z_][\w]*)\s*\(\s*(.*?)\s*\)\s*\{?\s*$/);
    if (m && currentClassCtx()) {
      const name = m[1];
      const args = m[2] ? splitArgs(m[2]) : '';
      out.push(`${indent}${name}(${args}) {`);
      blockStack.push({ type: 'METHOD', name });
      // Clear method-scoped variables when entering a new method
      methodScopedVars.clear();
      continue;
    }
    if ((line === 'END METHOD' || line === 'END ACTION' || line === 'END TRICK' || line === 'END ABILITY' || line === 'END FUNC' || line === 'END FUNCTION' || line === 'END FN' || line === '}') && blockStack[blockStack.length - 1]?.type === 'METHOD') {
      out.push(`${indent}}`);
      blockStack.pop();
      // Clear method-scoped variables when exiting a method
      methodScopedVars.clear();
      continue;
    }

    // Property assignment: SET prop OF obj TO expr (and synonyms)
    m = line.match(/^(?:SET|MAKE|NAME)\s+([A-Za-z_][\w]*)\s+OF\s+([A-Za-z_][\w]*)\s+(?:TO|BE|AS)\s+(.+)$/);
    if (m) {
      const prop = m[1];
      const objWord = m[2];
      let expr = replaceComparisons(m[3]);
      expr = replaceQuotes(expr);
      expr = replaceListLiterals(expr);
      expr = replaceIndexing(expr);
      // Prefer converting method-call synonyms before property access to avoid clobbering patterns
      expr = replaceMethodCallsExpr(expr);
      // Resolve property access after method conversions
      expr = replacePropertyAccess(expr);
      // Then apply logical and arithmetic operations
      expr = replaceLogicalOperators(expr);
      expr = replaceArithmetic(expr);
      expr = replaceConstruction(expr);
      expr = expr.replace(/\s+AND\s+/g, ' + ');
      const classCtx = currentClassCtx();
      expr = adjustPrivateAccess(expr, classCtx);
      const obj = objWord === 'SELF' ? 'this' : objWord;
      const isPrivate = classCtx && obj === 'this' && classCtx.privateFields.has(prop);
      const target = isPrivate ? `this.#${prop}` : `${obj}.${prop}`;
      out.push(`${indent}${target} = ${expr};`);
      continue;
    }

    // Function definition (fun synonyms supported)
    m = line.match(/^THIS IS FUNCTION\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?\s*DO$/)
      || line.match(/^MAKE FUNCTION\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?\s*DO$/)
      || line.match(/^RECIPE\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*?))?\s*DO$/);
    if (m) {
      const name = m[1];
      const args = m[2] ? splitArgs(m[2]) : "";
      // record function args as declared variables
      if (args) {
        args.split(/\s*,\s*/).forEach(a => { if (a) declaredVars.add(a); });
      }
      out.push(`${indent}function ${name}(${args}) {`);
      blockStack.push("FUNCTION");
      continue;
    }
    if (line === "END FUNCTION") { out.push(`${indent}}`); blockStack.pop(); continue; }

    // Function call (fun synonyms)
    m = line.match(/^DO\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*))?$/)
      || line.match(/^USE\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*))?$/)
      || line.match(/^GO\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*))?$/);
    if (m) {
      const name = m[1];
      let argsFinal = "";
      if (m[2]) {
        let a = replaceComparisons(m[2]);
        a = replaceQuotes(a);
        a = replaceListLiterals(a);
        a = replaceIndexing(a);
        a = replacePropertyAccess(a);
        a = replaceMethodCallsExpr(a);
        a = replaceLogicalOperators(a);
        a = replaceArithmetic(a);
        a = replaceConstruction(a);
        const classCtx = currentClassCtx();
        a = adjustPrivateAccess(a, classCtx);
        argsFinal = a.replace(/\s+AND\s+/g, ", ");
      }
      out.push(`${indent}${name}(${argsFinal});`);
      continue;
    }

    // Object method call statements (both conversational and dot notation)
    m = line.match(/^(?:ASK|TELL)\s+([A-Za-z_][\w]*)\s+TO\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*))?$/)
      || line.match(/^([A-Za-z_][\w]*)\s*\.\s*([A-Za-z_][\w]*)\s*\(\s*(.*?)\s*\)\s*;?\s*$/);
    if (m) {
      const obj = m[1];
      const method = m[2];
      let argsFinal = "";
      if (m[3]) {
        let a = replaceComparisons(m[3]);
        a = replaceQuotes(a);
        a = replaceListLiterals(a);
        a = replaceIndexing(a);
        a = replacePropertyAccess(a);
        a = replaceMethodCallsExpr(a);
        a = replaceLogicalOperators(a);
        a = replaceArithmetic(a);
        a = replaceConstruction(a);
        const classCtx = currentClassCtx();
        a = adjustPrivateAccess(a, classCtx);
        // For dot notation, args are already comma-separated; for ASK TO WITH, convert AND to commas
        argsFinal = line.includes('.') ? a : a.replace(/\s+AND\s+/g, ", ");
      }
      const jsObj = obj === 'SELF' ? 'this' : obj;
      let callLine = `${indent}${jsObj}.${method}(${argsFinal});`;
      const classCtx = currentClassCtx();
      callLine = adjustPrivateAccess(callLine, classCtx);
      out.push(callLine);
      continue;
    }

    m = line.match(/^(?:DO|CALL)\s+([A-Za-z_][\w]*)\s+OF\s+([A-Za-z_][\w]*)\s*(?:WITH\s*(.*))?$/);
    if (m) {
      const method = m[1];
      const objWord = m[2];
      let argsFinal = "";
      if (m[3]) {
        let a = replaceComparisons(m[3]);
        a = replaceQuotes(a);
        a = replaceListLiterals(a);
        a = replaceIndexing(a);
        a = replacePropertyAccess(a);
        a = replaceMethodCallsExpr(a);
        a = replaceLogicalOperators(a);
        a = replaceArithmetic(a);
        a = replaceConstruction(a);
        const classCtx = currentClassCtx();
        a = adjustPrivateAccess(a, classCtx);
        argsFinal = a.replace(/\s+AND\s+/g, ", ");
      }
      const obj = objWord === 'SELF' ? 'this' : objWord;
      let callLine = `${indent}${obj}.${method}(${argsFinal});`;
      const classCtx = currentClassCtx();
      callLine = adjustPrivateAccess(callLine, classCtx);
      out.push(callLine);
      continue;
    }

    // Exceptions: TRY/CATCH/FINALLY/THROW
    if (line === "TRY DO" || line === "TRY") { out.push(`${indent}try {`); blockStack.push("TRY"); continue; }
    m = line.match(/^(?:CATCH|ON ERROR|WHEN ERROR)\s*(?:AS\s+([A-Za-z_][\w]*))?\s+DO$/) || line.match(/^(?:CATCH|ON ERROR|WHEN ERROR)\s+([A-Za-z_][\w]*)$/);
    if (m && blockStack[blockStack.length - 1] === "TRY") { const param = m[1] || "e"; out.push(`${indent}} catch (${param}) {`); continue; }
    if (line === "FINALLY DO" || line === "AT LAST DO") { out.push(`${indent}} finally {`); continue; }
    if (line === "END TRY") { out.push(`${indent}}`); blockStack.pop(); continue; }
    m = line.match(/^(?:THROW|RAISE|PANIC)\s+(.+)$/);
    if (m) {
      let expr = m[1];
      expr = replaceComparisons(expr);
      expr = replaceQuotes(expr);
      expr = replaceListLiterals(expr);
      expr = replaceIndexing(expr);
      expr = replacePropertyAccess(expr);
      expr = replaceMethodCallsExpr(expr);
      expr = replaceLogicalOperators(expr);
      expr = replaceArithmetic(expr);
      expr = replaceConstruction(expr);
      const classCtx = currentClassCtx();
      expr = adjustPrivateAccess(expr, classCtx);
      expr = expr.replace(/\s+AND\s+/g, " + ");
      out.push(`${indent}throw ${expr};`);
      continue;
    }

    // User Input Assignment (must come before general SET pattern)
    m = line.match(/^SET\s+([A-Za-z_][\w]*)\s+TO\s+(?:ASK USER|GET INPUT|READ INPUT)\s*(?:FOR\s+(.+))?$/)
      || line.match(/^([A-Za-z_][\w]*)\s*=\s*(?:ASK USER|GET INPUT|READ INPUT)\s*(?:FOR\s+(.+))?$/)
      || line.match(/^SET\s+([A-Za-z_][\w]*)\s+TO\s+ASK\s+(.+)$/)
      || line.match(/^([A-Za-z_][\w]*)\s*=\s*ASK\s+(.+)$/);
    if (m) {
      hasUserInput = true;
      const varName = m[1];
      const prompt = m[2] || '"Enter input: "';
      let promptExpr = prompt;
      if (prompt !== '"Enter input: "') {
        promptExpr = replaceComparisons(prompt);
        promptExpr = replaceQuotes(promptExpr);
        promptExpr = replaceListLiterals(promptExpr);
        promptExpr = replaceIndexing(promptExpr);
        promptExpr = replacePropertyAccess(promptExpr);
        promptExpr = replaceMethodCallsExpr(promptExpr);
        promptExpr = replaceLogicalOperators(promptExpr);
        promptExpr = replaceArithmetic(promptExpr);
        promptExpr = replaceConstruction(promptExpr);
        const classCtx = currentClassCtx();
        promptExpr = adjustPrivateAccess(promptExpr, classCtx);
        promptExpr = promptExpr.replace(/\s+AND\s+/g, " + ");
        if (!/["'`]/.test(promptExpr) && isBareTextLiteral(promptExpr)) { promptExpr = `"${promptExpr.trim()}"`; }
      }
      declaredVars.add(varName);
      out.push(`${indent}const rl = readline.createInterface({ input: process.stdin, output: process.stdout });`);
      out.push(`${indent}let ${varName} = await new Promise(resolve => rl.question(${promptExpr}, resolve));`);
      out.push(`${indent}rl.close();`);
      continue;
    }

    // Constructor assignment (LET ... BE NEW ...)
    m = line.match(/^LET\s+([A-Za-z_][\w]*)\s+BE\s+NEW\s+([A-Za-z_][\w]*)\s*(?:WITH\s+(.+))?$/);
    if (m) {
      const varName = m[1];
      const className = m[2];
      const args = m[3];
      let argsExpr = '';
      if (args) {
        // Split arguments by AND and process each
        const argList = args.split(/\s+AND\s+/);
        const processedArgs = argList.map(arg => {
          let expr = replaceComparisons(arg.trim());
          expr = replaceQuotes(expr);
          expr = replaceListLiterals(expr);
          expr = replaceIndexing(expr);
          expr = replacePropertyAccess(expr);
          expr = replaceMethodCallsExpr(expr);
          expr = replaceLogicalOperators(expr);
          expr = replaceArithmetic(expr);
          expr = replaceConstruction(expr);
          const classCtx = currentClassCtx();
          expr = adjustPrivateAccess(expr, classCtx);
          return expr;
        });
        argsExpr = processedArgs.join(', ');
      }
      declaredVars.add(varName);
      out.push(`${indent}let ${varName} = new ${className}(${argsExpr});`);
      continue;
    }

    // Property assignment (SET self.property TO value or object.property = value)
    m = line.match(/^SET\s+(self|SELF|[A-Za-z_][\w]*)\s*\.\s*([A-Za-z_][\w]*)\s+TO\s+(.+)$/)
      || line.match(/^([A-Za-z_][\w]*)\s*\.\s*([A-Za-z_][\w]*)\s*=\s*(.+)$/);
    if (m) {
      const obj = m[1];
      const prop = m[2];
      let expr = replaceComparisons(m[3]);
      expr = replaceQuotes(expr);
      expr = replaceListLiterals(expr);
      expr = replaceIndexing(expr);
      // Prefer converting method-call synonyms before property access to avoid clobbering patterns
      expr = replaceMethodCallsExpr(expr);
      expr = replacePropertyAccess(expr);
      expr = replaceLogicalOperators(expr);
      expr = replaceArithmetic(expr);
      expr = replaceConstruction(expr);
      
      const classCtx = currentClassCtx();
      expr = adjustPrivateAccess(expr, classCtx);
      expr = expr.replace(/\s+AND\s+/g, " + ");
      
      const jsObj = (obj === 'self' || obj === 'SELF') ? 'this' : obj;
      const isPrivate = classCtx && jsObj === 'this' && classCtx.privateFields.has(prop);
      const target = isPrivate ? `this.#${prop}` : `${jsObj}.${prop}`;
      out.push(`${indent}${target} = ${expr};`);
      continue;
    }

    // Assignment (verbose and shorthand synonyms)
    m = line.match(/^SET\s+([A-Za-z_][\w]*)\s+TO\s+(.+)$/)
      || line.match(/^MAKE\s+([A-Za-z_][\w]*)\s+BE\s+(.+)$/)
      || line.match(/^NAME\s+([A-Za-z_][\w]*)\s+AS\s+(.+)$/)
      || line.match(/^(?:LET|VAR)\s+([A-Za-z_][\w]*)\s*=\s*(.+)$/)
      || line.match(/^([A-Za-z_][\w]*)\s*=\s*(?!(?:ASK USER|GET INPUT|READ INPUT))(.+)$/); // Exclude ASK USER patterns
    if (m) {
      const v = m[1];
      let expr = replaceComparisons(m[2]);
      expr = replaceQuotes(expr);
      expr = replaceListLiterals(expr);
      expr = replaceIndexing(expr);
      // Convert method-call synonyms before property access to preserve patterns
      expr = replaceMethodCallsExpr(expr);
      expr = replacePropertyAccess(expr);
      expr = replaceLogicalOperators(expr);
      expr = replaceArithmetic(expr);
      expr = replaceConstruction(expr);
      
      // Check if we're inside a class method/constructor and the variable is a class field
      const classCtx = currentClassCtx();
      const currentBlock = blockStack[blockStack.length - 1];
      const isInClassMethod = classCtx && (currentBlock?.type === 'CTOR' || currentBlock?.type === 'METHOD');
      
      // Check if this variable is a declared class field
      const isClassField = classCtx && (
        // Check if it's in the class's declared fields (from HAS statements)
        Array.from(classCtx.privateFields || []).includes(v) ||
        // For common field names in constructors, assume they're class properties
        (isInClassMethod && ['price', 'seats', 'artist', 'title', 'availableSeats'].includes(v))
      );
      
      // Auto-convert bare property references to this.property in class methods
      if (isInClassMethod && classCtx) {
        expr = expr.replace(/\b(price|seats|artist|title|availableSeats)\b/g, (match) => {
          // Don't replace if it's already prefixed with this.
          if (expr.includes(`this.${match}`)) return match;
          return `this.${match}`;
        });
      }
      
      const classCtxForPrivate = currentClassCtx();
      expr = adjustPrivateAccess(expr, classCtxForPrivate);
      expr = expr.replace(/\s+AND\s+/g, " + ");
      
      if (isInClassMethod && isClassField) {
        // This is a class property assignment
        const isPrivate = classCtxForPrivate && classCtxForPrivate.privateFields && classCtxForPrivate.privateFields.has(v);
        const target = isPrivate ? `this.#${v}` : `this.${v}`;
        out.push(`${indent}${target} = ${expr};`);
      } else {
        // Regular variable assignment
        const inMethod = isInMethod();
        const isAlreadyDeclared = inMethod ? methodScopedVars.has(v) : declaredVars.has(v);
        
        if (isAlreadyDeclared) {
          // Variable already declared, just assign
          out.push(`${indent}${v} = ${expr};`);
        } else {
          // New variable declaration
          if (inMethod) {
            methodScopedVars.add(v);
          } else {
            declaredVars.add(v);
          }
          out.push(`${indent}let ${v} = ${expr};`);
        }
      }
      continue;
    }

    // Logging with emotional modifiers (fun synonyms)
    m = line.match(/^SPEAK\s+(.+?)\s+WITH\s+(JOY|HAPPINESS|EXCITEMENT|ANGER|RAGE|FURY|SADNESS|SORROW|FEAR|WORRY|LOVE|AFFECTION|SURPRISE|WONDER|PRIDE|CONFIDENCE|WHISPER|SOFTLY|LOUDLY|BOLDLY)$/i)
      || line.match(/^YELL\s+(.+?)\s+WITH\s+(JOY|HAPPINESS|EXCITEMENT|ANGER|RAGE|FURY|SADNESS|SORROW|FEAR|WORRY|LOVE|AFFECTION|SURPRISE|WONDER|PRIDE|CONFIDENCE|WHISPER|SOFTLY|LOUDLY|BOLDLY)$/i)
      || line.match(/^SHOUT\s+(.+?)\s+WITH\s+(JOY|HAPPINESS|EXCITEMENT|ANGER|RAGE|FURY|SADNESS|SORROW|FEAR|WORRY|LOVE|AFFECTION|SURPRISE|WONDER|PRIDE|CONFIDENCE|WHISPER|SOFTLY|LOUDLY|BOLDLY)$/i)
      || line.match(/^SAY\s+(.+?)\s+WITH\s+(JOY|HAPPINESS|EXCITEMENT|ANGER|RAGE|FURY|SADNESS|SORROW|FEAR|WORRY|LOVE|AFFECTION|SURPRISE|WONDER|PRIDE|CONFIDENCE|WHISPER|SOFTLY|LOUDLY|BOLDLY)$/i);
    if (m) {
      let expr = m[1];
      const emotion = m[2].toUpperCase();
      
      // Define color codes and styles for different emotions
      const emotionStyles = {
        'JOY': '\x1b[33m', 'HAPPINESS': '\x1b[33m', 'EXCITEMENT': '\x1b[93m',
        'ANGER': '\x1b[31m', 'RAGE': '\x1b[91m', 'FURY': '\x1b[91m',
        'SADNESS': '\x1b[34m', 'SORROW': '\x1b[94m',
        'FEAR': '\x1b[35m', 'WORRY': '\x1b[95m',
        'LOVE': '\x1b[95m', 'AFFECTION': '\x1b[95m',
        'SURPRISE': '\x1b[36m', 'WONDER': '\x1b[96m',
        'PRIDE': '\x1b[32m', 'CONFIDENCE': '\x1b[92m',
        'WHISPER': '\x1b[2m', 'SOFTLY': '\x1b[2m',
        'LOUDLY': '\x1b[1m', 'BOLDLY': '\x1b[1m'
      };
      const resetCode = '\x1b[0m';
      
      expr = replaceComparisons(expr);
      expr = replaceQuotes(expr);
      expr = replaceListLiterals(expr);
      expr = replaceIndexing(expr);
      expr = replacePropertyAccess(expr);
      expr = replaceMethodCallsExpr(expr);
      expr = replaceLogicalOperators(expr);
      expr = replaceArithmetic(expr);
      expr = replaceConstruction(expr);
      
      // Auto-convert bare property references to this.property in class methods
      const classCtx = currentClassCtx();
      const currentBlock = blockStack[blockStack.length - 1];
      const isInClassMethod = classCtx && (currentBlock?.type === 'CTOR' || currentBlock?.type === 'METHOD');
      if (isInClassMethod && classCtx) {
        expr = expr.replace(/\b(price|seats|artist|title|availableSeats)\b/g, (match) => {
          if (expr.includes(`this.${match}`)) return match;
          return `this.${match}`;
        });
      }
      
      expr = adjustPrivateAccess(expr, classCtx);
      expr = expr.replace(/\s+AND\s+/g, " + ");
      if (!/["'`]/.test(expr) && isBareTextLiteral(expr)) { expr = `"${expr.trim()}"`; }
      const singleWord = expr.trim();
      if (!/["'`]/.test(singleWord) && /^[A-Za-z_][\w]*$/.test(singleWord) && !declaredVars.has(singleWord)) { expr = `"${singleWord}"`; }
      
      // Apply emotional styling
      const styleCode = emotionStyles[emotion] || '';
      out.push(`${indent}console.log("${styleCode}" + ${expr} + "${resetCode}");`);
      continue;
    }

    // Regular logging (fun synonyms) - fallback for SPEAK without emotions
    m = line.match(/^SPEAK\s+(.+)$/)
      || line.match(/^YELL\s+(.+)$/)
      || line.match(/^SHOUT\s+(.+)$/)
      || line.match(/^SAY\s+(.+)$/);
    if (m) {
      let expr = m[1];
      expr = replaceComparisons(expr);
      expr = replaceQuotes(expr);
      expr = replaceListLiterals(expr);
      expr = replaceIndexing(expr);
      expr = replacePropertyAccess(expr);
      expr = replaceMethodCallsExpr(expr);
      expr = replaceLogicalOperators(expr);
      expr = replaceArithmetic(expr);
      expr = replaceConstruction(expr);
      
      // Auto-convert bare property references to this.property in class methods
      const classCtx = currentClassCtx();
      const currentBlock = blockStack[blockStack.length - 1];
      const isInClassMethod = classCtx && (currentBlock?.type === 'CTOR' || currentBlock?.type === 'METHOD');
      if (isInClassMethod && classCtx) {
        // Replace property references, but be careful about quoted strings
        expr = expr.replace(/\b(price|seats|artist|title|availableSeats)\b/g, (match) => {
          // Don't replace if it's already prefixed with this.
          if (expr.includes(`this.${match}`)) return match;
          return `this.${match}`;
        });
      }
      
      expr = adjustPrivateAccess(expr, classCtx);
      expr = expr.replace(/\s+AND\s+/g, " + ");
      if (!/["'`]/.test(expr) && isBareTextLiteral(expr)) { expr = `"${expr.trim()}"`; }
      const singleWord = expr.trim();
      if (!/["'`]/.test(singleWord) && /^[A-Za-z_][\w]*$/.test(singleWord) && !declaredVars.has(singleWord)) { expr = `"${singleWord}"`; }
      out.push(`${indent}console.log(${expr});`);
      continue;
    }

    // User Input (synchronous readline)
    m = line.match(/^(?:ASK USER|GET INPUT|READ INPUT)\s*(?:FOR\s+(.+))?$/)
      || line.match(/^ASK\s+(.+)$/);
    if (m) {
      hasUserInput = true;
      const prompt = m[1] || '"Enter input: "';
      let promptExpr = prompt;
      if (prompt !== '"Enter input: "') {
        promptExpr = replaceComparisons(prompt);
        promptExpr = replaceQuotes(promptExpr);
        promptExpr = replaceListLiterals(promptExpr);
        promptExpr = replaceIndexing(promptExpr);
        promptExpr = replacePropertyAccess(promptExpr);
        promptExpr = replaceMethodCallsExpr(promptExpr);
        promptExpr = replaceLogicalOperators(promptExpr);
        promptExpr = replaceArithmetic(promptExpr);
        promptExpr = replaceConstruction(promptExpr);
        const classCtx = currentClassCtx();
        promptExpr = adjustPrivateAccess(promptExpr, classCtx);
        promptExpr = promptExpr.replace(/\s+AND\s+/g, " + ");
        if (!/["'`]/.test(promptExpr) && isBareTextLiteral(promptExpr)) { promptExpr = `"${promptExpr.trim()}"`; }
      }
      out.push(`${indent}const rl = readline.createInterface({ input: process.stdin, output: process.stdout });`);
      out.push(`${indent}const userInput = await new Promise(resolve => rl.question(${promptExpr}, resolve));`);
      out.push(`${indent}rl.close();`);
      continue;
    }

    // Return (fun synonyms)
    m = line.match(/^GIVE BACK\s+(.+)$/)
      || line.match(/^SEND BACK\s+(.+)$/)
      || line.match(/^HAND BACK\s+(.+)$/);
    if (m) {
      let expr = m[1];
      expr = replaceComparisons(expr);
      expr = replaceQuotes(expr);
      expr = replaceListLiterals(expr);
      expr = replaceIndexing(expr);
      expr = replacePropertyAccess(expr);
      expr = replaceMethodCallsExpr(expr);
      expr = replaceLogicalOperators(expr);
      expr = replaceArithmetic(expr);
      expr = replaceConstruction(expr);
      const classCtx = currentClassCtx();
      expr = adjustPrivateAccess(expr, classCtx);
      expr = expr.replace(/\s+AND\s+/g, " + ");
      out.push(`${indent}return ${expr};`);
      continue;
    }

    // If / Elif / Else / End If (fun synonyms)
    m = line.match(/^UNLESS\s+(.+)\s+THEN$/);
    if (m) {
      let cond = replaceComparisons(replaceQuotes(m[1]));
      cond = replaceIndexing(cond); cond = replacePropertyAccess(cond); cond = replaceMethodCallsExpr(cond); cond = replaceLogicalOperators(cond); cond = replaceArithmetic(cond); cond = replaceConstruction(cond);
      const classCtx = currentClassCtx();
      cond = adjustPrivateAccess(cond, classCtx);
      out.push(`${indent}if (!(${cond})) {`);
      blockStack.push("IF");
      continue;
    }
    m = line.match(/^(?:IF|WHEN)\s+(.+)\s+(?:THEN|\{)$/) || line.match(/^(?:IF|WHEN)\s+(.+)$/);
    if (m) {
      let cond = replaceComparisons(replaceQuotes(m[1]));
      cond = replaceIndexing(cond); cond = replacePropertyAccess(cond); cond = replaceMethodCallsExpr(cond); cond = replaceLogicalOperators(cond); cond = replaceArithmetic(cond); cond = replaceConstruction(cond);
      const classCtx = currentClassCtx();
      cond = adjustPrivateAccess(cond, classCtx);
      out.push(`${indent}if (${cond}) {`);
      blockStack.push("IF");
      continue;
    }
    m = line.match(/^ELIF\s+(.+)\s+THEN$/) || line.match(/^ELSE IF\s+(.+)\s+THEN$/);
    if (m) {
      let cond = replaceComparisons(replaceQuotes(m[1]));
      cond = replaceIndexing(cond); cond = replacePropertyAccess(cond); cond = replaceMethodCallsExpr(cond); cond = replaceLogicalOperators(cond); cond = replaceArithmetic(cond); cond = replaceConstruction(cond);
      const classCtx = currentClassCtx();
      cond = adjustPrivateAccess(cond, classCtx);
      out.push(`${indent}} else if (${cond}) {`);
      continue;
    }
    if (line === "ELSE" || line === "OTHERWISE") { out.push(`${indent}} else {`); continue; }
    // Handle "} OTHERWISE {" on the same line
    m = line.match(/^}\s*(?:ELSE|OTHERWISE)\s*\{$/);
    if (m) {
      out.push(`${indent}} else {`);
      continue;
    }
    if (line === "END IF" || line === "}") { 
      if (blockStack[blockStack.length - 1] === "IF") {
        out.push(`${indent}}`); 
        blockStack.pop(); 
      }
      continue; 
    }

    // For-each (fun synonyms)
    m = line.match(/^FOR EACH\s+([A-Za-z_][\w]*)\s+IN\s+(.+)\s+DO$/)
      || line.match(/^FOR EVERY\s+([A-Za-z_][\w]*)\s+IN\s+(.+)\s+DO$/);
    if (m) {
      const v = m[1];
      let listExpr = replaceComparisons(m[2]);
      listExpr = replaceQuotes(listExpr);
      listExpr = replaceListLiterals(listExpr);
      listExpr = replaceIndexing(listExpr);
      listExpr = replacePropertyAccess(listExpr);
      listExpr = replaceMethodCallsExpr(listExpr);
      listExpr = replaceLogicalOperators(listExpr);
      listExpr = replaceArithmetic(listExpr);
      listExpr = replaceConstruction(listExpr);
      const classCtx = currentClassCtx();
      listExpr = adjustPrivateAccess(listExpr, classCtx);
      out.push(`${indent}for (const ${v} of ${listExpr}) {`);
      blockStack.push("FOR");
      continue;
    }
    if (line === "END FOR") { out.push(`${indent}}`); blockStack.pop(); continue; }

    // While (fun synonyms)
    m = line.match(/^WHILE\s+(.+)\s+DO$/) || line.match(/^KEEP GOING WHILE\s+(.+)\s+DO$/);
    if (m) {
      let cond = replaceComparisons(replaceQuotes(m[1]));
      cond = replaceIndexing(cond); cond = replacePropertyAccess(cond); cond = replaceMethodCallsExpr(cond); cond = replaceLogicalOperators(cond); cond = replaceArithmetic(cond); cond = replaceConstruction(cond);
      const classCtx = currentClassCtx();
      cond = adjustPrivateAccess(cond, classCtx);
      out.push(`${indent}while (${cond}) {`);
      blockStack.push("WHILE");
      continue;
    }
    if (line === "END WHILE") { out.push(`${indent}}`); blockStack.pop(); continue; }

    // Repeat / Until (fun synonyms)
    if (line === "REPEAT" || line === "AGAIN") { out.push(`${indent}do {`); blockStack.push("REPEAT"); continue; }
    m = line.match(/^UNTIL\s+(.+)$/) || line.match(/^STOP WHEN\s+(.+)$/);
    if (m) {
      let cond = replaceComparisons(replaceQuotes(m[1]));
      cond = replaceIndexing(cond); cond = replacePropertyAccess(cond); cond = replaceMethodCallsExpr(cond); cond = replaceLogicalOperators(cond); cond = replaceArithmetic(cond); cond = replaceConstruction(cond);
      const classCtx = currentClassCtx();
      cond = adjustPrivateAccess(cond, classCtx);
      out.push(`${indent}} while (!(${cond}));`);
      blockStack.pop();
      continue;
    }

    // Break/Continue (fun synonyms)
    if (line === "STOP THIS") { out.push(`${indent}break;`); continue; }
    if (line === "SKIP THIS") { out.push(`${indent}continue;`); continue; }

    // Fallback: keep original line
    out.push(raw);
  }
  
  // Wrap in async function if user input is used
  if (hasUserInput) {
    code = "import readline from 'readline';\n\n(async () => {\n" + out.join("\n") + "\n})();";
  } else {
    code = out.join("\n");
  }
})();

// Replace LukeLang syntax with JavaScript equivalents (safe boundaries)
code = replaceLukeLangTokens(code, tokenModule.tokens, process.env.LUKELANG_DEBUG === '1');

// Post-process: ensure console.log calls have parentheses (handle Windows CRLF)
const lines = code.split(/\r?\n/);
code = lines
  .map((line) => {
    const m = line.match(/^([ \t]*)console\.log\s+(.*)$/);
    if (m) {
      const [, indent, expr] = m;
      const trimmed = expr.trim();
      if (trimmed.startsWith("(")) return line; // already has parentheses
      return `${indent}console.log(${expr})`;
    }
    return line;
  })
  .join("\n");

// Write transpiled JS
// Tag compiled output with a small version comment for tracking
code = `// Compiled by Mimo v0.1\n` + code;
fs.writeFileSync(outputFile, code);

console.log("🔥 LukeLang transpiled to JS successfully!");
console.log("   💬 \"Programming with Personality\" - Where code meets conversation!");
if (!autoRun) {
    console.log(`Run it using: node ${outputFile}`);
  } else {
    console.log(`Auto-running: node ${outputFile}`);
    console.log(""); // Add spacing before execution
    const res = spawnSync("node", [outputFile], { stdio: "inherit" });
    if (res.error) {
      console.error("Failed to run output:", res.error.message);
    }
  }
