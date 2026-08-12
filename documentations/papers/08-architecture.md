# LukeLang Architecture: One Front End, Many Backends

A technical paper on the structural spine of the compiler: the shared program tree that every tool reads, and the layering that carries a value from a database row to a screen pixel.

## Abstract

The architecture of LukeLang rests on a single decision: there is one front end, and everything that consumes a program consumes the same tree it produces. A parser turns source into a program tree of statements and expressions. Build, the intermediate representation dump, the formatter, the language server, and the debug adapter all read that one tree. This paper describes the front end and the shared tree, the tools that read it, the compile time type discipline, the module and package structure, and the end to end layering that connects the database to the screen. It states honestly where the tree is fully specialized and where parts of the surface are still carried as opaque statements pending specialization.

## 1. The Central Decision

A language accumulates tools. It needs a compiler, and then it needs a formatter, and then an editor integration, and then a debugger. The tempting way to build these is one at a time, each with its own understanding of the syntax. The result is drift: the formatter parses a construct the compiler accepts and reformats it wrongly, or the editor highlights a shape the compiler has since changed. The way LukeLang avoids this is to have a single front end and to make every tool a reader of its output. There is one place that understands what a program is, and every other tool asks that place rather than re deriving the answer.

## 2. The Front End

### 2.1 Parsing to a program tree

The parser turns source into a program tree. The tree is a sequence of statements, and statements nest: a conditional contains statements, a loop contains statements, a function contains statements, and a reactive handler contains statements. Statements that carry expressions hold them as expression nodes rather than as text. The tree is the authoritative representation of the program's structure.

### 2.2 Expressions by precedence

Expressions are parsed by a precedence climbing parser, so that operator precedence and associativity are decided in one place and are the same for every tool that evaluates or renders an expression. An expression is a node with typed structure, not a string that each back end re parses.

### 2.3 Flattening for emission

Before emission the nested tree is flattened into an ordered statement stream with the nesting preserved as structure, so that a single pass can walk it and emit. This flattening is the join between the structured front end and the linear back ends: the tree is convenient to analyze, and the flattened stream is convenient to emit from.

## 3. The Tools That Read the Tree

Because the tree is shared, the tools are thin. Build walks the flattened stream and emits C. The intermediate representation dump prints the same stream so that the compiler's understanding of a program is inspectable. The formatter renders the tree back to canonical source, which is why formatting is a structural operation and not a text transformation: the formatter cannot reformat a construct wrongly because it renders from the same tree the compiler compiled. The language server answers editor queries — an outline, a definition, references, a rename, a signature, a hover, semantic tokens, and code actions — from the tree, so its answers agree with the compiler by construction. The debug adapter maps a running binary back to program lines through the line directives the compiler emitted. Every one of these is a reader; none of them owns a second understanding of the language.

## 4. Type Discipline at Compile Time

Types are resolved during emission rather than carried as runtime tags in Build mode. Arithmetic distinguishes exact integers from other numbers, text is a first class kind, and a mismatch is a compile error rather than a runtime surprise. This is what lets the arena model work without a collector: values have known kinds and known scopes, so the compiler knows where they live and when their region is released. The type discipline and the memory model are two views of the same property, that a Build mode program is fully understood at compile time.

## 5. Modules and Packages

A program imports standard modules and third party packages by name. The standard library is a set of modules that the front end resolves and includes. Packages are resolved through a lock file that pins versions and verifies content by hash on install, so that a build is reproducible and an installed package is the one that was intended. The package surface is deliberately small at this stage; the mechanism is present and the ecosystem is future work, and the architecture paper does not pretend otherwise.

## 6. The End to End Layering

The architecture that matters most to a user is not internal to the compiler; it is the path a value takes from storage to the screen. LukeLang arranges this as a layered substrate.

```
database row  ->  server cell  ->  wire  ->  client cell  ->  pixel
```

A database row is the source of truth. A server cell is a value maintained over that row by the differential ledger described in the execution paper. The wire carries changes to the client as they happen, over a resumable channel. A client cell is the reactive value the interface binds to, maintained by the reactive engine. A pixel is the surgical paint that the frontend engines apply when the client cell changes. Each arrow is a described system with its own paper. The architectural claim is that these are one continuous dataflow rather than four subsystems bolted together: a change to a row propagates through the layers to a single repainted node without any layer polling the one before it.

## 7. Why the Structure Holds

The structure holds because every layer is generated from, or reads, the same front end. The server cell's maintenance is emitted from the query in the program tree. The client binding is emitted from the same tree. The wire subscription is declared in the program and compiled alongside. There is no configuration format that can disagree with the code and no second schema that can drift. What the compiler can see, the compiler generates consistently, and the layers are consistent because they came from one source.

## 8. Status and Limitations

The front end produces a program tree with nested control flow, functions, and reactive handlers, and precedence parsed expressions, and every tool named above reads it. The honest limitation is specialization coverage. Parts of the surface — some of the backend declaration forms among them — are still carried in the tree as opaque statements that Build understands but that are not yet fully specialized nodes. This does not compromise correctness, because those statements compile correctly; it means the tree is not yet uniformly specialized, and the remaining work is to convert opaque statements into typed nodes without introducing a second intermediate representation. The discipline the project holds to here is explicit: specialize within the one tree, never grow a parallel one.

## 9. Related Work

Compilers have long shared a single intermediate representation across passes; language servers built on the compiler's own front end are the modern best practice precisely because they avoid the drift of a second parser. LukeLang applies this uniformly, extending the shared front end past the compiler and the server to the formatter and the debugger, and extending the same principle of a single source of truth outward from the compiler to the full stack layering, so that the runtime layers of the application are as consistent with each other as the compiler's passes are.

## 10. Conclusion

LukeLang has one front end. A parser produces a program tree, and Build, the representation dump, the formatter, the language server, and the debug adapter all read it, so they cannot disagree about what a program is. The same principle carries outward into the running system: the database row, the server cell, the wire, the client cell, and the pixel form one dataflow, each layer generated from the same tree. The remaining architectural work is to specialize the last opaque statements into typed nodes, and the rule that governs it is to keep one tree, not two.
