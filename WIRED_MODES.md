# WIRED_MODES — load-bearing run-ci modes (plan 1.6 / D7)

The strict-mocks × Part-5 merge race silently DROPPED load-bearing env/mode lines from
individual ports' `scripts/run-ci.sh` — in THIS repo it deleted the `strict_mocks_gate`
function body while the gate line calling it survived, so the nightly STRICT-MOCKS lane
died with exit 127 (round-4 finding; restored in 1fd01e9). This manifest is the
merge-coherence guard: each line below is a regex the WIRED-MODES gate
(`porting-sdk/scripts/check_wired_modes.py`) requires to be present in
`scripts/run-ci.sh`. If a future merge drops one, the gate reds instead of shipping a
vacuous strict lane.

Format (one required pattern per line): `` - `<python-regex>` — <why it is load-bearing> ``.
Prose/headers/comments are ignored, so this file doubles as human documentation.

- `MOCK_RELAY_STRICT=1` — RELAY strict mode: the RELAY mock suite re-runs with the shared mock in 400-on-violation mode (unknown field / duplicate id) so a wire-shape regression the tolerant mock would swallow fails loud (STRICT-MOCKS gate; the env is inherited by the fork+execlp'd `python -m mock_relay` child).
- `strict_mocks_gate` — nightly strict RELAY re-run body: the function the STRICT-MOCKS gate line invokes (with cpp's host/exec/run BUILD_MODE routing). This exact body was silently deleted in the strict-mocks × Part-5 merge race (call survived, body gone → nightly exit 127); this pattern pins body + call.
- `export MOCK_SIGNALWIRE_STRICT` — REST 400 strict default (D3): the REST mock returns 400 on an unknown key / wrong type instead of tolerantly journaling it, exported run-ci-wide so the TEST + REST-COVERAGE lanes catch the regression; inherited by the mocktest harness's spawned `python -m mock_signalwire`.
