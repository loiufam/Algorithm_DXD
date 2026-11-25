# Improved BFS design and unit-test report

## Scope

This implementation is isolated under `test/improved_bfs.*` and is not
connected to `HybridDXD`. The production search path is unchanged.

## Algorithm

For each active column, rows are already ordered by row id:

1. Run one DSU `find` per row in the column.
2. Bucket rows by their pre-column DSU root.
3. Connect the distinct roots with `q - 1` tree edges.
4. All other pairs in the column are non-tree pairs.
5. Deduplicate tree edges globally; different DSU roots cannot share a
   previously classified edge.

For small matrices, non-tree edges are materialized and compared exactly
with a pair-by-pair DSU reference. For large matrices, non-tree edges use a
compressed bucket:

```text
nonTreeOccurrences(column) = C(k, 2) - (q - 1)
```

where `k` is the number of rows in the column and `q` is the number of
distinct DSU roots before processing it. Each bucket stores the column rows
and its `q - 1` selected tree edges.

The classification is output-sensitive:

```text
time = O(N + P + M alpha(V))
```

- `N`: nonzero matrix entries
- `P`: actual row pairs in columns, `sum(C(k_c, 2))`
- `M`: selected tree edges

There are only `N` DSU finds, instead of two `find` operations for each of
the `P` pairs. Large data uses compressed non-tree buckets, so `P` does not
become an explicit edge-memory allocation.

## Correctness tests

The unit test executable is `build/test_improved_bfs`.

It verifies:

- components from the improved scan equal incidence-matrix BFS components;
- explicit tree/non-tree edges equal the pair-by-pair DSU reference;
- tree edges contain no cycle;
- tree edge count equals `V - components`;
- explicit tree/non-tree sets are disjoint and cover every unique projected
  edge;
- component row/column sets partition the active matrix;
- compressed bucket counts, members, and tree edges are consistent.

Results:

- 3 deterministic tests passed;
- 300 randomized matrices passed;
- randomized tests included duplicate cross-column edges, dense columns,
  and disconnected components.
- one real matrix (`grafo10269.99`, 3,083,482 candidate pairs) was tested in
  explicit mode: components, all 1,792 tree edges, and all 389,474 unique
  non-tree edges matched the reference exactly.

## Large benchmarks

Dataset: all 20 matrices under
`GMLToMat/experiments/test/matrices`.

| Metric | Result |
|---|---:|
| Cases | 20 |
| Nonzeros | 20,658,247 |
| Candidate row pairs | 13,187,233,390 |
| Components | 1,124 |
| Tree edges | 3,609,647 |
| Compressed non-tree buckets | 16,716 |
| Non-tree pair occurrences | 13,183,623,743 |
| Incidence BFS total | 714.4 ms |
| Improved scan total | 907.3 ms |

Per-case incidence BFS time was 31.3-41.8 ms. Improved scan time was
40.1-50.3 ms. The improved scan is about 20-29% slower because it also
performs DSU classification and retains tree edges and non-tree buckets.

Materializing the projected graph would require generating 13.19 billion
row pairs in aggregate. That naive approach was not run.

Representative cases:

| Case | Nonzeros | Candidate pairs | Components | Tree edges |
|---|---:|---:|---:|---:|
| `grafo11433.92` | 915,162 | 526,699,158 | 60 | 162,336 |
| `grafo10358.95` | 1,085,160 | 689,041,620 | 60 | 189,630 |
| `grafo8855.100` | 1,124,045 | 772,399,955 | 50 | 194,135 |

The exact real-matrix check used `grafo10269.99`:

| Metric | Result |
|---|---:|
| Nonzeros | 20,732 |
| Candidate pairs | 3,083,482 |
| Components | 6 |
| Tree edges | 1,792 |
| Unique non-tree edges | 389,474 |
| Incidence BFS | 4.39 ms |
| Improved + explicit classification | 60.00 ms |

## Important limitation

`nonTreeOccurrences` counts pair occurrences across column buckets. If the
same row pair appears in multiple columns, it is represented once in each
column bucket. Therefore the compressed large-case total is not necessarily
the number of unique projected non-tree edges.

Exact unique tree/non-tree sets are verified in materialized mode for small
matrices. A unique large-scale `E_N` count requires a separate exact
deduplication design and should not be inferred from
`nonTreeOccurrences`.
