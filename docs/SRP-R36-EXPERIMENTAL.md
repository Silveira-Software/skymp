# SRP R36 — experimental authoritative attributes

RELEASE_BLOCKED. No production deployment or native replacement authorized.

Based on PR #4 / `0aa78d6c427818f0903c3d976ff4e4367726c4d8`, with the
additional ActionListener hardening preserved from the archived authorized
build source. That source archive is SHA-256
`f944dbd02cbb7a32dda982f13967324a9933df867a5b5d0d418e6f07974b6ba5`.
It is not identical to fork HEAD; disconnected/stale host and invalid opId
guards were present in the actual build source and must not be dropped.

Experimental addon SHA-256:
`811b604b8b09a545ef644f2751f05708335b26eb1d81f07b7161367d0bd33969`.
Built with the project's existing clang20/Ninja Release toolchain image,
offline, without replacing the authorized public addon. The binary is held
in the local OFFLINE review bundle, not committed as a distributed release.

Adds a character/profile/revision-scoped `srpActorStats` projection whose
private envelope shares a ChangeForm mutation with resource percentages.
Ranks must match the server-owned `srpProgression`; supplied bonus values
are ignored. The caller must wait for the existing saved-generation ACK.
Duplicate revisions are idempotent, and foreign projections grant nothing.

Health/magicka/stamina maxima, weapon/ranged/spell damage, SRP resistance
and TES5 armor use the server projection. Papyrus resource reads/healing
use the composed maxima. Regeneration fields are preserved; effect removal
does not erase the SRP maxima or heal a damaged actor. The companion
gamemode implements server carry and ACK delivery after durability.

Validation on this addon: 6 new C++ cases, 42 assertions; 68 selected C++
regression cases including those six; 60 SIGKILL generation-ACK runs with
zero ACK ahead of disk; 10 real-addon/real-ESM groups using two synthetic
bots, plus four fresh-process restart/rollback/identity checks. The complete
incremental R35 gamemode passed six additional checks using synthetic
sessions/API responses and this exact addon. These are not human Skyrim tests.
Nine additional combined Mongo/native allocation crash boundaries passed with
real SIGKILL, recovery and repeated opId: one receipt, one point debit, one
projection. No production database or world was used.

The server/libespm/unit source closure was recovered from the archived build:
529 C++/header files now match the compiled source after CRLF normalization.
See SRP-R36-BASELINE-RECOVERY.json for the 75 recovered preexisting files.
Seven Windows-client/client-config differences outside the Linux native build
were left untouched; no Windows native client binary was built here.

General temporary maximum/resistance composition, engine-side carry,
six-attribute UI/human
acceptance remain blockers. Magic cost/movement/survival-decay formulas
remain preview-only. Point allocation remains disabled. No claim of complete
vanilla magic, supernatural transformations or release readiness is made.

Companion code and evidence: SkyrimRP PR #58 base, branch
`codex/progression-native-r36-20261027`, `docs/progression-r36/`.
PUBLIC 7777 remains R35; 7778/7789 were not started.
