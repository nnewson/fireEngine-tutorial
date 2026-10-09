# Historical report fixtures, not Stage 7 measurements

- `lavapipe-one-auto.txt`: Nick's successful laptop qualification, reviewed
  October 8, 2026. Release, Mesa 25.2.8, automatic one participant, one draw.
- `kosmickrisp-two.txt`: the restored Stage 6 overlap check supplied by Nick
  on October 6, 2026. Release, KosmicKrisp 1.4.363, forced two, 10,000 draws.

Both are unchanged application stdout, retained to test the parser against real
reports rather than only fixtures synthesized from its own schema. Neither is
an arm in a Stage 7 session. Other tests generate explicitly synthetic reports,
including the direct path, to exercise arithmetic and failure controls.
