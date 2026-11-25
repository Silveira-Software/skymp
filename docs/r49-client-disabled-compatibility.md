# R49 — canonical disabled state in the client

The native initial property message serializes `isDisabled`, while the client
read only `disabled` at creation and updates. R11 reads the canonical property
first, preserves explicit false, and accepts the legacy spelling at both sites.
The existing ModelApplyUtils and its social enable guard are unchanged.

The private bundle patch changes exactly two sites of R10 (39897b7f) and
reverses byte for byte. Tests execute the actual compiled RemoteServer and
ModelApplyUtils classes: canonical and legacy create/update, explicit false,
unrelated updates and the existing enable guard all pass. R10 reproduces the
missing disable. The compiled SendInputs constructor also passes.

R11 bundle SHA-256:
`be2f9eb2694be974d6db2710a3e9d7d821a6a3cd1b17c32a6bb17902d6dd26c8`.
Manifest: `8451f5287b4355ef1241379e4fc6c1fba8adf71f0114095afd2fccaab5f0b189`.
The 37 other packages and the platform DLLs are preserved. Actual engine
projection of the three disabled Bthardamz objects is still pending. This
change does not establish human feeding or authorize PUBLIC publication.

The subsequent human entry confirmed disabled state for the chest and book,
but the door retained its enabled state on its separate full FormView path.
R12 addresses that path; see r49-client-door-projection.md. The earlier six
tests covered the static optimization and did not exercise the real door
predicate. Three-object engine projection has not passed yet.
