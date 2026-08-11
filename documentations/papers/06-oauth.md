# OAuth and Authentication Flows as Compiler Verified State Machines

A technical paper on modeling delegated authorization and multi step authentication as flows the compiler can prove safe.

## Abstract

Delegated authorization through OAuth, two factor verification, and password reset are the authentication features where mistakes are both easy to make and severe in consequence. They are multi step processes with security critical ordering, and the classic vulnerabilities in them, a missing state check, a replayed step, an unverified completion, an open redirect, a leaked token, are all failures of that ordering or of the handling of sensitive values along the way. LukeLang models these processes as declarative flows, and because a flow is a declaration rather than a set of imperative branches, the compiler can prove properties of it. In particular the compiler rejects a flow that can reach completion without passing a required verification step. This paper describes the flow model, its application to OAuth and to two factor and reset flows, the way it composes with the protected data and audit machinery described in the authentication paper, and its honest status. The OAuth flow rejection described here is implemented and exercised by a test in continuous integration.

## 1. Motivation

The flows that grant access are the flows that attackers study most closely. OAuth in particular has a catalog of well known vulnerabilities that arise from getting the sequence of steps wrong. If the authorization response is accepted without checking the state value that ties it to the request that started the flow, an attacker can inject their own authorization and complete the flow in a victim's session. If the callback can be reached without the redirect that precedes it, the flow can be entered in an unexpected state. If the token exchanged at the end of the flow is written to a log, it becomes a credential lying in plaintext. Two factor and reset flows have their own versions of the same problem, chiefly the replay of a step that should be usable only once, and the completion of a flow without the verification the flow exists to enforce.

The common thread is that these are ordering and handling failures in a state machine. A state machine expressed as imperative code is hard to reason about, because the states and transitions are implicit in the control flow. A state machine expressed as a declaration can be checked, because the states and transitions are explicit. LukeLang expresses authentication processes as declared flows so that the compiler can verify their safety properties.

## 2. The Flow Model

### 2.1 Flows as declarations

A flow is a declared sequence of steps with a defined completion. A sign up flow collects credentials, verifies an email through a code, and then completes by creating an account. A login flow authenticates and completes. Each step is named, and the completion is a distinguished state. Because the flow is declared, the compiler has the full graph of states and transitions and can reason about which states can reach completion and what must be true along the way.

### 2.2 The core guarantee

The core guarantee of the flow model is that an impossible authentication state does not compile. A flow that can reach its completion without passing a required verification step is rejected by the compiler, which reports the impossible state by name. This is the same guarantee described for sign up in the authentication paper, generalized to any flow. The value of the guarantee is that the most dangerous property of an authentication flow, whether it can be completed without the verification it exists to enforce, is checked at build time rather than discovered in testing or in production.

### 2.3 Composition with protected data

Flows compose with the protected data model. A token or secret handled inside a flow carries its protection, so the compiler's rules about protected data apply within the flow. A token cannot be exposed on an unscoped path, and it can only be deliberately released through the single audited declassification operation described in the authentication paper. The flow model and the protected data model are two applications of the same compiler knowledge, one over control flow and one over data flow, and they reinforce each other within an authentication process.

## 3. OAuth as a Verified Flow

### 3.1 The steps

An OAuth login is a flow with a characteristic shape. It begins with a redirect to the authorization server. It returns through a callback that carries an authorization response. It verifies that response, including the state value that ties the callback to the request that began the flow. It exchanges the authorization for a token. It completes.

### 3.2 The rejection

The compiler applies the flow guarantee to this shape. An OAuth flow that reaches completion without the OAuth verification step does not compile. The compiler rejects it and reports that completion of the OAuth flow requires the verification step. The classic OAuth failure of accepting the authorization response without the verification that protects against injection is therefore a compile error rather than a latent vulnerability. This rejection is implemented and is asserted by a continuous integration test that builds a deliberately incorrect OAuth flow and confirms that the build fails, alongside a correct OAuth flow that builds and runs.

### 3.3 The direction of the work

The current implementation proves the central property, that the verification step is mandatory for completion. The full modeling of OAuth carries further properties that are the direction of the work: that the state value is not only present but checked, that the redirect target is constrained so that the flow cannot be turned into an open redirect, and that the exchanged token is treated as protected for its whole lifetime so that it cannot be logged or exposed. Each of these is an instance of a property the compiler is positioned to check, because the flow and the data are both visible to it, and each is identified honestly as work beyond the current spike.

## 4. Two Factor and Reset Flows

The same model applies to two factor verification and to password reset, and it gains additional strength from the reactive and historical machinery of the platform.

A reset or a second factor is a step that should be usable exactly once. Because the state of a flow is carried in the reactive graph and the causal history records the consumption of steps, a step that has been consumed is known to have been consumed, and its reuse can be prevented. This turns the prevention of replay from a matter of careful token bookkeeping into a property of the recorded history. A step that the history shows was already used cannot be used again.

A reset flow also benefits from resumability. Because flow state can be carried across the wire through the Live Graph and resumed after a disconnection, a flow can be expired and resumed in a controlled way rather than depending on ad hoc token lifetimes. The combination of a declared flow, a recorded history, and a resumable wire gives these sensitive flows a foundation that is stronger than the usual assembly of single use tokens and expiry timestamps.

## 5. Step Up Authentication

A further property that the flow model makes natural is step up authentication, the requirement that a sensitive action be preceded by a fresh verification. Because a flow's verification is a recorded event, an action can require that a recent verification exists in its context. The requirement is expressed as a condition the compiler and runtime can check, so a sensitive action that is not preceded by the required fresh verification is prevented. This places re authentication for high value operations under the same discipline as the rest of the flow model rather than leaving it to manual checks scattered through the code.

## 6. Status and Limitations

The following is implemented and exercised by tests. Authentication processes are expressed as declared flows. The core guarantee holds, that a flow which reaches completion without a required verification step fails to compile. OAuth is modeled as a flow, and an OAuth flow that reaches completion without its verification step fails to compile, while a correct OAuth flow builds and runs, both asserted in continuous integration.

The boundaries are as follows. The current OAuth modeling proves the mandatory verification property. The further OAuth properties, that the state value is checked, that the redirect target is constrained against open redirects, and that the token is protected across its whole lifetime, are the direction of the work rather than complete guarantees today. The replay prevention through recorded consumption, the resumable expiry of reset flows, and step up authentication are described here as the design that the flow model, the causal history, and the Live Graph make possible, and their full implementation is future work. As with the rest of the authentication system, the honest description is that the central compiler enforced property is running and tested, and the complete, production ready OAuth, two factor, and reset flows are being built on top of it. A security system must not overstate its guarantees, so the line between the proven property and the intended surface is drawn plainly.

## 7. Related Work

The specification of OAuth and its security considerations document the vulnerabilities this paper addresses, and the practice of modeling authentication protocols as state machines is established in the security literature, where such models are used to find exactly the ordering flaws described here. Session types and typestate systems in programming languages provide the general technique of encoding the permitted sequence of operations in a type so that an illegal sequence is a type error. The contribution of this paper is to bring that technique to bear on real authentication flows in an application language, so that the ordering property that matters most, that a flow cannot complete without its verification, is checked by the same compiler that checks the rest of the program, and to connect it to the protected data model, the causal history, and the reactive wire so that replay prevention, token protection, and resumability follow from machinery the language already has.

## 8. Conclusion

OAuth, two factor verification, and password reset are the authentication flows where ordering mistakes are most costly. LukeLang models them as declared flows and proves the property that matters most, that a flow cannot reach completion without its required verification, as a compile error. OAuth is modeled this way today, with the mandatory verification enforced and tested. The deeper OAuth properties, and the replay prevention, resumability, and step up authentication that the recorded history and the reactive wire make possible, are the direction of the work. The principle is consistent with the rest of the language. An unsafe sequence of authentication steps is not a bug to be found later. It is a program the compiler declines to build.
