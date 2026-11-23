# T-023 — The owner's 30-minute playtest: script and questionnaire

**Assignee:** Astra · **Status:** open (after T-021) · **Branch:** `helper/astra/T-023`
**Deliver to:** `docs/reviews/playtest-script.md` only.

## Why
Your T-006 review set numeric targets that stay "tracking metrics" until a real 30-minute session by the owner
confirms the cadence (the lead's decision on PR 19). That session needs a plan, so one sitting produces
answers instead of impressions.

## Read first
`AGENTS.md`; `docs/CONCEPT.md`; your `docs/reviews/astra-design-review.md` (each finding's "human check");
`docs/SPEC-MYTHOS.md` sections 0 and 2; `docs/handoff/release-notes.md` (owner playtest 1: what went wrong
last time); `docs/SPEC-UI.md` sections 4 and 5.

## Deliver
A one-sitting plan for the owner, who is not a game designer and will play once, for 30 to 40 minutes:
- Before: what build to run, what settings, what the lead should log automatically (list the counters, so the
  lead's build agents can add them).
- During: at most 6 pause points tied to moments in the game (not clock times), each with one or two short
  questions in plain language ("what were your options just now?"). No more than 12 questions in total.
- After: a 10-question questionnaire (1-5 scales plus two free answers) that maps each answer to one of your
  findings or to a Mythos system, with the threshold that would turn a tracking metric into a gate.
- A one-page "how the lead will read the answers" table.

## Acceptance criteria
Plain language, short, respectful of the owner's time; every question traceable to a finding or a system;
no question that needs design vocabulary to answer.
