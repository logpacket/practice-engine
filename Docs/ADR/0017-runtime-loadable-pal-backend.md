# ADR-0017: PAL backends are runtime-loadable modules (symmetric with RHI)

**Status:** Superseded by [ADR-0018](0018-configure-time-pal-static-link.md) (same session — see "Why this ADR exists" below)

## Why this ADR exists

For roughly one editing pass, the design pivoted from configure-time PAL
backend selection to runtime-`dlopen`-loadable PAL backends, motivated by the
desire for symmetry with RHI backends (which ARE runtime-loaded — [ADR-0002](0002-rhi-is-a-dynamically-loaded-module.md)).
A user critique surfaced that the symmetry-for-symmetry's-sake argument was
weaker than initially presented; ADR-0018 records the reversal in detail.

This ADR file is preserved per the append-only convention so the navigation
trail (`ADR-0005` → `ADR-0017` → `ADR-0018`) is intact, and so future
contributors can see exactly which counter-arguments cause this decision
to lose to its alternative.

## Original context (for the record)

[ADR-0005](0005-applicationcore-as-pal.md) introduced the PAL with configure-time backend selection. The decision was justified by "simplest model that solves the immediate problem". When the question "why is PAL configure-time but RHI runtime-loadable?" was raised, the original ADR-0005 reasoning turned out to be inertia rather than principle — the same question deserved a real answer.

## Decision (now superseded)

PAL backends would become runtime-loadable SHARED modules siblings of `ApplicationCore` (the interface), discovered by `pe::ModuleLoader::LoadModule(name, …)`. The default backend would be hardcoded in `Launch` with env/CLI override.

## Why it was reversed (full reasoning in ADR-0018)

Five UE-derived counterarguments survived scrutiny:

1. **Cross-OS compilation is the real constraint.** A Win32 backend's `windows.h` cannot compile on Linux; the apparent "runtime selection" only applies within OS-compatible backends. ADR-0017's run-time loading was largely cosmetic.
2. **Boot order**: ModuleLoader itself depends on platform APIs; PAL is closer to a foundational dependency than a "module among modules".
3. **Console NDA**: not relevant for this engine today, but the configure-time path is what makes Stage 7 console backends shippable.
4. **No use case for runtime selection**: shipping games never let users pick a PAL backend at runtime; the default chosen at build is what runs.
5. **PAL ≠ RHI domain**: RHI backends are runtime-loadable because multiple GPU vendors coexist on one OS and users may genuinely benefit from runtime backend choice. PAL backends are mutually exclusive on a given OS install.

The symmetry argument loses to these five together. ADR-0018 takes the corrected position.

## References

- [ADR-0005](0005-applicationcore-as-pal.md) — PAL design (the parent decision)
- [ADR-0018](0018-configure-time-pal-static-link.md) — supersedes this
- [ADR-0002](0002-rhi-is-a-dynamically-loaded-module.md) — the RHI runtime-load decision that ADR-0017 had tried to mirror
