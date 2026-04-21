# Device Drivers (drivers/) Review Rules

Rules specific to the drivers/ subsystem. Always also check `_global.md`.

## Hard Rules

1. **Fix concurrency at data-owning layer** — When a race condition causes corruption or null access, add proper locking at the data-owning layer (the driver struct), not defensive null checks at the call site.
2. **Fix at the correct abstraction layer** — When a buffer needs draining, fix the wait logic at the character-level function where the condition is already handled (e.g., uart_putxmitchar), not at the higher-level write function. Use a loop to ensure complete drainage.
3. **Operation ordering for shared state** — Operations that populate shared state must execute before operations that consume it. Review call ordering when moving code between functions.
4. **Kconfig-guard new struct fields** — New fields in shared headers that increase struct size must be guarded by a Kconfig option so resource-constrained builds pay zero cost.
5. **Guard side-effect calls** — Ensure side-effect calls like wakeup are guarded by the condition that actually requires them, not called unconditionally.

## Anti-Patterns

- **AP-1: Forward declaration instead of reordering** — Don't add forward declarations; move function definitions before their first use. Keeps the dependency order explicit.
- **AP-2: Wholesale binary data reformatting** — When updating firmware tuning arrays, change only the bytes that need updating. Do not reformat hex case, indentation, or comment syntax. Style churn obscures the real diff.
- **AP-3: Blank lines between lock release and return** — Don't leave blank lines between a lock release and the immediately following return; they form a single logical unit.
- **AP-4: Rename patch with extra whitespace** — A rename/refactor patch must contain only the rename and its mechanical fallout. No unrelated whitespace or formatting fixes.

## Style Notes

- Prefer `MSEC2TICK(NAMED_CONSTANT)` over `MSEC2TICK(100)` for timeout values
- Preserve established function names when refactoring internals (don't rename unless necessary)
- Function calls exceeding line length: wrap with continuation-line arguments aligned to opening parenthesis

## Review Checklist

- [ ] Race conditions: Is shared state protected by proper locking?
- [ ] ISR vs thread context: Are blocking calls avoided in interrupt handlers?
- [ ] Named constants: Are magic numbers replaced with descriptive names?
- [ ] Minimal diff: Does the patch change only what's necessary?
- [ ] Lock ordering: Is there a consistent lock acquisition order?
- [ ] work_queue usage: Are deferred operations properly scheduled?
- [ ] Error paths: Do all error paths release acquired resources?

## Traceability

| Rule | Source Changes |
|------|---------------|
| HR-1 (concurrency root cause) | #851831, #850914, #6035411 |
| HR-2 (correct layer) | #850914, #851831 |
| HR-3 (operation ordering) | #851831 |
| HR-4 (Kconfig guard) | #850914 |
| AP-1 (no forward decl) | #851831 |
| AP-2 (binary data) | #850914 |

For full before/after diff examples, Read `examples/drivers_examples.md`.
