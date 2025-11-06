// Comprehensive LukeLang token mappings
// Note: Most structural keywords are handled by the transformer in main.js.
// These mappings are safe post-process replacements for readability and fallbacks.

export const tokens = {
  // Program markers
  "LET'S START": "// LukeLang program starts here",
  "GET OUTTA HERE": "// LukeLang program ends here",

  // Variables
  "MY NAME IS": "let",

  // Printing (all map to console.log; transformer adds parentheses)
  "SPEAK": "console.log",
  "YELL": "console.log",
  "SHOUT": "console.log",
  "SAY": "console.log",

  // Classes (synonyms)
  "THIS IS CLASS": "class",
  "MAKE CLASS": "class",
  "BLUEPRINT": "class",
  "FOLLOWS": "extends",

  // Constructors (synonyms)
  "WHEN BORN": "constructor",
  "BORN": "constructor",
  "MAKE ONE": "constructor",
  "CALL PARENT": "super",
  "CALL SUPER": "super",

  // Interfaces / Contracts (brand)
  "CONTRACT": "",
  "AGREEMENT": "",
  "PACT": "",
  "IMPLEMENTS": "",
  "COMPLIES WITH": "",
  "AGREES TO": "",
  "ADHERES TO": "",
  "MUST": "",
  "REQUIRES": "",
  "EXPECTS": "",

  // Static members (brand keywords)
  "ALWAYS": "",
  "ETERNAL": "",
  "FOREVER": "",

  // Functions (synonyms)
  "THIS IS FUNCTION": "function",
  "MAKE FUNCTION": "function",
  "RECIPE": "function",

  // Object construction (synonyms)
  "NEW": "new",
  "CREATE": "new",
  "MAKE OBJECT": "new",

  // Returns (synonyms)
  "GIVE BACK": "return",
  "SEND BACK": "return",
  "HAND BACK": "return",

  // Self-reference
  "SELF": "this",

  // Structural method call keywords handled by transformer (left empty intentionally)
  "ASK": "",
  "TELL": "",
  "DO": "",
  "CALL": ""
};
