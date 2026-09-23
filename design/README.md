# ACIC documentation

The project has six maintained design documents. Older experiment narratives
were consolidated on 2026-09-23 and remain available through Git history.

| Document | Use it for |
|---|---|
| [Current evidence](current-state.md) | Accepted results, recent causal findings, job IDs, raw-data pointers and claim boundaries. |
| [Configurations](configurations.md) | Source/runtime pins, build identities, machine layouts, campaign paths and accepted command-line profiles. |
| [Implementation](implementation.md) | Algorithm structure, data path, correctness invariants, source map and scale limits. |
| [Optimization ledger](optimization-ledger.md) | Keep/reject/pending status for every material optimization. |
| [Forward plan](sc27-plan.md) | IPDPS decision, priorities, stop rules and the longer SC27 path. |
| `README.md` | Entry point and documentation policy. |

The IA³@SC24 paper is retained as `acic_2024paper.pdf`. Machine-readable result
summaries remain under `design/onenode-data/`, `design/step6-data/`,
`design/step7-data/` and `design/step75-data/`. Experiment configurations and
parsers remain under `benchmarks/`; launch/build scripts remain under
`scripts/`.

## Documentation policy

- Update an existing maintained document instead of creating a new Markdown
  file for each experiment.
- Add accepted or rejected findings to `current-state.md` and update the
  optimization ledger in the same change.
- Store predictions and run parameters in the machine-readable benchmark
  configuration. Store job IDs and conclusions in the evidence table.
- Keep raw JSON and logs immutable. Git history supplies the chronological
  narrative when it is needed.
- A binary name is not an identity. Results cite its hash, application/htram
  revisions, runtime tree, machine layout and runtime arguments.
- Training sources select settings; held-out sources confirm them. Failed,
  hung, wrong and out-of-memory runs remain visible.
