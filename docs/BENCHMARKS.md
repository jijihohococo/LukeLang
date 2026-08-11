# LukeLang Reactive Benchmarks

> **Status:** Phase 2 baseline (Track 12) — method-aware  
> Re-run locally; quote **median / min**, not a single sample.

## What we measure

Among **N** mounted Argus text nodes:

| Path | Work |
| --- | --- |
| **Granular** | Warmup ×5, then `UPDATE` one node ×**100** → median / min ms. Assert region Δ == 100. |
| **Full rebuild** | `CLEAR` + recreate N + `PAINT` ×**20** → median / min ms. |

Program: [`examples/build/reactive_benchmark.luke`](../examples/build/reactive_benchmark.luke)  
Harness: `RESET THE BENCH` / `RECORD BENCH SAMPLE` / `THE BENCH MEDIAN` / `THE BENCH MIN`

## Production fixes behind these numbers

1. **O(1) id lookup** — Argus `id → node` open-addressed hash (mount was O(N²) via linear `argus_find` + `strlen`).
2. **Stable arena growth** — bump allocator **chains blocks** instead of `realloc` (old pointers stay valid; default start remains **1 MiB**).
3. **Owned ids** — node id/parent strings are malloc-backed and freed on `CLEAR` (not `char id[64]` in the node).

## Baseline (native Build, this host)

| Nodes | build_ms | granular median (min) | full median (min) | region Δ |
| ---: | ---: | ---: | ---: | ---: |
| 1 000 | ~0.3 | ~0.001 (~0.001) | ~0.14 (~0.14) | 100 |
| 10 000 | ~2.7 | ~0.009 (~0.008) | ~1.6 (~1.6) | 100 |

Headline: **one-cell updates stay ~µs**; mount/rebuild is ~linear after the index fix (10K build ~3 ms, not ~370 ms).

## Reference app

[`examples/build/dashboard_{server,client}.luke`](../examples/build/dashboard_client.luke) — live `/reqs` on Path A flex; each poll prints `region=1`.

## Re-run

```bash
cd vm
./build/luke BUILD ../examples/build/reactive_benchmark.luke -o build/reactive_benchmark
./build/reactive_benchmark
```

Also covered by `make test` (`benchmark_ok=1`, `region=100`, sample counts).

## Pressure suite (100K / concurrent)

Separate from the CI lap:

```bash
cd vm && make pressure
```

| Probe | Load | Result (this host) |
| --- | ---: | --- |
| Reactive granular | **100 000** nodes, 50 updates | median ~0.35 ms, region Δ == 50 |
| Reactive mount | 100 000 nodes | build ~38 ms; full rebuild median ~21 ms |
| Frontend mount | **2 500** boxes | ~0.7 ms |
| Concurrent HTTP | 32× `/fast` + 8× `/slow` overlapping | all replies correct |

Programs: `examples/build/reactive_pressure.luke`, `frontend_pressure.luke`, `concurrent_pressure.luke`.
