# Conversational v1 archive (Phase 5 deprecation)

These are the pre-flip conversational sources kept for:

- `luke --syntax=1 BUILD …` (deprecation window)
- `scripts/syntax_v2_equiv.sh` / `syntax_v2_migrate_equiv.sh` / `fmt_roundtrip_all.sh`
- `luke MIGRATE` input

The live tree under `examples/build/` and `vm/stdlib/` is **syntax v2** in `.luke` files.
`.lk` was the transition extension and is removed from those directories at the Phase 5 flip.

Do not edit these archives except to keep gates honest; prefer changing the live v2 sources.
