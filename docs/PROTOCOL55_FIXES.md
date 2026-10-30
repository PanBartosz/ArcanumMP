# Protocol55: dense campaign areas

Fixes session failure when a dense original area exceeds the multiplayer
object-tracking limit. The Tulla continuation loaded 40,329 native instances
while preparing its exit after a shared reload; the previous limit was 32,768.

The limit is now 131,072 instances, with the existing 16 MiB byte budget retained.
Tracker storage grows geometrically instead of reallocating for each new object.
Protocol55 requires all players to update together. Existing native campaign
saves remain compatible; the check loads the original protocol54 Tulla save.

Linux, Linux ASan/UBSan and Windows builds pass. Four mixed engines complete
the shared reload and continue replication beyond the old failure: Linux host,
ASan/UBSan Linux guest and two Windows guests under Wine. Session/discovery
checks pass, including incompatible-version rejection. Record:
`evidence/large-scene-p55/acceptance.json`.

Also fixes an out-of-bounds read in the experience gauge when rounded progress
reaches100% during native loading. The original Vendigroth transition save
reproduces that boundary on all four characters. All four engines load the
unchanged native recovery and continue to the third floor with the corrected
gauge; ASan/UBSan reports no repeated failure. Record:
`evidence/large-scene-p55/experience-gauge.json`.

The connected original route now reaches the Device ending, including original
Vendigroth acquisition, return/banishment, Arronax and castle passages. All four
engines complete shared ending presentation and native menu return. The route
uses strengthened combat stats and retained native checkpoints; normal owner
movement steers controller routing mistakes. See
[CAMPAIGN_TODO.md](CAMPAIGN_TODO.md) and
`evidence/large-scene-p55/connected-ending.json` for evidence and qualifications.
