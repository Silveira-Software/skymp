# Original NPC resource composition — candidate only

The native reads ACBS/CNAM from the winning NPC record and resolves UseStats
and UseTraits through the existing template chain. NPC_/GMST enumeration now
supports the existing CombineBrowser winning-record interface.

`applySrpOriginalStats(refId)` accepts a loaded original reference only. It
accepts no supplied level, resource amounts, class, or encounter context. It
requires a saved, alive original identity. Fixed-level resource growth uses
the original CLAS weights and fNPCHealthLevelBonus. Non-integral distribution
is refused until AE rounding has an independent engine fixture. PC-level
scaling is refused until a persistent encounter-level producer exists.

The method preserves absolute Health/Magicka/Stamina in the same ChangeForm
transaction as the snapshot. A reduction that would clip current resources
is refused. Repeat calls compare the source snapshot and do not add growth.
The snapshot has a canonical SHA-256 checksum; the existing
`private.srpResourceMaximumsV1` format remains version 1. Reads restore those
same maxima and fractions after restart. No player progression is enabled.

Validation on 30 October 2026: 62 focused C++ cases; 393/393 full native cases;
actual addon resource/write/restart tests for five fixed-level High Fantasy
bases; expected refusal for original corpses, protected base, and PC-level
context; actual SIGKILL after save before ACK recovered identical state.
Evidence: `D:/srp-work/r38-native-gameplay-20261030/`.
Candidate addon SHA-256:
`f0c6227374ccc3ac898f2e68a5384176c7f2365bc760878413a4eeddbaf9cbf7`.

This is not a gameplay release. Engine parity remains HUMAN_PENDING. Skills,
PC-level/encounter scaling, fractional rounding, authoritative impact, original
death rewards and CELL/EZ reset are incomplete. OnHit/OnWeaponHit Managed
guards remain active. No public actor is activated. Old native readers must
not be used on newly composed candidate records: they ignore the resource
snapshot. Keep the forward-compatible candidate reader and pending records
when rolling back gamemode; PUBLIC has no records produced by this method.
