#ifndef LUKE_REACTIVE_H
#define LUKE_REACTIVE_H

/* Luke Reactive Runtime — architecture stub (Phase 1 not shipped).
 * See docs/REACTIVE.md
 *
 * Doctrine: Lukelang understands change.
 * Cells / derived / effects + scheduler live here eventually.
 * Argus/Hanka consume invalidation; they are not the reactive framework.
 */

#include "luke_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t LukeRxId;

typedef enum LukeRxKind {
  LUKE_RX_CELL = 1,
  LUKE_RX_DERIVED = 2,
  LUKE_RX_EFFECT = 3
} LukeRxKind;

typedef enum LukeRxWave {
  LUKE_RX_WAVE_INVALIDATE = 1,
  LUKE_RX_WAVE_RECOMPUTE = 2,
  LUKE_RX_WAVE_EFFECT = 3,
  LUKE_RX_WAVE_LAYOUT = 4,
  LUKE_RX_WAVE_PAINT = 5
} LukeRxWave;

typedef struct LukeRxNode {
  LukeRxId id;
  LukeRxKind kind;
  int dirty;
  int version;
} LukeRxNode;

typedef struct LukeRxGraph {
  LukeArena *arena;
  LukeRxNode *nodes;
  size_t len;
  size_t cap;
  /* Phase 1: edge tables (deps/subs) + dirty queue land here. */
  int epoch;
} LukeRxGraph;

/* Phase 1 API sketch (unimplemented — link fails if called).
 * luke_rx_cell / luke_rx_derived / luke_rx_write / luke_rx_read / luke_rx_flush
 */

static inline void luke_rx_graph_init(LukeRxGraph *g, LukeArena *a) {
  if (!g) return;
  memset(g, 0, sizeof(*g));
  g->arena = a;
}

#ifdef __cplusplus
}
#endif

#endif /* LUKE_REACTIVE_H */
