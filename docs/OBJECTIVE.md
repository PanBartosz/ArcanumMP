# Arcanum campaign multiplayer: acceptance contract

The objective is an entire Arcanum campaign played by at least four independently
controlled characters over a LAN, with Linux/Windows cross-play. A successful
lobby, movement demo, or opening quest is not completion.

Required release evidence:

1. Native Linux and Windows game executables built from the same protocol and
   content definitions; mixed four-process sessions may use Windows binaries
   under Wine. Delivery is drop-in executables and required libraries.
2. An original-style lobby reachable from the main menu, with host/join, useful
   errors, character creation, chat, ready state, host controls, content checks,
   departure/rejoin, and persistent character ownership.
3. A single authoritative campaign simulation; all four players can move, fight,
   cast, use skills, equip, trade, loot, craft, interact with NPCs, and gain levels.
   Turn-based and real-time combat need explicit policy and verification.
4. The initiating player owns conversation choices. Everyone can observe text,
   choices and outcomes, including script-initiated dialogue and cutscenes.
5. Shared scripts, flags, quests, journals, reputation policy, XP policy, followers,
   time, rest, world travel, area transitions, resurrection and ending sequences.
6. Atomic campaign save/load with all player characters and stable ownership;
   disconnect/rejoin, host loss, and interruption behavior tested.
7. Original campaign content retained. Traversal and quests verified through all
   major story regions and the ending, plus representative alternate branches.
8. Meaningful automated engine/network tests, fault injection, prolonged sessions,
   actual gameplay screenshots, and documented campaign acceptance using original
   content and ordinary player controls. Native automation and manual review may
   be combined; neither source coverage nor a controlled finale alone proves this.
9. Reproducible builds, installation, user guide, architecture, known limitations,
   provenance and license notices. No proprietary game assets in the repository.

Work can be incremental. Every requirement remains open until supported by
appropriate runtime evidence. Do not report stubbed or simulated systems as done.

Acceptance methods must support the scope of the claim. An uninterrupted manual
run, fixed-duration bot session or exhaustive permutation matrix is not itself a
product feature. The full campaign scope remains.

## Permanent scope exclusions — user instruction, 2026-10-26

**No installers. No Windows VMs. No testing on real Windows.** These activities
are excluded, not deferred: they contribute zero toward completion and cannot
be release gates or remaining requirements. Deliver Linux and Windows drop-in
executables with their required libraries. Retain accurately labelled Wine
results. This instruction supersedes conflicting historical notes and may only
be changed by an explicit later user instruction.
