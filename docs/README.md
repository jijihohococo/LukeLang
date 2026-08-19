# LukeLang Documentation

Welcome to the official documentation for LukeLang — a conversational language with a **Build-first** native/WASM runtime in [`vm/`](../vm/).

> **Start here for direction:** [`STRATEGY.md`](./STRATEGY.md) — identity, the wedge, and the plan.  
> Prefer [`BUILD_MODE.md`](./BUILD_MODE.md) and [`getting_started.md`](./getting_started.md).  
> Removed JS emitters: [`LEGACY.md`](./LEGACY.md).

## Introduction

LukeLang uses verbose, human-readable syntax. The language of record is **Build** (`luke BUILD`): typed layouts, arenas, no GC in shipped binaries. The Play VM (`luke SHOW --vm`) is a compatibility skateboard.

## Table of Contents

0.  **[Strategy](./STRATEGY.md)** — identity, the wedge (reactive full-stack), renderer decision, and the phased plan
1.  **[Getting Started](./getting_started.md)**
2.  **[Language Reference](./language_reference.md)**
3.  **[Standard Library](./standard_library.md)**
4.  **[Advanced Topics](./advanced_topics.md)**
5.  **[Backend Publish Plan](./BACKEND_PUBLISH.md)**
6.  **[Editor Tooling](./EDITOR_TOOLING.md)**
7.  **Engine track**
    *   [Build Mode](./BUILD_MODE.md) — AOT / native / browser path
    *   [INTEGER](./INTEGER.md) — exact int64 rules (overflow, mix, division)
    *   [Frontend Roadmap](./FRONTEND_ROADMAP.md) — Hanka / Argus / publish
    *   [Reactive](./REACTIVE.md) — language understands change
    *   [Live Graph](./LIVE_GRAPH.md) — DB row → pixel (full-stack reactive substrate)
    *   [Reactive Spec v0.1](./REACTIVE_SPEC.md) — normative scheduler contract
    *   [Reactive Roadmap](./REACTIVE_ROADMAP.md) — production milestones
    *   [Argus](./ARGUS.md) — rendering engine (DOM presentment)
    *   [Hanka](./HANKA.md) — layout engine (frames → Argus)
    *   [Production Web](./PRODUCTION_WEB.md) — forms, routes, deploy stack
    *   [Legacy (removed)](./LEGACY.md) — former `main.js` / `mimo/` history
8.  **[Contributor Guide](./contributor_guide.md)**
