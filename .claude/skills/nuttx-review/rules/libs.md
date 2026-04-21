# Standard Libraries (libs/) Review Rules

Rules specific to the libs/ subsystem. Always also check `_global.md`.

## Hard Rules

1. **POSIX spelling verification** — Always verify function names in declarations match the POSIX standard spelling exactly. Typos in headers propagate silently as link errors or wrong symbols.
2. **Format specifier: fix type, not semantics** — When fixing format specifier type-safety (e.g., adding 'z' length modifier for size_t), preserve the original numeric base (x for hex, u for decimal). Fix the type, not the semantics.
3. **.PHONY only for non-file targets** — Never mark real filesystem targets (especially directories) as .PHONY. .PHONY is only for targets that don't correspond to actual files, otherwise make unconditionally re-executes the recipe.
4. **Syscall allocation in unconditional range** — New syscalls that must work in all build modes (including CONFIG_BUILD_FLAT) should be allocated in the unconditional syscall number range with proper reserved count updates, not appended after conditional syscalls.
5. **Zero-initialize before partial assignment** — Always zero-initialize unions and structs before partial field assignment to prevent undefined behavior from uninitialized padding or inactive members.

## Anti-Patterns

- **AP-1: Incomplete conditional removal** — When removing a config guard, remove it consistently from ALL layers: source files, public headers, C++ wrappers, CSV symbol tables, and Kconfig. Partial removal causes declaration/implementation inconsistency.
- **AP-2: Blanket comment-out** — Comment out only the specific rules that need disabling. Don't blanket-comment adjacent logic (download, build) that may still be required.
- **AP-3: Separate early return vs inline guard** — When adding a NULL check before an existing condition on the same variable, merge it into the existing condition rather than adding a separate early-return block.

## Style Notes

- FAR annotation on ALL pointer types in function signatures and casts
- Multi-line function calls: continuation lines align to opening parenthesis
- All text files end with a newline character
- Use canonical Kconfig variable names (CONFIG_ARCH_TOOLCHAIN_CLANG, not CONFIG_ARM_TOOLCHAIN_CLANG)
- CMake keyword casing: NOT, not `not`

## Review Checklist

- [ ] POSIX names: Exact standard spelling verified?
- [ ] Format specifiers: Type fixed, semantics preserved?
- [ ] Makefile targets: .PHONY only for non-file targets?
- [ ] Syscall numbers: In unconditional range?
- [ ] Partial init: Struct/union zero-initialized first?
- [ ] Config removal: All layers updated consistently?
- [ ] FAR: On all pointers in signatures and casts?

## Traceability

| Rule | Source Changes |
|------|---------------|
| HR-1 (POSIX spelling) | #7334351 |
| HR-2 (format specifier) | #6035411 |
| HR-3 (.PHONY) | #6035450 |
| HR-4 (syscall range) | #7334351 |
| HR-5 (zero-init) | #6035411 |

For full before/after diff examples, Read `examples/libs_examples.md`.
