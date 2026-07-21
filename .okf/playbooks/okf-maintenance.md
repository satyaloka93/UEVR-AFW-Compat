---
type: playbook
title: OKF consumption, maintenance, and usage logging
description: Keep the bundle as current canonical knowledge rather than a task worklog; amend concepts in place when facts solidify and record concise consume/maintain audit entries in the root log.
tags:
- okf
- knowledge-management
- maintenance
- logging
timestamp: '2026-07-20T19:05:00+09:00'
---

# Purpose

This bundle is a current knowledge model, **not a chronological engineering
worklog**. Worklogs, raw logs, dumps and safety bundles remain evidence sources.
OKF concepts contain the durable conclusions an agent should trust now.

# Consumption protocol

1. Read `/index.md`, then only the concepts relevant to the task.
2. Prefer the concept's current statement over old chronology, but verify
   changing project state against its cited artifact when necessary.
3. If the bundle materially informed a task, add one concise
   `**Consumption**` entry to `/log.md`: name the concepts/domain used and
   whether they remained current. Do not record every file read or tool call.

# Maintenance protocol

When evidence becomes solid:

1. **Amend the canonical concept in place.** Replace stale status, constraints,
   hashes or causal statements; do not append a diary section to the concept.
2. Create a new concept only for a genuinely reusable mechanism, decision,
   playbook or independently useful asset. One concept still means one idea.
3. Keep hypotheses and one-off incidents in the project/worklog until proven.
   An unresolved result may be listed as an open validation item, not promoted
   to a fix.
4. Update affected cross-links, directory indexes and timestamps in the same
   pass.
5. Add one concise `**Maintenance**` entry to `/log.md` summarizing which
   canonical knowledge changed and why. The log is an audit pointer, not the
   knowledge itself.
6. Run strict validation and fix all errors/warnings before committing.

# Usage-log vocabulary

Use these leading labels in the root log:

- `**Consumption**` — bundle knowledge guided a task; no canonical fact changed.
- `**Maintenance**` — concepts were amended/created because evidence solidified.
- `**Validation**` — optional when validation or structural repair is the only
  action.
- `**Deprecation**` — a concept/resource is no longer current; point to its
  replacement rather than silently deleting context.

Entries should answer: **what knowledge area was used or changed, and what is
the canonical place now?** They should not reproduce the task transcript.

# Commit discipline

Commit `.okf/` independently from unrelated source changes when practical so
knowledge diffs are reviewable. The root log entry and concept edits belong in
the same commit. See [checkpointing and recovery](/playbooks/checkpoint-and-recovery.md).

# Related

- [Staged fix methodology](/playbooks/staged-fix-methodology.md)
- [Checkpointing and recovery](/playbooks/checkpoint-and-recovery.md)
