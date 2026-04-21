# Other Subsystems (Kconfig, tools, build) Review Rules

Rules for code outside the main subsystems: Kconfig, Makefiles, tools, nxgdb, documentation, build system. Always also check `_global.md`.

## Hard Rules

1. **IOCTL ABI stability** — When adding new IOCTL numbers, never renumber existing IOCTLs belonging to other subsystems. Allocate the next unused number after your subsystem's block, accepting gaps to preserve ABI stability.
2. **Struct field alignment** — In sensor data structs, prefer naturally-aligned types (int32_t) over narrow types (int8_t) to avoid implicit padding, ensure consistent struct layout across architectures, and prevent unaligned access penalties.
3. **Feature gate completeness** — When feature-gating a group of related functions, gate ALL of them consistently. Don't leave some members of the group ungated. Place the split point so the entire logical group moves together.
4. **Config dependency verification** — When moving feature configs between defconfigs, verify all implicit dependencies are satisfied in the target config. Run a dependency check or build to catch missing Kconfig prerequisites.

## Anti-Patterns

- **AP-1: Trivial wrapper as source file** — When a function is a trivial one-line wrapper around another function with fixed arguments, implement it as a macro in the header rather than creating a separate source file.
- **AP-2: Board ROMFS bundled with features** — A patch adding board ROMFS/rcS infrastructure must not also bundle unrelated feature enablement (e.g., uORB). Each feature should be a separate patch.
- **AP-3: Leading-underscore macros** — Never use leading-underscore identifiers in public macros (reserved by C/POSIX). Use descriptive names like `predict_true`/`predict_false`.

## Style Notes

- Preprocessor directives at column 0 with no leading whitespace
- Defconfig entries in strict alphabetical order; insert at sorted position
- No trailing whitespace before closing parentheses or at end of expressions
- Macro naming: no leading underscores, descriptive names

## Review Checklist

- [ ] IOCTL numbers: No renumbering of existing subsystem IOCTLs?
- [ ] Struct alignment: Naturally-aligned types preferred?
- [ ] Feature gating: All related functions gated consistently?
- [ ] Config deps: All implicit dependencies satisfied?
- [ ] Defconfig order: Alphabetical?
- [ ] Macros: No leading underscores in public macros?

## Traceability

| Rule | Source Changes |
|------|---------------|
| HR-1 (IOCTL ABI) | #7334351, #6035411 |
| HR-2 (struct alignment) | #6035450 |
| HR-3 (feature gate) | #7334351 |
| HR-4 (config deps) | #6035411 |

For full before/after diff examples, Read `examples/other_examples.md`.
