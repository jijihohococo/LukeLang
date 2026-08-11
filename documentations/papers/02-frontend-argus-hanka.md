# The LukeLang Frontend Engines: Argus and Hanka

A technical paper on the rendering and layout engines that render LukeLang user interfaces.

## Abstract

LukeLang renders user interfaces through two cooperating engines. Hanka is the layout engine. It owns the structure of a screen and resolves the size and arrangement of elements. Argus is the rendering engine. It owns the scene tree of elements and applies changes to the host surface. This paper describes both engines, the pipeline that connects them, the node model, the accessibility and motion surfaces, and the strategic direction that moves layout and paint responsibility toward the browser through a compilation target the project calls Path A. It also documents the performance work that removed a quadratic cost from element mounting and restored linear scaling, and it states honestly which parts are complete and which are proven prototypes.

## 1. Motivation

A frontend engine has to answer two questions. Where does each element go, and how does the host surface reflect the current state of the interface. In LukeLang these questions are answered by two separate engines so that each has a single responsibility. Hanka answers where. Argus answers how the surface is updated. Separating them keeps the layout mathematics independent of the paint mechanism and lets each evolve on its own.

The engines are driven by the Reactive Engine described in the companion paper. When a reactive value that is bound to a node changes, the change propagates to a paint effect, and the effect asks Argus to update exactly that node. The frontend engines are therefore the visible leaves of the reactive graph. Their job is to turn a reactive change into the smallest possible surface update.

## 2. Pipeline

The rendering pipeline has a fixed shape. A program expresses interface structure using layout terms. Hanka resolves those terms into concrete frames or into layout instructions. Argus holds the resulting scene tree and paints it. A thin embedder applies the paint operations to the host surface, which in the browser is the document.

```
Layout terms  ->  Hanka (layout)  ->  Argus (scene tree, paint)  ->  embedder  ->  host surface
```

The important property of the pipeline is that after the first paint it does not rebuild. When a value changes, the reactive effect calls a single node update on Argus, and Argus patches that node on the surface. There is no rebuild of the surface from a string of markup and no diff of a shadow tree.

## 3. Hanka, the Layout Engine

### 3.1 Responsibility

Hanka owns the arrangement of elements. It supports containers that lay out their children in a column, in a row, or as a stack of overlapping elements. Containers carry padding, a gap between children, an alignment for leftover space along the main axis, and a wrap option that flows children onto the next line or column when the main axis fills.

### 3.2 Surface

The layout surface is conversational. A program opens a container, adds leaves such as text, buttons, images, inputs, selects, tables, and modals, and closes the container. Leaves carry an explicit size, and text leaves can request an automatic width that Hanka measures through the embedder. A single instruction resolves the open containers into the arrangement that Argus will paint.

### 3.3 Two modes

Hanka has a classic mode and a Path A mode. In the classic mode Hanka computes absolute coordinates for each element and feeds those frames to Argus. In the Path A mode, described in section six, Hanka emits layout instructions that the browser resolves natively, and it stops computing absolute coordinates for the common container types. The classic mode is the historical default. The Path A mode is the strategic direction.

## 4. Argus, the Rendering Engine

### 4.1 The scene tree

Argus owns a scene tree of nodes. Each node has an identifier, a kind, a frame, an opacity, optional text or image source, accessibility role and label, and dirty flags. The kinds cover the common interface elements: a box for a colored or empty region, a text label, a button, an image, an input with several input types, a select, a table, and a modal dialog surface. Each kind maps to a concrete element on the host surface.

### 4.2 Surgical paint

The defining feature of Argus is that it paints surgically. When a single node changes, Argus updates that one node on the surface. It does not clear the surface and rebuild it. This is the mechanism that turns the Reactive Engine's constant time update into a constant time visible update. The engine exposes a region paint count that reports how many nodes painted in the most recent flush, so a program can assert that a change painted exactly one node.

### 4.3 Identifier index

Argus locates a node by its identifier on every update. An early implementation scanned the node array linearly on every insert and lookup, which made mounting a scene quadratic in the number of nodes. This was replaced with an open addressed hash index from identifier to node index, with the scene nodes held in a stable allocation so that growth of the underlying arena does not invalidate the index. The result is linear mounting. This change is described further in the performance section because the benchmark that revealed the quadratic cost is the same benchmark that now protects the linear behavior.

### 4.4 Accessibility

Argus applies default accessibility roles when it paints. Interactive nodes receive appropriate roles such as button, textbox, checkbox, radio, image, listbox, table, and dialog, and nodes receive an accessible label derived from placeholder or text where one is present. Full focus trapping and live regions are open work. The position of the engine is that accessibility is a property of paint, applied by default, rather than an attribute the author must remember to add.

### 4.5 Motion

Argus supports immediate opacity changes and a fade operation that animates opacity over a duration with an ease out curve. In the browser the fade runs on the animation frame callback. On native surfaces it steps. Motion is intentionally a small surface at this stage, sufficient for entrance and emphasis rather than a general animation system.

## 5. The Reactive Binding

The frontend engines do not poll. A node is connected to reactive state through a bind. When the bound value changes, the reactive effect calls the corresponding node update on Argus, and only that node is touched. This is why the frontend inherits the constant time update property of the Reactive Engine. The cost of reflecting a state change on the surface is the cost of updating the nodes that depend on that state, which for a typical binding is one node.

## 6. Path A: Compiling to the Browser Layout and Paint

### 6.1 The decision

Owning layout mathematics and paint on top of a document surface means reimplementing work the browser already does well, including text reflow, scrolling, internationalization, and the box model. The project's strategy document records the decision to move this responsibility to the browser. This direction is called Path A. Under Path A the browser lays out and paints, Hanka emits cascading style layout instructions rather than absolute coordinates, and Argus becomes a thin reactive patcher that applies node level updates while the browser owns geometry.

### 6.2 What is proven

The Path A proof of concept is implemented and green. Row and column containers emit flexible box layout instructions to the browser through the embedder, with direction, gap, padding, alignment, and wrap. Children attach to their flexible box parent in normal flow rather than as absolutely positioned elements. Stack containers and explicit placement remain absolute, which is the correct behavior for overlapping and pinned content. Automatic sizing maps to a grow behavior on flow children. Crucially, the reactive property survives the change: a bound value still repaints exactly one node under the new layout mode, verified by the same region paint assertion used elsewhere.

### 6.3 What remains

The full migration of Hanka from computing frames to emitting styles is open work. Three items convert the mechanism into the full benefit. The page root should participate in fluid layout rather than being pinned to a fixed frame. Flow children should size from content and from a grow factor rather than from fixed pixel dimensions, which is where the responsive benefit actually lives. Alignment should be expressible independently along the main axis and the cross axis. Until these land, Path A is a proven mechanism rather than a complete responsive layout system.

## 7. Performance

All numbers are from native builds measured on the development host and are reproduced by the frontend and reactive benchmark programs in the repository.

Frontend mount of a few thousand boxes completes in well under a millisecond, which means interface construction is never the bottleneck for a realistic screen. Reactive mount at the scale of one hundred thousand nodes completes in tens of milliseconds and scales linearly, following the identifier index fix described above. A single bound update paints one node in constant time regardless of scene size. These properties together mean that both building the interface and updating it are cheap, and that neither cost grows in a way that would degrade a large application.

## 8. Status and Limitations

Hanka supports nested containers, alignment, wrap, and automatic measurement, and it drives Argus in both the classic and Path A modes. Argus supports the full set of node kinds, surgical paint, the identifier index, default accessibility roles, and the motion surface. The Path A renderer is a green proof of concept, not the complete default. Full accessibility, including focus trapping and live regions, is open. Independent per axis alignment and content driven fluid sizing under Path A are open. The honest summary is that the frontend engines are fast and correct at what they do today, and that the remaining work is the migration that turns proven mechanisms into a complete responsive system on top of the browser.

## 9. Related Work

Immediate mode interface systems redraw on every frame. Retained mode systems with virtual document trees rebuild and diff a shadow tree. Native application frameworks own their own layout and paint against a canvas surface. Argus and Hanka occupy a deliberate middle position for the web target: they own the scene tree and the reactive patching, and under Path A they delegate geometry and painting to the browser rather than reimplementing them. This keeps the engine small and lets the reactive patcher, which is the genuine contribution, drive a surface the browser already renders well.

## 10. Conclusion

Hanka and Argus turn a reactive change into the smallest possible surface update. Hanka resolves where elements go. Argus applies changes to exactly the nodes that changed. The identifier index makes mounting linear, the reactive binding makes updates constant time, and the accessibility and motion surfaces are applied by default. The strategic direction, Path A, delegates layout and paint to the browser and keeps for LukeLang the part that is genuinely differentiated, the reactive patcher that updates one node when one value changes.
