# Scheduler (sched/) Review Rules

Rules specific to the sched/ subsystem. Always also check `_global.md`.

## Hard Rules

1. **Critical section on every exit path** — When adding a critical section around a loop body, every exit path (break, error return, early exit) must release the lock. Place `leave_critical_section` immediately before each break rather than relying on a single release point at the bottom.
2. **Unconditional critical sections** — Critical sections protecting scheduler state and context switches must be unconditional. IRQ nesting on single-CPU can cause the same races as SMP if the critical section is compiled out.
3. **Integrate into existing framework** — New functionality must integrate into the existing framework (e.g., sched_note) rather than bolting ad-hoc entry points onto a peripheral driver. Follow established patterns for note types, data structures, Kconfig gating, and no-op macros.
4. **Single-responsibility primitives** — Keep low-level functions single-responsibility and stateless. Push retry/loop policy to callers who understand the intent. A cancel primitive should report status (EBUSY), not embed wait-and-retry logic.

## Anti-Patterns

- **AP-1: Critical section leak** — Adding `enter_critical_section` without auditing all exit paths. Every break/return/goto inside the protected region must call `leave_critical_section`.
- **AP-2: Ad-hoc scheduler hooks** — Don't add ad-hoc entry points to drivers for scheduler instrumentation. Use the sched_note framework.
- **AP-3: Compiling out critical sections** — Never gate critical sections on SMP config. Single-CPU IRQ nesting causes the same races.
- **AP-4: Debug prints in patches** — Don't include debug prints or unrelated config changes alongside a functional fix.

## Style Notes

- Global variables must carry a module-specific prefix (e.g., `g_sched_*`) to avoid name collisions
- FAR qualifier: only for pointers, never for scalar value types (pid_t, int)
- Comment capitalization: only capitalize first word after period or at start of block comment
- Continuation lines: align with opening parenthesis or operator

## Review Checklist

- [ ] Critical sections: Every exit path releases the lock?
- [ ] Critical sections: Not conditionally compiled out?
- [ ] Framework integration: Using sched_note, not ad-hoc hooks?
- [ ] Single responsibility: Low-level functions stateless?
- [ ] Global naming: Module-specific prefix on globals?
- [ ] FAR correctness: Only on pointers, not scalars?

## Traceability

| Rule | Source Changes |
|------|---------------|
| HR-1 (critical section leak) | #7334351 |
| HR-2 (unconditional critsec) | #6035411 |
| HR-3 (framework integration) | #7334351, #6035450 |
| HR-4 (single responsibility) | #6035411 |

For full before/after diff examples, Read `examples/sched_examples.md`.
