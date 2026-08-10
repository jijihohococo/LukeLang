# LukeLang Reactive Benchmarks

> **Status:** Phase 2 baseline (Track 12 beachhead)  
> **Machine note:** numbers below are from CI/dev hosts; re-run locally with the command at the bottom.

## What we measure

Among **N** mounted Argus text nodes:

| Path | Work |
| --- | --- |
| **Granular** | `UPDATE` one node → `argus_paint_one` (Spike A/B path). Assert `region` delta == 1. |
| **Full rebuild** | `CLEAR THE SCREEN` + recreate all N nodes + `PAINT THE SCREEN` (naive remount). |

Program: [`examples/build/reactive_benchmark.luke`](../examples/build/reactive_benchmark.luke)

## Baseline (native Build)

| Nodes | build_ms | granular_ms | full_ms | region Δ |
| ---: | ---: | ---: | ---: | ---: |
| 1 000 | ~4 | ~0.001 | ~4 | **1** |
| 10 000 | ~370 | ~0.016 | ~380 | **1** |

Headline: **one cell change stays ~O(1) paint work** while a full rebuild scales with N. At 10K nodes the granular path is on the order of **10⁴× faster** than clear+rebuild on this host.

## Reference app

[`examples/build/dashboard_{server,client}.luke`](../examples/build/dashboard_client.luke) — live `/reqs` feed on Path A flex; each poll prints `region=1` / `granular_delta=1`.

## Re-run

```bash
cd vm
./build/luke BUILD ../examples/build/reactive_benchmark.luke -o build/reactive_benchmark
./build/reactive_benchmark
```

Also covered by `make test` (asserts `region=1` and `benchmark_ok=1`).
