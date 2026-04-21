---
name: nuttx-review
description: >-
  NuttX kernel architect-level code review with subsystem-specific rules
  (arch/drivers/net/sched/fs/mm/libs) and on-demand Gerrit precedent examples.
  Use when: review nuttx patch, nuttx code review, check nuttx patch,
  nuttx architect review, nuttx gerrit review, pre-submit nuttx patch check
---

# NuttX Review Skill

Review vela/nuttx patches against architect standards before submitting to Gerrit.

## Repository Structure

- **`rules/`** — *Statute*: hard rules + anti-patterns + checklists. Always loaded during review.
  - `_global.md` — cross-subsystem rules (patch org, style, root cause, error handling, ten good habits)
  - `<subsystem>.md` — subsystem-specific rules (one per subsystem in the routing table below)
- **`examples/`** — *Case law*: full before/after diffs from source Gerrit changes. Loaded on demand (typically at most 1-2 example files per review), when an identified issue benefits from illustrating a concrete precedent (e.g., citing a `#change-id` from the Traceability table of a rules file). Keep token budget in mind — prefer citing the precedent by change-id without loading the full example when the rule alone is self-explanatory.

## Usage

```
/nuttx-review <gerrit-url-or-local-diff>
```

## When NOT to Use

- **Pure nxstyle/formatting fixes** — Use `/code-style` skill; nxstyle handles these mechanically
- **Merge/Revert commits** — Mechanical commits; no architectural review needed
- **Defconfig-only changes** — Unless also touching Kconfig files
- **Third-party code (external/)** — Different standards apply; use `/port-thirdparty-lib`
- **Non-code changes (docs only)** — No architectural review needed

## Workflow

1. Fetch the patch diff (from Gerrit URL or local git diff)
2. **Identify subsystem** from changed file paths
3. Load `rules/_global.md` (cross-cutting rules, always loaded)
4. Load `rules/<subsystem>.md` (subsystem-specific rules)
5. For each changed file, check in priority order:
   a. **Hard Rules** — violations here are bugs (highest priority)
   b. **Anti-Patterns** — known mistake patterns from review history
   c. **Design** — architectural concerns
   d. **Style** — NuttX coding style issues
   e. **Review Checklist** — ensure all items are addressed
6. For each issue found:
   - Cite exact file:line
   - Name the violated rule by its numbered ID from the loaded rules file
     (e.g., `drivers/HR-3` for a Hard Rule, `arch/AP-2` for an Anti-Pattern,
     or `global/patch-org-1` for a cross-cutting rule). The prefix before
     `/` is the rules filename stem; `HR-N` / `AP-N` / `<topic>-N` come from
     the Hard Rules / Anti-Patterns / Traceability tables inside the file.
   - Show the preferred fix approach
   - Rate severity: correctness > design > style
7. Output structured review report
8. If concrete Gerrit precedent would help illustrate, Read from `examples/<subsystem>_examples.md`

## Subsystem Routing

Determine the primary subsystem from changed file paths:

| File Path Prefix | Subsystem | Rules File |
|-------------------|-----------|------------|
| `drivers/` | drivers | `rules/drivers.md` |
| `arch/` | arch | `rules/arch.md` |
| `net/` | net | `rules/net.md` |
| `sched/` | sched | `rules/sched.md` |
| `fs/` | fs | `rules/fs.md` |
| `mm/` | mm | `rules/mm.md` |
| `libs/` | libs | `rules/libs.md` |
| Other paths | other | `rules/other.md` |

If a patch touches multiple subsystems, check rules for each.
`include/` changes: check the subsystem that defines the API.

## Output Format

Output in **English** — reviews may be shared with the openvela / Apache NuttX
upstream community. Rule IDs, file paths, and Gerrit change numbers are
always English regardless of conversational language.

```markdown
# Architect Review: {Patch Subject}

**Files:** {file list}
**Subsystem:** {detected subsystem}

## Issues

### Issue 1: {category} [{severity}]
**File:** path/to/file.c:123
**Rule:** {the rule violated}
**Problem:** {what's wrong}
**Fix:** {how to fix it}

## Summary
| Severity | Count |
|----------|-------|
| correctness | N |
| design | N |
| style | N |

**Verdict:** APPROVE / REQUEST CHANGES / REJECT
```

## Data Source

| Metric | Count |
|--------|-------|
| Gerrit merged changes queried | 4,774 |
| With review delta (ps1 ≠ final) | 1,852 |
| Classified patterns | 2,032 |
| Generated | 2026-04-03 |

Some review deltas yielded multiple classified patterns (e.g., a change with both a
root-cause fix and a style correction produces two separate rules).
