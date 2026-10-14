# Evidence snapshot before the baseline retry

This folder preserves the entire `m8-slice*` evidence set as it stood after the first fresh refactor-baseline run. That run stopped at the failed party revive assertion. Its authoritative result is `../refactor-baseline-slice.log` and `../refactor-baseline.json`.

The placement, solo, load and party files belong to that failed run. The party-travel files and aggregate `m8-slice.json` were inherited from the earlier implementation session: party travel was not executed in the failed baseline run. They are retained as historical bytes, not fresh verification evidence.

The successful fresh retry is recorded in `../refactor-baseline-confirmation-slice.log`, `../refactor-baseline-confirmation.json` and the committed top-level `m8-slice*` receipts at baseline checkpoint `873799e`.
