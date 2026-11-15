<!--
Copy to .scratch/gameplayer-v1-review/findings/ticket-<MM>-<reviewer>.md
(<MM> = reviewed ticket 02-10; <reviewer> = short lowercase name).
Fill every section; "None." if empty. Delete these comments.
Plain prose, GLOSSARY.md terms, path:line at the reviewed commit. Fixes in words, no patches.
-->
# Findings: ticket <MM>, <title of the reviewed ticket>
- Reviewer: <name>
- Review ticket: `.scratch/gameplayer-v1-review/issues/<NN>-....md`
- Commit reviewed: <git rev-parse --short HEAD>
- Unit tests: <test.bat passed (N cases) / failed (which) / not run (why)>
- Build: <built / not built (why) / not attempted>

## Verdict
<2-4 sentences. End with: Sound / Sound with fixes / Needs rework.>
Counts: Blocker <n>, Should fix <n>, Nit <n>.

## Acceptance criteria of the original ticket
Source: `.scratch/gameplayer-v1/issues/<MM>-....md`. One row per checkbox, in order. Verdict: met / not met / cannot tell from code. Criterion changed by a later decision (listed in the review ticket): judge against it, write "per later decision". Needs monitor, speakers or hardware: "cannot tell from code" unless code makes it impossible.
| # | Criterion (short) | Verdict | Evidence (path:line) or reason | Finding |
|---|---|---|---|---|

## Findings
By severity, then importance. F1, F2, ...
### F1. <title>
- Severity: Blocker / Should fix / Nit
- Axis: Spec / Standards / Correctness / Latency
- Location: `<path>:<line>`
- What is wrong:
- Why it matters:
- Suggested fix:
- Confidence: high / medium / low, <why>

## Looks wrong, checked, fine
<what, path:line, why fine; one paragraph each>

## Known and not yet verified by hand: comments
<item: what the code does, does it look right, what the hand check should look for. New defects go in Findings.>

## Noticed outside this ticket's scope
<one line each, path:line, no severity>
