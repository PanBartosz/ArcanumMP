# Cooperative dialogue training

Native dialogue training already runs against the initiating `DialogState.pc_obj`
inside the host's ordinary conversation evaluation. The existing shared dialogue
service therefore carries its offered skills, native price, reply and result.
Only that PC's owner chooses; observers receive the same text. This path does
not require a client command that sets arbitrary training bits or prices.

`dialog.c` interprets the retail `T:` option, offers the listed skills, checks
native apprenticeship prerequisites, computes its reaction-adjusted price,
asks for payment and transfers money before granting native training. These are
host operations. Basic and technological training bits arrive on replicas in
the normal object batch. Replica dialogue callbacks cannot evaluate the action.
Expert/master training uses additional native conditions/actions and quest
branches; those remain separate acceptance work.

## Controlled four-engine acceptance

`tests/integration_bootstrap.py --mode training --framebuffer` starts four actual
engines in both Linux/Windows host directions. It uses the unmodified Doc Roberts
dialogue, number 1280 at line 46, with the crash-site speaker's art/name. It
deliberately disables that speaker's quest/heartbeat scripts and controls PC
skill/stat and gold preparation. This is a native training-transaction fixture,
not a visit to Doc Roberts or an organic campaign playthrough.

Every owner exercises six cases in sequence:

1. Insufficient Heal skill: refusal, with no payment or training.
2. Eligible Heal skill and no money: quote followed by payment refusal.
3. Declined quote: gold and training unchanged.
4. Paid Heal apprenticeship: native charge and training exactly once.
5. Repeated Heal request: already-trained refusal, with no second charge.
6. Paid Firearms apprenticeship: technological training and its native charge.

Two native skill allocation units provide eligibility; the fixture verifies
the setter accepted them. Every case compares all four PCs' training, allocated
skill points and gold. Only the initiating PC may change. NPC gold increases
by the exact native payment. Each chooser submits duplicate keyboard press/
release pairs in one UI poll, which must still produce one action. Every
participant also attempts stale-page choices and observers attempt choices;
those requests are refused locally. The existing portable dialogue suite covers
forged/stale requests received by the host independently.

Each host accepts 88 choices across 24 conversations and 112 published pages;
replicas execute none. Every process records the same complete transcript.
Each owner submits 22 accepted choices, attempts 66 observer choices and 88
stale choices. Per-page probe acknowledgments hold the next scripted selection
until all four processes have checked the page; they are fixture coordination,
not an extra production dialogue protocol.

`evidence/training-native-points` contains the reports/transcripts and 88
composed captures. All 88 training frames were actually reviewed through 22
contact sheets, including remote quotes, refusals, decline, both success paths
and host success pages. Text is wrapped and chooser/observer labels agree.
The native rotating message area depends on hover/history and is empty in some
frames; these captures do not establish information-card parity. Captures use
the TIG framebuffer with SDL dummy/software and Windows under Wine.

Two rejected fixture runs are retained in
`evidence/training-checks/diagnostics.json`: early chooser progression raced
observer assertions, and an invalid twenty-unit preparation was correctly
refused by the native setter. The accepted fixture uses a page barrier and
checked two-unit preparation.

The same three game builds pass four follow-up drivers: initial TCP bootstrap,
the original five-page chooser/observer dialogue, ordinary runtime controls,
and native campaign/object/subsystem/save round trips in both OS directions.
All 37 accepted engine logs have no sanitizer findings. Eight additional
dialogue captures were reviewed, making 96 frames for this checkpoint.
`evidence/training-checks/regressions.json`, `accepted-log-audit.json` and
`visual-review-*.json` record these checks; the 1,516-asset installation audit
is `evidence/retail-assets-training-final.json` and reports no changes.
The preceding progression checkpoint contains the wider eighteen-regression
and 42-portable-test suite. This change adds a fixture and its engine-probe
dispatch; it does not change the production dialogue/wire implementation.

## Remaining work

Original trainer placement, expert/master quest paths, all skill/background/
race/effect variants, Educator follower side effects, fault/save/reconnect
boundaries and prolonged native play need coverage. NPC identification/repair, barter and crafting now have authoritative service
adapters; see `COMMERCE.md`. Wider dialogue-to-inventory combinations remain
campaign acceptance work. Whole campaign completion remains unfinished.
