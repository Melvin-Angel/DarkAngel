# Royal Crown imported material deduplication

Date: 18 October 2026

The 88 static Royal Crown model imports contained 88 distinct material product
IDs and 94 distinct texture product IDs even though their cooked definitions
and texture payloads were repeated. Their exact cooked signatures reduce to
five material products and six Crown texture products. The registry also
contains one texture product for the separate player model.

The model material-slot keys (`material/0`, etc.), model IDs and mesh IDs stay
model-specific. Only the imported material and texture subasset product IDs
are shared, selected by equal cooked material fields and equal cooked texture
payloads. This preserves object-level material override addressing while
allowing packages to carry each identical product once.

`AssetService::scan` and coordinated native-save validation permit cross-model
identity reuse only for static imported material slots with material slots,
or texture slots with texture slots. Root, mesh, and other IDs remain globally
unique. Runtime package assembly still rejects any shared product ID whose
kind, cooked hash, or extension differs. The Crown import script applies the
same conflict check while combining partial registries.

## Verification

- `python scripts/import_royal_crown_temple.py` recooked the 88 model roots,
  preserved the 1,052-entity scene, and generated the combined registry.
- Registry audit: five Crown material products and six Crown texture products;
  shared registry IDs are unique in the combined manifest.
- `MaterialTests` passed, including identical cross-root material sharing and
  refusal to package different settings under one shared material ID.
- `git diff --check` passed.

This is content/identity deduplication; it does not claim a frame-time gain.
The models retain distinct geometry and their own draw/instance groups.
