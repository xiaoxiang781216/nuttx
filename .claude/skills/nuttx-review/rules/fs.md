# Filesystem (fs/) Review Rules

Rules specific to the fs/ subsystem. Always also check `_global.md`.

## Hard Rules

1. **goto label before cleanup** — A goto-out label must land before the common error-handling/cleanup path, not after it. Skipping error checks silently swallows failures.
2. **Stack alloc: always zero-init** — When replacing heap allocation (zalloc/calloc) with stack allocation, always explicitly zero-initialize the struct. Stack memory is uninitialized and stale fields cause undefined behavior.
3. **Check every disk write return** — Every function that returns an error code in filesystem code must have its return value checked. Silently ignoring disk write errors causes silent data corruption.
4. **POSIX limits via Kconfig** — When a POSIX interface needs a system limit, define it as a Kconfig option, expose via the standard header (limits.h), validate at the API boundary, and expose via sysconf. Don't patch internal control flow.

## Anti-Patterns

- **AP-1: Mixing error-handling refactors into feature patches** — Implement the minimal correct fix. Don't mix unrelated error-handling refactors into a feature patch.
- **AP-2: Inlining raw buffer arithmetic** — Complex filesystem operations involving disk layout changes must be decomposed into well-named static helper functions with clear single responsibilities and complete error-path cleanup.
- **AP-3: VFS internal detail reach-through** — Use the highest appropriate abstraction layer. Don't reach through internal implementation details (fdlist + task lookup) when a purpose-built wrapper (file_dup) already exists.
- **AP-4: Extending build system by bypassing abstractions** — When build system abstractions don't support a feature, extend the abstraction with new parameters rather than bypassing it with lower-level commands.

## Style Notes

- Kconfig files: tab indentation, no mixing spaces and tabs
- Embed notifier/callback state in per-instance device struct, not file-scope globals

## Review Checklist

- [ ] Error paths: goto labels before cleanup code?
- [ ] Stack alloc: Zero-initialized?
- [ ] Disk writes: Return value checked?
- [ ] POSIX limits: Kconfig + limits.h + sysconf?
- [ ] Helpers: Complex operations decomposed into named functions?
- [ ] Abstraction: Using highest-level API available?

## Traceability

| Rule | Source Changes |
|------|---------------|
| HR-1 (goto label) | #7334351 |
| HR-2 (stack zero-init) | #6035411 |
| HR-3 (disk write check) | #6035450 |
| HR-4 (POSIX limits) | #7334351, #6035411 |

For full before/after diff examples, Read `examples/fs_examples.md`.
