# The LukeLang Frontend Engines: Argus and Hanka

A technical paper on the rendering and layout engines that render LukeLang user interfaces.

## Abstract

LukeLang renders user interfaces through two cooperating engines. Hanka is the layout engine. It owns the structure of a screen and resolves the arrangement of elements. Argus is the rendering engine. It owns the scene tree of elements and applies changes to the host surface. This paper describes both engines, the pipeline that connects them, the node model, the accessibility and motion surfaces, the responsive model, and the strategic direction that moves layout and paint responsibility to the browser through a compilation target the project calls Path A. Since the previous revision of this paper, Path A has moved from a proven prototype to the working default for the reactive web target: per axis alignment, opt in grid, scroll containers, declarative breakpoints with reflow, focus management on modal open, live regions, and a utility class escape hatch are implemented and wired end to end. The paper states honestly which parts are verified by execution, which are verified by construction, and where the remaining test coverage gap lies.

## 1. Motivation

A frontend engine has to answer two questions. Where does each element go, and how does the host surface reflect the current state of the interface. In LukeLang these questions are answered by two separate engines so that each has a single responsibility. Hanka answers where. Argus answers how the surface is updated. Separating them keeps the layout mathematics independent of the paint mechanism and lets each evolve on its own.

The engines are driven by the Reactive Engine described in the companion paper. When a reactive value bound to a node changes, the change propagates to a paint effect, and the effect asks Argus to update exactly that node. The frontend engines are therefore the visible leaves of the reactive graph. Their job is to turn a reactive change into the smallest possible surface update.

## 2. Pipeline

The rendering pipeline has a fixed shape. A program expresses interface structure using layout terms. Hanka resolves those terms into concrete frames or into layout instructions. Argus holds the resulting scene tree and paints it. A thin embedder applies the paint operations to the host surface, which in the browser is the document.

```
Layout terms  ->  Hanka (layout)  ->  Argus (scene tree, paint)  ->  embedder  ->  host surface
```

The important property of the pipeline is that after the first paint it does not rebuild. When a value changes, the reactive effect calls a single node update on Argus, and Argus patches that node on the surface. There is no rebuild of the surface from a string of markup and no diff of a shadow tree.

## 3. Hanka, the Layout Engine

### 3.1 Responsibility

Hanka owns the arrangement of elements. It supports containers that lay out their children in a column, in a row, as a stack of overlapping elements, or, since this revision, as an explicit grid. Containers carry padding, a gap between children, alignment for leftover space, an optional wrap, an optional scroll behavior, and optional responsive thresholds.

### 3.2 Surface

The layout surface is conversational. A program opens a container, adds leaves such as text, buttons, images, inputs, selects, tables, and modals, and closes the container. Leaves carry an explicit size, and text leaves can request an automatic width that Hanka measures through the embedder. A single instruction resolves the open containers into the arrangement Argus paints.

### 3.3 Two modes

Hanka has a classic mode and a Path A mode. In the classic mode Hanka computes absolute coordinates for each element and feeds those frames to Argus. In the Path A mode Hanka emits layout instructions that the browser resolves natively and stops computing absolute coordinates for the common container types. For rows, columns, and grids the Path A path is now the only path taken; the absolute frame arithmetic for those kinds is retained solely as a documented fallback and is not reached. Stack containers and explicit placement remain absolute, which is the correct behavior for overlapping and pinned content.

### 3.4 Alignment along two axes

Alignment is expressible independently on the main axis and the cross axis. A single alignment token continues to set both axes together for backward compatibility, and an explicit main and cross form sets each axis on its own. Hanka carries a main alignment and a cross alignment on every container and passes both to Argus, which maps them to the browser's main axis and cross axis alignment properties. This closes an item that the previous revision listed as open.

### 3.5 Grid, scroll, and the responsive threshold

Grid is opt in. A grid container declares a column count and Hanka emits a grid instruction that becomes a native grid of that many equal columns. This is a choice offered to the author, never an imposed layout system; rows and columns remain the common case and nest to cover most two dimensional needs.

A container may declare itself a scroll region, which Argus realizes as an automatic overflow so that content taller or wider than the box scrolls within it rather than being clipped by the page.

The responsive threshold is the mechanism behind declarative breakpoints at the container level. A row may declare that it stacks into a column below a pixel width, or that it wraps below a width. Hanka evaluates the threshold against the current viewport width at layout time. Because layout runs again on viewport change, the direction is re evaluated on resize and the container flips both ways as the window crosses the threshold. The reflow correctness depends on the layout tree being retained across relayout, which the compiler now arranges whenever a program uses a responsive threshold.

## 4. Argus, the Rendering Engine

### 4.1 The scene tree

Argus owns a scene tree of nodes. Each node has an identifier, a kind, a frame or a flow position, an opacity, optional text or image source, accessibility role and label, an optional utility class string, a live region level, a scroll flag, and dirty flags. The kinds cover the common interface elements, each mapping to a concrete element on the host surface.

### 4.2 Surgical paint

The defining feature of Argus is that it paints surgically. When a single node changes, Argus updates that one node on the surface. It does not clear the surface and rebuild it. This is the mechanism that turns the Reactive Engine's constant time update into a constant time visible update. The engine exposes a region paint count so a program can assert that a change painted exactly one node.

### 4.3 Identifier index

Argus locates a node by its identifier on every update through an open addressed hash index from identifier to node index, with the scene nodes held in a stable allocation so that arena growth does not invalidate the index. An earlier linear scan made mounting quadratic; the index restored linear mounting. The performance section returns to this.

### 4.4 Accessibility

Argus applies default accessibility roles when it paints. Interactive nodes receive appropriate roles, and nodes receive an accessible label where one is present. Two capabilities the previous revision listed as open are now implemented. A live region level marks a node so that its text updates are announced by assistive technology; because reactive text already flows through a single node update, an announcement follows a state change automatically. Focus management is owned by the modal lifecycle: a modal mounts closed and hidden, an open instruction reveals it, moves focus into it, and traps the tab cycle within its focusable descendants, and a close instruction restores focus to the element that held it before. Focus is therefore never trapped by the act of painting, only by the act of opening a dialog. The position of the engine is unchanged: accessibility is a property of paint, applied by default, rather than an attribute the author must remember to add.

### 4.5 The utility class escape hatch

Argus owns layout and geometry through inline properties. For visual styling that the author wants to control directly, a node may carry a utility class string that Argus applies as the element's class attribute. Because inline properties win over class based rules, this divides responsibility cleanly: the engine keeps structural layout, and a utility framework such as a class based stylesheet owns the visual skin of color, spacing, typography, and border. This is the interoperability seam that lets an external styling system coexist with the engine rather than fight it.

### 4.6 Motion

Argus supports immediate opacity changes and a fade operation that animates opacity over a duration with an ease out curve, running on the animation frame callback in the browser. Motion is intentionally a small surface, sufficient for entrance and emphasis rather than a general animation system.

## 5. The Reactive Binding

The frontend engines do not poll. A node is connected to reactive state through a bind. When the bound value changes, the reactive effect calls the corresponding node update on Argus, and only that node is touched. The cost of reflecting a state change on the surface is the cost of updating the nodes that depend on that state, which for a typical binding is one node.

## 6. Path A in Its Completed Form

The strategy document records the decision to move layout and paint responsibility to the browser rather than reimplement text reflow, scrolling, internationalization, and the box model. Under Path A the browser lays out and paints, Hanka emits style level layout instructions, and Argus is a thin reactive patcher that applies node level updates while the browser owns geometry.

The mechanism is now complete for the reactive web wedge. Rows and columns emit flexible box instructions with direction, gap, padding, two axis alignment, and wrap. Grids emit a native grid. Flow children attach to their parent in normal flow. Automatic sizing maps to a grow behavior. Scroll regions overflow rather than clip. Declarative breakpoints exist at two levels: a container level threshold that flips direction on resize, and a viewport predicate that drives handlers through the browser's media query mechanism. The reactive property survives all of this: a bound value still repaints exactly one node under the new layout mode.

## 7. Performance

All numbers are from native builds measured on the development host and are reproduced by the frontend and reactive benchmark programs in the repository. Frontend mount of a few thousand boxes completes in well under a millisecond. Reactive mount at the scale of one hundred thousand nodes completes in tens of milliseconds and scales linearly, following the identifier index fix. A single bound update paints one node in constant time regardless of scene size. Building the interface and updating it are both cheap, and neither cost grows in a way that would degrade a large application.

## 8. Status and Limitations

Hanka supports nested containers, two axis alignment, wrap, opt in grid, scroll regions, automatic measurement, and responsive thresholds, and it drives Argus in both the classic and Path A modes. Argus supports the full set of node kinds, surgical paint, the identifier index, default accessibility roles, live regions, modal focus management, the utility class escape hatch, and the motion surface. Path A is the working default for rows, columns, and grids.

One honest boundary remains, and it is about verification rather than correctness. The layout mathematics, the reflow behavior, and the paint chain are verified by native harnesses and by construction, and the pure decoders are unit tested. The browser side behaviors — grid and scroll and class and live region emission, focus trapping, media query dispatch, and resize reflow — are verified by reading the embedder and by driving the engine natively, but they are not yet exercised in a real document by an automated end to end test. The remaining work to call the frontend closed without qualification is a headless browser smoke test over a generated page that opens a dialog, tabs through it, crosses a breakpoint, and asserts the result. That test is the thing that would convert construction level confidence into execution level proof.

## 9. Related Work

Immediate mode interface systems redraw on every frame. Retained mode systems with virtual document trees rebuild and diff a shadow tree. Native application frameworks own their own layout and paint against a canvas surface. Argus and Hanka occupy a deliberate middle position for the web target: they own the scene tree and the reactive patching, and under Path A they delegate geometry and painting to the browser rather than reimplementing them. This keeps the engine small and lets the reactive patcher, the genuine contribution, drive a surface the browser already renders well.

## 10. Conclusion

Hanka and Argus turn a reactive change into the smallest possible surface update. Hanka resolves where elements go, now with two axis alignment, opt in grid, scroll, and responsive thresholds. Argus applies changes to exactly the nodes that changed, now with live regions, modal focus management, and a styling escape hatch. The identifier index makes mounting linear, the reactive binding makes updates constant time, and Path A delegates layout and paint to the browser while keeping for LukeLang the part that is genuinely differentiated, the reactive patcher that updates one node when one value changes.
