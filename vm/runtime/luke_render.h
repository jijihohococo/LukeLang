#ifndef LUKE_RENDER_H
#define LUKE_RENDER_H

/* Compatibility shim — rendering engine is Argus (docs/ARGUS.md). */
#include "argus.h"

typedef ArgusKind LukeRenderKind;
typedef ArgusNode LukeRenderNode;
typedef ArgusTree LukeRenderTree;

#define LUKE_RENDER_BOX ARGUS_BOX
#define LUKE_RENDER_TEXT ARGUS_TEXT
#define LUKE_RENDER_BUTTON ARGUS_BUTTON
#define LUKE_RENDER_IMAGE ARGUS_IMAGE

#define luke_render_paint argus_paint
#define luke_render_place_text argus_place_text
#define luke_render_place_button argus_place_button
#define luke_render_place_image argus_place_image
#define luke_render_place_box argus_place_box

#endif /* LUKE_RENDER_H */
