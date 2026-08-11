# LukeLang Authentication: Secure by Compiler

A technical paper on authentication and authorization as language properties.

## Abstract

Most authentication failures are not the result of a broken cryptographic primitive. They are the result of ordinary mistakes: a password stored without a proper hash, a permission check that was forgotten, user input concatenated into a query. These are runtime faults that surface in production. The LukeLang authentication system treats authentication and authorization as properties of the language rather than as a library the developer must use correctly. Passwords are hashed with a vetted algorithm by default, database access is parameterized so injection is closed, and, most distinctively, a class of authorization mistakes is turned into a compile error. This paper describes the system in three layers: the secure defaults, the compiler enforced properties, and the reactive and time travel capabilities. Each described feature has been built and is exercised by tests in continuous integration. The paper is explicit about which features are complete and which are proven spikes, because overstating the guarantees of a security system is itself a security failure.

## 1. Motivation

Authentication is where small mistakes have large consequences. A single forgotten check can expose every record. A weak password hash can be brute forced offline after a single database leak. A query built by string concatenation can be turned into arbitrary database access by a crafted input. Libraries help, but a library only protects the developer who uses it correctly every time. The premise of the LukeLang authentication system is that the secure path should be the default path, and that the most dangerous mistakes should be caught by the compiler before the program runs.

The system exploits three properties of LukeLang that a library in another language cannot. The compiler knows the path that data takes from the database to the pixel. The Live Graph makes state reactive across the stack. The runtime records a causal history of change. Each of these becomes the basis for an authentication capability that is difficult or impossible to provide as an ordinary library.

## 2. Layer One: Secure Defaults

### 2.1 Password hashing

When an account is created, the password is hashed with Argon2id through the libsodium library. The system never invents its own cryptography. Argon2id is a memory hard password hashing function and the current recommended default for new systems. The stored value is a standard encoded hash that records the algorithm, its version, and its parameters, for example a memory parameter of sixty four mebibytes and a small iteration count. The application code never handles a plaintext password beyond the point of hashing, and the storage layer never holds anything but the hash.

### 2.2 Parameterized database access

Database access is parameterized. User supplied values are bound to query parameters rather than concatenated into query text. A value that contains what would otherwise be a destructive fragment of query syntax is stored and compared as literal data. The classic injection payload that attempts to terminate a statement and drop a table is stored as an ordinary string, and the table is untouched. Injection through the standard data path is closed by construction rather than by developer vigilance.

### 2.3 Sessions, cross site request forgery, and identity

The framework provides session management, cross site request forgery protection, and a first class notion of the current authenticated user. These are the unglamorous foundations of a real authentication system, and they are present at the framework level so that an application does not assemble them from parts.

## 3. Layer Two: Secure by Compiler

This layer is the distinctive contribution. It uses the compiler's knowledge of the data flow graph to turn authorization mistakes into compile errors.

### 3.1 Protected data as a type

A field can be marked as protected. The compiler then tracks how that field flows from the database toward the client. If the program attempts to bind or watch protected data on a path that is not scoped to the current user, the program does not compile. The error states the cause directly, that protected data can only be exposed on a path scoped to the current user, and that unauthorized access is a compile error.

The significance is that the most common serious authorization bug, exposing data that belongs to one user to another because a check was forgotten, becomes impossible to express without the compiler objecting. A library cannot provide this, because a library runs after the program is written and can only check at runtime what the developer remembered to ask it to check. A compiler can refuse to build a program whose data flow is unsafe.

### 3.2 Authentication flows as verified state machines

Login, sign up, two factor verification, and password reset are multi step state machines. The system expresses these as declarative flows. Because a flow is declared rather than assembled from imperative branches, the compiler can verify properties of the flow. In particular it rejects a flow that can reach its completed state without passing a required verification step. Attempting to write a sign up that reaches completion without the verification step produces a compile error that names the impossible state.

This applies the same idea as protected data typing to control flow. An unsafe state of the authentication process is not a bug to be found in testing. It is a program the compiler declines to build.

### 3.3 Declassification as the single audited escape

Sometimes protected data must be deliberately released in a reduced form, for example the last four digits of an identifier. In information flow systems this deliberate release is called declassification, and it is the most delicate operation, because it is the point where protected data is allowed to escape its protection. The system provides a single explicit operation for this, a reveal, which produces the reduced value and records the act in the audit history.

Because reveal is the only sanctioned way for protected data to leave its protection, the question where does protected data get exposed has a finite and reviewable answer: every reveal in the program. This is what makes an information flow discipline practical rather than merely sound. The dangerous operation is named, explicit, and logged.

## 4. Layer Three: Reactive and Time Travel Capabilities

### 4.1 Live revocation

Because a permission can be part of the reactive graph, revoking a user's access is a reactive change. When access is revoked, the data that depended on that access is removed from the user's view without a refresh. The signature feature of the platform, reactive propagation of change, is applied to authorization, so that a change in permission has the same immediate effect as any other change in state.

### 4.2 Reactive rate limiting

A rate limit is expressed declaratively, and the remaining budget is a reactive value. Because it is reactive and can cross the wire through the Live Graph, the client can display the remaining number of attempts and disable an action as the budget is exhausted, with no additional code. The current implementation exposes the remaining budget as a reactive value. Full enforcement across a window and across multiple workers, and the complete limit syntax, are identified as further work.

### 4.3 Rewind to the incident

Because the runtime records a causal history and that history can be hash chained, the audit of who accessed protected data is not a separate log. It is the same history that supports time travel. A query reports who saw a given field and when. A scrub returns to the moment of an access. The hash chain makes the history tamper evident, so the record can be shown to be unaltered. Compliance reporting becomes a query over a structure that already exists.

## 5. The Unified Position

These features are not independent. They follow from the same three properties of the language. The compiler's knowledge of the data flow graph gives protected data typing, verified flows, and the reveal escape. The reactive Live Graph gives live revocation and reactive rate limiting. The recorded causal history gives the rewindable, tamper evident audit. Stated in one sentence: in LukeLang, unauthorized access is a compile error, an impossible authentication state is a compile error, a change in permission revokes data live, protected data leaves only through an audited reveal, and an incident can be rewound. Password hashing is table stakes. These properties are the difference.

## 6. Status and Limitations

Honesty about a security system is a security requirement, so the status of each feature is stated plainly. The following are implemented and exercised by tests in continuous integration: Argon2id password hashing through libsodium, parameterized database access with the injection payload stored as literal data, sessions and cross site request forgery protection and the current user, protected data typing that rejects an unscoped bind or watch at compile time, declarative flows that reject reaching completion without verification, the reveal declassification operation, live revocation, a reactive remaining budget for rate limiting, and a hash chained audit history with query and scrub.

The following are the boundaries of the current work. The protected data analysis catches direct exposure of a protected field. It does not yet propagate a protection label through intermediate values, so a determined author could route protected data through an unlabeled value. Closing this requires full information flow tracking with label propagation and is the deeper version of the compiler enforced guarantee. The declarative flow verification proves a specific required step rule and does not yet model the full complexity of real two factor, reset, and delegated authorization flows. Rate limiting exposes a budget but does not yet provide full windowed and distributed enforcement. The audit history is maintained in memory and its persistence across restarts and its wiring into the time travel developer tooling are open. Password reset, two factor, delegated authorization, and account enumeration hardening beyond the current baseline are open.

The correct way to describe the system in public is therefore as language level authentication primitives that are proven and tested, not as a finished production authentication system. The concept, that authentication and authorization are enforced by the compiler and the runtime rather than left to developer discipline, is running and covered by tests. The surface around that concept is still being completed.

## 7. Related Work

The system draws on several traditions. Framework level secure defaults for password hashing and identity are established practice in mature web frameworks. Information flow control, including protected labels and declassification, comes from a long line of programming language security research. Capability oriented security informs the treatment of dangerous operations as explicit and named. Tamper evident logs through hash chaining are established in distributed systems. The contribution is to bring these together in one language whose compiler knows the whole data flow and whose runtime is reactive and records history, so that authorization is checked where the data flows, permission changes propagate reactively, and the audit is the same structure that enables time travel.

## 8. Conclusion

The LukeLang authentication system moves authentication and authorization from developer discipline into the language. Secure defaults remove the common catastrophic mistakes. Compiler enforced properties turn a forgotten authorization check and an unsafe authentication flow into compile errors. Reactive and time travel capabilities give live revocation, reactive rate limiting, and a rewindable tamper evident audit. The guarantees are stated honestly, with the boundary between what is proven and what remains drawn clearly, because a security system that overstates its guarantees is worse than one that states them plainly. The direction is a single idea taken seriously: security that is checked by the compiler is stronger than security that depends on remembering to check.
