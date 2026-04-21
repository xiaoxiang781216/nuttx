# Network Stack (net/) Review Rules

Rules specific to the net/ subsystem. Always also check `_global.md`.

## Hard Rules

1. **IPv4/IPv6 symmetry** — When modifying one protocol-variant file (e.g., ipt_* for IPv4), always check and apply the same change to its counterpart (e.g., ip6t_* for IPv6).
2. **Switch fallthrough audit** — When fixing a switch fallthrough bug, audit ALL cases in the same switch for the same class of missing break/goto, not just the one that triggered the bug report.
3. **Refcount: explicit reset to zero** — When releasing a resource that must reach zero references, set the refcount to 0 explicitly rather than decrementing, to avoid assumptions about current count.
4. **State ownership at correct level** — Place protocol offload state (e.g., GRO aggregation context) at the network device level (net_driver_s), not at individual IO buffers (iob_s). Offload is a per-device capability, not per-packet metadata.
5. **No shadow counters** — Don't maintain a shadow counter for state already queryable from the underlying data structure (e.g., use `iob_get_queue_entry_count`). Shadow state causes synchronization bugs.

## Anti-Patterns

- **AP-1: Double negation in loop termination** — Prefer explicit break conditions over inverting return value semantics. Double negation obscures intent and may miss edge cases.
- **AP-2: Trivial wrapper functions** — Avoid wrapper functions that hide trivial flag-setting behind an API. Inline the operation or use a macro.
- **AP-3: Debug logging as a fix** — Don't commit debug logging as a fix. Identify the actual deficiency (e.g., missing cancellation point compliance) and implement the correct POSIX mechanism.
- **AP-4: Conditional format strings** — Diagnostic/procfs format strings should be unconditional for consistent output layout. Use typed zero defaults in #else branches for disabled features.

## Style Notes

- switch/case: Body indented by exactly 2 spaces from case label
- Always keep explicit default case (even if it only breaks)
- Factor identical code out of #ifdef/#else — wrap only the differing logic

## Review Checklist

- [ ] Protocol symmetry: Is the IPv4/IPv6 counterpart updated?
- [ ] Switch completeness: Are all cases audited for fallthrough?
- [ ] Resource lifecycle: Are alloc and cleanup co-located?
- [ ] No shadow state: Using source-of-truth queries?
- [ ] procfs format strings: Unconditional layout?
- [ ] Flag migration: All check sites updated consistently?

## Traceability

| Rule | Source Changes |
|------|---------------|
| HR-1 (IPv4/IPv6 symmetry) | #7334351 |
| HR-2 (fallthrough audit) | #6035411 |
| HR-3 (refcount reset) | #6035450 |
| HR-4 (state ownership) | #7334351 |
| HR-5 (no shadow counters) | #6035411 |

For full before/after diff examples, Read `examples/net_examples.md`.
