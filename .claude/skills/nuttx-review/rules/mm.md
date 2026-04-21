# Memory Management (mm/) Review Rules

Rules specific to the mm/ subsystem. Always also check `_global.md`.

## Hard Rules

1. **Fix library, not caller** — When a library function (e.g., mm allocator) produces incorrect results, fix the library at its root rather than patching around it in every caller.
2. **Fix abstraction, not hack around** — Don't paper over a broken abstraction with magic-constant hacks. Fix the abstraction itself (use offsetof for correct layout, thread values through existing fields, fix all dependent macros consistently).
3. **Context-aware lock lifecycle** — When a function is called from both normal and interrupt context, implement proper context-aware lock management instead of silently skipping work or adding bailout guards.
4. **Fix macro, not call sites** — When a disabled-feature stub macro causes unused-variable warnings by expanding to its argument, fix the macro definition to not reference the parameter rather than suppressing warnings at every call site.

## Anti-Patterns

- **AP-1: Symmetric bookkeeping duplication** — When alloc and free paths perform symmetric bookkeeping (magic checks, pid/seqno updates, backtrace recording), consolidate into one function with a direction parameter rather than duplicating logic.
- **AP-2: Private header cross-module include** — When a subsystem header is needed by multiple sibling subsystems (e.g., mm_heap, mempool both need kasan), promote it from a private module header to a public `include/nuttx/` header.
- **AP-3: Board config bundled with feature** — A patch introducing a new subsystem feature must not bundle board-specific defconfig changes. Submit board enablement as a separate follow-up.

## Style Notes

- When converting internal macros to public functions, preserve original macro names for API consistency
- Multi-line function call with cast: keep cast and function name on same line, align continuation to opening parenthesis
- Feature additions must cover full stack: documentation, Kconfig, toolchain flags, arch enablement, runtime hooks, shadow memory setup

## Review Checklist

- [ ] Library fixes: Fixed at the root, not patched at callers?
- [ ] Abstractions: Using offsetof/proper APIs, not magic constants?
- [ ] Lock lifecycle: Context-aware for normal/interrupt callers?
- [ ] Macros: Stub macros don't reference unused parameters?
- [ ] Symmetric ops: Consolidated, not duplicated?
- [ ] Headers: Public if needed by multiple subsystems?

## Traceability

| Rule | Source Changes |
|------|---------------|
| HR-1 (fix library) | #7334351, #6035411 |
| HR-2 (fix abstraction) | #6035450 |
| HR-3 (context-aware lock) | #7334351 |
| HR-4 (fix macro) | #6035411 |

For full before/after diff examples, Read `examples/mm_examples.md`.
