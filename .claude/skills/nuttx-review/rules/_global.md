# Global NuttX Review Rules

Cross-cutting rules that apply to ALL subsystems. Load this file for every review.

## Patch Organization (Hard Rules)

1. **One patch, one logical change** — Never bundle unrelated fixes, refactors, typo corrections, or config changes into a functional change patch. Submit each concern as a separate commit.
2. **No merge conflict markers** — Always resolve conflicts and verify the result compiles before posting for review.
3. **No commented-out code** — Delete disabled code entirely along with any declarations, definitions, and variables that become unused. Version control preserves history.
4. **No debug artifacts** — Remove stray debug files, printf debugging, TODO markers before submission.
5. **Minimal diff** — Change only what needs changing. Unrelated style reformatting (hex case, indentation, comment syntax) obscures the real diff and makes review impossible.
6. **Self-contained feature patches** — Include documentation, Kconfig options, all arch support code, and consistent naming. No disconnected follow-ups.
7. **Complete migration** — When migrating a preprocessor guard, flag semantic, or config symbol, grep the entire codebase and update ALL occurrences. Partial migration causes inconsistent behavior.
8. **Symmetric changes** — When modifying one protocol variant (e.g., IPv4), always check and apply the same change to its counterpart (e.g., IPv6).
9. **Clean removal** — When removing a feature's invocation, also remove its config entries in defconfig to avoid orphaned config.

## Root Cause Fix (Hard Rules)

1. **Fix the library, not the caller** — When a library function is broken, fix it at the source rather than patching around it in every caller.
2. **Fix the abstraction, not the symptom** — Don't hack around a broken API with magic constants; extend or fix the API itself.
3. **Minimal correct fix** — Insert guard conditions at the narrowest scope. Don't restructure surrounding control flow or remove unrelated branches.
4. **Fix concurrency at the data-owning layer** — When a race condition causes corruption, add proper locking at the data-owning layer instead of defensive null checks at the call site.
5. **No debug logging as a fix** — Identify the actual deficiency and implement the correct mechanism (cancellation point, proper lock, etc.).

## Coding Style (Hard Rules)

1. **FAR annotation on all pointers** in function signatures and casts. Never apply FAR to scalar value types (pid_t, int, etc.).
2. **Explicit NULL comparisons** — Use `ptr != NULL` / `ptr == NULL`, not `if (ptr)` / `if (!ptr)`.
3. **Include alphabetical order** — Standard library includes sorted alphabetically within their group.
4. **78-column line limit** — Continuation lines align arguments to the opening parenthesis.
5. **Kconfig: tab indentation** — No spaces. defconfig entries in strict alphabetical order.
6. **Preprocessor directives at column 0** — No leading whitespace on #define, #if, #else, #endif.
7. **No trailing whitespace.** All text files end with a newline character.
8. **switch/case indentation** — Body indented by exactly 2 spaces from case label. Always keep explicit default case.
9. **No unnecessary casts** — Don't cast when source type already matches destination type.
10. **Magic numbers → named constants** — Replace hardcoded values with descriptive named constants.
11. **No leading-underscore identifiers** in public macros (reserved by C/POSIX).
12. **Correct spelling** — Verify function/variable names match POSIX standard spelling exactly.

## Design Philosophy (Hard Rules)

1. **Use existing frameworks** — Integrate into established patterns (sched_note, etc.) rather than bolting ad-hoc entry points.
2. **Use the highest-level API** — Don't reach through internal implementation when a purpose-built wrapper already exists.
3. **Sensible defaults** — New configurable limits ship with non-zero defaults so they work immediately.
4. **No core header pollution** — Localize driver-specific logic behind compile-time guards in the driver. Use helper functions instead of complex macro ternaries.
5. **Header self-containment** — When a header uses a macro, include its definition; never rely on transitive inclusion.
6. **No extern declarations** — Include the canonical header instead of adding standalone extern declarations.
7. **Kconfig guard new struct fields** — New fields in shared headers that increase struct size must be guarded by a Kconfig option so constrained builds pay zero cost.
8. **No shadow state** — Don't maintain a shadow counter for state already queryable from the underlying data structure. Prefer querying the source of truth.
9. **Embed state in per-instance struct** — Use per-instance device struct rather than file-scope globals for callback/notifier state.

## Error Handling (Hard Rules)

1. **Check every return value** — Every function that returns an error code must have its return value checked. Silently ignoring errors causes silent corruption.
2. **All callers updated** — When a function's signature changes to return a value, all call sites must capture and use the return value.
3. **goto label before cleanup** — A goto-out label must land before the common error-handling/cleanup path, not after it.
4. **Co-locate alloc and cleanup** — Resource allocation and cleanup belong in the same function and error-handling scope.

## Core Principles (Ten Good Habits)

1. **Coding style** — Follow existing conventions. Consistency is the #1 rule.
2. **Variable scope** — Minimize scope. Libraries must NOT use globals.
3. **Memory** — Prefer stack over heap. Use memory pools for hot paths.
4. **Threading** — Don't create threads casually. Use poll/epoll/work_queue.
5. **Error handling** — Never swallow return values. Propagate POSIX errno.
6. **Patch organization** — One patch = one logical change. Self-review first.
7. **No private copies** — Enhance the original code, don't fork it.
8. **No reinventing** — Reuse proven open source. Understand Linux/Android patterns.
9. **License** — Know GPL/LGPL/Apache implications before using OSS.
10. **Testing** — Tests accompany code. Write test first for regressions.
