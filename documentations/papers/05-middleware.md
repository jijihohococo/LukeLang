# The LukeLang Request Pipeline: Middleware as Compiler Checked Capabilities

A technical paper on routing and middleware as language properties rather than ordered runtime lists.

## Abstract

The request pipeline of a web backend decides how an incoming request reaches a handler and what cross cutting work runs along the way. In most frameworks this pipeline is an ordered list of middleware functions assembled by hand. That model has two recurring failure modes: a required piece of middleware is forgotten on a route, and the middleware is placed in the wrong order. Both are silent at build time and dangerous at runtime, because the missing or misordered piece is often the one that enforces security. LukeLang treats the request pipeline as a structure the compiler understands. Routes are a declared table rather than a set of imperative branches. Middleware is expressed as capabilities that a handler requires rather than as a list the author must remember to attach. Because the compiler knows the route table, the capability requirements, and the data a handler touches, an entire class of pipeline mistakes becomes a compile error. This paper describes the model, the guarantees it provides, and its honest status. The compile time rejections described here are implemented and exercised by tests in continuous integration.

## 1. Motivation

Consider the ordinary way a backend pipeline goes wrong. A developer adds a new route that returns account data but forgets to attach the authentication middleware, so the route is public. Or the developer attaches a rate limiting middleware before the authentication middleware, so unauthenticated traffic is rate limited by a key that only exists after authentication, and the protection is effectively disabled. Or a template contains a link to a route that was renamed, so the link is a broken path that returns a not found error only when a user clicks it.

Each of these is a small mistake with a large consequence, and each is invisible until the program runs. The reason is that the pipeline is expressed as imperative code and an ordered list. The compiler sees a sequence of function calls, not a structure it can reason about. LukeLang changes what the compiler sees. When the route table, the capability requirements, and the data flow are declarations, the compiler can check them, and the mistakes above become build failures rather than runtime incidents.

## 2. Routes as a Declared Table

### 2.1 The table

Routes in LukeLang are a declared table. Each entry names a method and a path, binds any path parameters with their types, states the capabilities the route requires, and names the handler. Because the routes are data rather than control flow, the compiler can analyze the whole surface of the application at once.

### 2.2 Typed path parameters

A path parameter carries a type. A route whose path contains an identifier declared as an integer requires that the handler treat that parameter as an integer, and any navigation to that route must supply an integer. The type of a route parameter is checked on both ends because the compiler sees both the route declaration and the code that links to it. This removes a common source of runtime errors, the mismatch between the shape of a link and the shape of the route it targets.

### 2.3 Link integrity

Because the route table is known, every link to a route can be checked against it. A link whose target has no matching entry in the table does not compile. The compiler rejects it and reports that the link points at a route that does not exist. The class of bug where an internal link silently becomes a not found error is closed at build time. The application cannot ship a broken internal link, because a broken internal link is a compile error.

### 2.4 Routes and protected data

The route table intersects the authorization model described in the authentication paper. A route that reads protected data must declare that it requires authentication. If a route touches protected data without that declaration, the program does not compile, and the compiler reports that the route reads protected data without requiring authentication. This is the route level expression of the principle that unauthorized access to protected data is a compile error. A public route cannot accidentally expose protected data, because the compiler refuses to build a route that would.

## 3. Middleware as Capabilities

### 3.1 From a list to a requirement

The central idea of the pipeline model is that middleware is not an ordered list the author attaches. It is a set of capabilities a handler requires in its context. A handler that performs a sensitive operation requires an authentication capability and a request forgery protection capability. The handler cannot be built without those capabilities present in its context. The compiler enforces the presence of the required capabilities, so forgetting to protect a handler is not possible in the same way that forgetting to attach middleware is possible in a list based model. A handler that needs authentication and does not have it does not typecheck.

### 3.2 Ordering constraints

Some cross cutting concerns have ordering requirements. Authentication must run before rate limiting keyed on the authenticated user, because the key does not exist until authentication has run. In a list based model the author is responsible for placing the middleware in the correct order, and a wrong order is a silent misconfiguration. In LukeLang the ordering requirement is declared, and the compiler enforces it. A pipeline that places rate limiting before authentication does not compile. The compiler rejects it and reports that authentication must run first. The ordering of the pipeline becomes a checked property rather than a matter of author discipline.

### 3.3 Cross cutting concerns for free

Some cross cutting concerns do not need to be attached at all, because the runtime already provides them. Every request is a node in the causal history described in the Live Graph and authentication papers. Audit and access logging are therefore properties of the history rather than middleware that must be inserted into the pipeline. The concern that is hardest to remember to add, a complete and tamper evident record of access, is the one that requires no addition.

## 4. Form Bodies and Schema

The shipped backend work includes two further pieces that belong to the same principle, that a declaration the compiler understands is safer than imperative code the compiler cannot check.

A form body is declared as a shape with typed fields. The declaration is a single source of truth. It describes the fields the handler receives and their types, so a handler receives typed and validated data rather than an untyped bag of strings that it must parse and check by hand. Because the declaration is one artifact, the client side and the server side cannot drift apart.

A schema is declared as well. The declaration of a table and its fields is the source of truth for the shape of the data, and the compiler can relate the declared schema to the queries and bindings that depend on it. This is the foundation on which safe migrations are built, a direction discussed as future work, where a change to the schema that would break a downstream consumer becomes a compile error because the compiler sees the whole path from the schema to the consumer.

## 5. Guarantees

The pipeline model provides a set of guarantees that hold at compile time. A link to a route that does not exist is a compile error. A route that reads protected data without requiring authentication is a compile error. A handler that requires a capability it does not have does not typecheck. A pipeline whose middleware ordering violates a declared constraint, such as rate limiting before authentication, is a compile error. These guarantees share a single cause. The pipeline is expressed as declarations that the compiler understands, so the compiler can reject a pipeline that is unsafe rather than allowing it to fail at runtime.

## 6. Implementation

The route table, the capability requirements, and the ordering constraints are analyzed during compilation to native code. The bad cases described above are present in the repository as example programs that are expected to fail to compile, and the continuous integration configuration asserts that each of them does fail with the appropriate message. The good cases are example programs that compile and run, and continuous integration asserts their successful output. This means the guarantees are not aspirational. They are checked on every change by building programs that must be rejected and confirming that they are.

## 7. Status and Limitations

The following are implemented and exercised by tests. Routes as a declared table with typed path parameters. Link integrity, where a link to a route with no matching entry fails to compile. Route level protected data checks, where a route that touches protected data without requiring authentication fails to compile. Middleware as required capabilities. Middleware ordering constraints, where an invalid order fails to compile. Form bodies as typed declarations. Schema as a declaration.

The boundaries are as follows. The capability model covers the core cross cutting concerns of authentication, request forgery protection, and rate limiting. A general capability system that lets an application define arbitrary cross cutting requirements and their ordering is a natural extension and is not yet complete. The form and schema declarations are beachheads that establish the single source of truth principle, and the full surface, including rich validation vocabularies and the automatic derivation of migrations from schema changes, is future work. As with the rest of the backend, these are proven language spikes rather than a finished framework, and the correct way to describe them is that the principle is running and tested and the surface around it is being completed.

## 8. Related Work

The onion model of layered middleware is standard in web frameworks and is the model this paper reacts against. Capability based security informs the treatment of middleware as a requirement in a handler's context rather than as an attached list. Typed routing has appeared in several typed web frameworks, where the type of a route parameter is checked against its use. The contribution here is to place routing, middleware, form bodies, and schema under a single principle, that the pipeline is a declaration the compiler checks, and to enforce the security relevant properties of the pipeline, capability presence and ordering and protected data exposure, as compile errors in a language whose compiler already sees the whole path from the request to the data.

## 9. Conclusion

The LukeLang request pipeline replaces an ordered list of middleware with a structure the compiler understands. Routes are a declared table with typed parameters and checked links. Middleware is a set of capabilities a handler requires, with ordering constraints the compiler enforces. Form bodies and schema are declarations that serve as a single source of truth. The result is that the pipeline mistakes that are most dangerous and most easily missed, a forgotten protection, a wrong order, a broken link, an unprotected route over protected data, become build failures. The principle is the same one that runs through the rest of the language. What the compiler can see, the compiler can guarantee.
