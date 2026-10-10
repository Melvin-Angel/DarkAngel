# Visual locomotion blend spaces

Selected 1D/2D blend nodes now expose a bounded coordinate view. Directional triangles and numbered points show native authored geometry; a yellow cross shows requested isolated-preview input, not inferred weights. Point clicks and an Inspect blend point selector focus existing typed input/X/Y fields instead of displaying every point form. Node connections can be hidden to focus on geometry. 2D node labels correctly identify lateral/forward ownership.

`python scripts/verify_binding_navigation.py blend-space` passed editor build and one D3D12 exercise. The scripted selected point2 and requested lateral0.5/forward1.2 retain three native topology commands. Capture inspected after hiding connections: complete triangles, point highlight and matching typed point fields are visible. No additional native fixture needed for read-only geometry/selection; existing numeric edits retain native validation/history. [Receipt](evidence/blend-space.json).

Finite point and triangle-reference display guards expose malformed drafts; Save/preview remains final validation. Point dragging and inferred weight computation are not implemented. No publication/live gameplay changes. M4/M5 In progress; tag states/reactions/layers planned. Next: inspect actual native sampled clips/weights in the isolated graph preview. History stays MVP.
