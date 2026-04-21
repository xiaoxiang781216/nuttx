# Architecture (arch/) Review Rules

Rules specific to the arch/ subsystem. Always also check `_global.md`.

## Hard Rules

1. **Cross-architecture consistency** — When adding a new context field (like JB_FLAG) to setjmp/longjmp register sets, add it consistently across ALL architecture variants, not just the one tested on.
2. **Fix all arch copies** — When fixing a bug in arch-specific code duplicated across architectures, apply the same fix to every architecture copy in a single patch.
3. **Generalize multi-core support** — Generalize the architecture (symbols, macros, entry points) so each core is a first-class citizen, rather than duplicating single-core code behind #ifdef blocks with runtime core-index checks.
4. **Signal: ignore vs block** — Use `signal(SIG_IGN)` to discard unwanted signals process-wide; do not block via sigmask, which only defers delivery per-thread and contaminates shared signal sets.
5. **Minimal register clearing** — Clearing one register (LR) to terminate backtraces is sufficient. Don't memset the entire saved register context — it risks losing state needed by signal delivery.

## Anti-Patterns

- **AP-1: Incomplete feature patch** — A feature patch must include documentation, Kconfig naming conventions (MM_KASAN_ prefix), all arch support code (e.g., MMU/TBI for SW_TAGS), and consistent Makefile indentation.
- **AP-2: Stale header comments** — New files must not carry stale header comments copied from template files. Update the file path and description.
- **AP-3: Removing conditional instead of fixing** — When a conditional compilation branch adds unnecessary complexity, simplify by removing the branching entirely rather than patching the condition.

## Style Notes

- arch/sim function names: follow established short-form naming pattern (e.g., `up_buttonevent`, `up_kbdevent`)
- Overflow-safe pointer arithmetic: compute known-safe difference (where invariant guarantees no underflow) rather than adding to smaller operand

## Review Checklist

- [ ] All architectures covered: Does the change apply to all relevant arch variants?
- [ ] No commented-out code in feature patches
- [ ] Multi-core: Are symbols/macros generalized per-core?
- [ ] Signal handling: Using SIG_IGN, not sigmask?
- [ ] Context save/restore: Are all register fields consistent across architectures?
- [ ] Makefile indentation: Spaces not tabs in NuttX Makefiles

## Traceability

| Rule | Source Changes |
|------|---------------|
| HR-1 (cross-arch fields) | #7334351, #6035411 |
| HR-2 (fix all copies) | #6035450 |
| HR-3 (multi-core) | #7334351 |
| HR-4 (signal ignore) | #6035411, #6035450 |
| HR-5 (minimal clear) | #7334351 |

For full before/after diff examples, Read `examples/arch_examples.md`.
