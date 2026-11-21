# R49 — shader-only controller classification

The real `CandleLanternwithCandle01.nif`, SHA-256
`af936b61381d3771495a547e6f079848b41a84e26cd493dcbf7829cf0ce89f65`,
was refused as an unbounded spatial animation. Its root manager owns a
lighting-shader float sequence. Its multi-target transform controller has
only null extra targets. There is no transform-key sequence.

Layouts were checked against [niftools/nifxml
292bb9403cbf4052c58d66e80906b6bde1700779](https://github.com/niftools/nifxml/blob/292bb9403cbf4052c58d66e80906b6bde1700779/nif.xml).
The reader recognizes this BS100 graph only after checking manager links,
controlled-block class strings, null transform targets, float interpolator
and key data, the shader-property target, manager-controlled blend type and
the observed controlled variable 11. Other graphs retain the existing
spatial-animation bounds and rejection. NIF strings are retained to validate
the class declarations instead of trusting block indices alone.

The baseline reproduces the refusal in the real asset and a synthetic graph.
The corrected reader passes 33 producer/controller/load-order tests, including
the real pinned asset. Its collision triangles, volumes and margin equal a
control with only the root controller pointer detached. The shipped asset
is not modified or checked into this repository. Negative cases cover
spatial targets, transform interpolators, wrong class strings/links, malformed
sequence data and nonfinite float keys.

Run from `tools/srp-collision-scene`:

```powershell
$env:SRP_R49_LANTERN_NIF='<local owned CandleLanternwithCandle01.nif>'
python -m unittest discover -p 'test_*.py' -v
```

This producer correction does not authorize combat in Bthardamz01a. Its book,
chest and door still have untracked movable physics. Their live poses remain
unsupported. The R14 binary and human claw/reconnect acceptance are unchanged;
feeding remains pending and PUBLIC has not been updated.
