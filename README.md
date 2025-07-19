# search-index-library

High‑performance, embeddable C components for **building, storing, and querying compact inverted indexes** with optional **term positions, per‑term values, document metadata, and int8 embedding vectors** (e.g. quantized semantic embeddings). Designed for low‑latency in‑memory or memory‑mapped search workloads (log search, feature stores, hybrid lexical + vector search) with tight control over allocations via a custom pool allocator.

> **License:** Apache-2.0
> **Copyright:** 2023‑2025 Andy Curtis; 2024‑2025 Knode.ai

---

## Key Concepts

| Concept                                     | Description                                                                                                                          |
| ------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| **Document Image** (`sil_document_image_t`) | Decoded, in-memory view of a single serialized document (terms, positions, values, content bytes, embeddings).                       |
| **Search Image** (`sil_search_image_t`)     | Memory‑mapped / loaded global index file containing many documents’ global headers + concatenated term posting lists.                |
| **Term / Posting** (`sil_term_t`)           | Iterator over sorted document IDs for a term. Optionally exposes: value (uint32), positions (delta encoded), and BM25 / IDF helpers. |
| **Term Set** (`sil_term_set_t`)             | Query term collection with frequencies & precomputed stats (idf, qtf, etc.).                                                         |
| **Snippets**                                | Utilities to build dense, diverse text windows from weighted term occurrences.                                                       |
| **Embeddings**                              | Per document (or record) int8 vectors (e.g. PQ / scalar quantized) retrievable via `sil_search_image_embeddings()`.                  |

---

## Features

* Compact **bit‑packed posting groups** (10‑bit local IDs + flag bits) with adaptive integer & position length encoding.
* Optional per‑posting **value field** (1/2/4 bytes) and **positions** (delta + variable high‑bit continuation scheme).
* Support for **term wildcards** (prefixed variants) during index build helpers.
* **BM25 / BM25+** helpers + **spread / proximity scoring** primitives (see inline statics in `sil_term_impl.h`).
* **Hybrid search ready**: attach embedding vectors per document for downstream reranking / ANN.
* **Memory pool** (`aml_pool`) friendly: predictable allocations, fast teardown.
* **Composable**: separate *builder* (write path) and *image / iterator* (read path) APIs.

---

## Directory / API Overview

| Header                   | Responsibility                                                                               |
| ------------------------ | -------------------------------------------------------------------------------------------- |
| `sil_document_builder.h` | Build a single serialized document blob (terms + positions + values + embeddings + content). |
| `sil_document_image.h`   | Decode / inspect a document blob; match & enumerate its terms.                               |
| `sil_search_builder.h`   | Aggregate many documents (global records) into one search image file.                        |
| `sil_search_image.h`     | Open & query a persisted search image; fetch global metadata, content, embeddings, terms.    |
| `sil_term.h` (+ `impl`)  | Public term iterator struct & inline position decoding.                                      |
| `snippets.h`             | Rank and select top text snippets from term occurrence lists.                                |
| `impl/sil_constants.h`   | Internal flag & encoding constants.                                                          |

---

## Memory / Encoding Notes

Posting lists are grouped into **“small groups”**: a 16‑bit control word (or 8‑bit variant for doc image) packs flags + a 10‑bit delta document ID. Flags signal presence & byte length of value and positional payload blocks. Positions are stored as first-position base + variable length deltas (high‑bit continuation). This keeps common cases (single occurrence, small docID delta, small value) to a few bytes.

---

## Quick Start

### 1. Build a Document Blob

```c
#include "search-index-library/sil_document_builder.h"
#include "a-memory-library/aml_buffer.h"

sil_document_builder_t *b = sil_document_builder_init();
// Add plain terms
sil_document_builder_term(b, "hello");
// Add term with explicit position
sil_document_builder_term_position(b, 0, "world");
// Add term with a numeric value (e.g. field boost)
sil_document_builder_term_value(b, 42, "scoretag");
// Wildcard term variant capture (stores term + prefix wildcard)
sil_document_builder_wterm(b, 0, "prefixed");

aml_buffer_t *bh = aml_buffer_init(1024);
int8_t emb[384]; /* fill with quantized embedding bytes */
const char *content = "Hello world example content";
uint32_t content_len = (uint32_t)strlen(content);
uint32_t local_id = 1234; // first 4 bytes of d define local/document id
sil_document_builder_global(b, bh, emb, 384, content, content_len,
                            &local_id, sizeof(local_id));
```

The buffer `bh` now contains a serialized document suitable for:

* **Direct per-document search** via `sil_document_image_t`
* Feeding into the **search builder** to aggregate into a multi-doc index.

### 2. Inspect a Document

```c
#include "search-index-library/sil_document_image.h"

sil_document_image_t img; // no dynamic alloc inside
sil_document_image_init(&img, bh->data, bh->length);

aml_pool_t *pool = aml_pool_init(4096);
uint32_t num_terms; char **terms = sil_document_image_terms(pool, &img, &num_terms);
for (uint32_t i=0; i<num_terms; i++) {
    sil_term_t *t = sil_document_image_term(&img, pool, terms[i]);
    if (!t) continue;
    while (t->c.advance((atl_cursor_t*)t)) { // advance via embedded cursor
        sil_term_decode_positions(t);
        // use t->term_positions..term_positions_end
    }
}
aml_pool_destroy(pool);
```

### 3. Build a Search Image (Multi‑Document Index)

```c
#include "search-index-library/sil_search_builder.h"

sil_search_builder_t *sb = sil_search_builder_init("index.sil", 1<<20); // internal buffer size
// For each document blob (constructed similarly to above) call:
//   sil_search_builder_global(sb, emb, emb_len, content, content_len, &local_id, sizeof(local_id));
// Then enumerate its terms & positions calling the appropriate term / wterm / *_position / *_value
// builder functions to append postings.
// Flush & destroy when done:
sil_search_builder_destroy(sb);
```

*(Exact write/flush semantics depend on implementation details not shown here; typically destruction finalizes the file.)*

### 4. Query a Search Image

```c
#include "search-index-library/sil_search_image.h"

sil_search_image_t *si = sil_search_image_init("index.sil");
uint32_t max_id = sil_search_image_max_id(si);

uint32_t header_len; // not number of docs; length of returned header region
const sil_global_header_t *gh = sil_search_image_global(&header_len, si, 1234); // doc id
const char *content = sil_search_image_content(si, gh);
const int8_t *emb = sil_search_image_embeddings(si, gh);

aml_pool_t *pool = aml_pool_init(4096);
sil_term_t *term = sil_search_image_term(si, pool, "hello");
while (term && term->c.advance((atl_cursor_t*)term)) {
    sil_term_decode_positions(term);
    // scoring logic here
}
aml_pool_destroy(pool);

sil_search_image_destroy(si);
```

### 5. Snippets (Highlight Windows)

Collect weighted term occurrences into an array of `snippet_position_t`, call:

```c
#include "search-index-library/snippets.h"

size_t n = snippet_position_sort(positions, num_positions);
snippets_create(bh, positions, n, /*max_snippet_tokens*/ 40);
// Access generated snippet_t array inside buffer `bh` (implementation dependant helper may exist).
```

Then sort / pick top windows:

```c
snippets_sort(snips, num_snips);
size_t top = snippets_top(snips, num_snips);
```

---

## Scoring Helpers (Inline)

Inline functions in `sil_term_impl.h` expose reusable primitives:

* **IDF / QTF weighting:** `sil_term_idf`, `sil_qtf_weight`, `sil_idf_qtf`.
* **BM25 / BM25+:** `sil_bm25_doc_norm`, `sil_bm25_tf`, `sil_bm25_plus_tf`, `sil_bm25_plus_tf_spread`, `sil_bm25_plus_score`.
* **Spread & proximity:** `sil_term_spread_score`, `sil_pair_proximity`.
  Use these to assemble custom ranking functions (lexical only or hybrid with embedding similarities).

---

## Dependencies

* **a-memory-library** (pool + buffer abstractions)
* **a-tokenizer-library** (token + cursor types for iteration / parsing)
  Include paths assume sibling repositories or installed headers.

---

## Error Handling

Functions generally return pointers (NULL on failure) or void (assumed success). Iterator `advance` returns boolean (non‑zero for success). Ensure file existence & memory mapping success before querying `sil_search_image_t`.

---

## Performance Tips

* Reuse an `aml_pool_t` per query; destroy afterward for O(1) cleanup.
* Decode positions only when needed (`sil_term_decode_positions`).
* Use `advance_to` (if provided via cursor implementation) for skipping.
* Precompute per‑document normalization (BM25) once per matched document.

---

## Roadmap Ideas (Suggestive)

* Signed term values (see TODO in impl for encoding).
* SIMD accelerated position decoding / proximity.
* Parallel index build & merge utilities.
* Optional on‑disk sparse vector / ANN integration.

---

## Contributing

1. Fork & branch.
2. Add tests / benchmarks.
3. Submit PR referencing any related issue.

---

## License

Apache-2.0. See SPDX headers in source files.

---

## Minimal Example (Putting It Together)

```c
// Pseudocode: index a few docs then run a one-term query
sil_search_builder_t *sb = sil_search_builder_init("example.sil", 1<<18);
for(each input_doc) {
  // build doc with sil_document_builder_* OR directly call search_builder_* helpers
  sil_search_builder_term(sb, "example");
  // ... add more terms, positions, values
  sil_search_builder_global(sb, emb, emb_len, content, content_len, &doc_id, sizeof(doc_id));
}
sil_search_builder_destroy(sb);

sil_search_image_t *si = sil_search_image_init("example.sil");
aml_pool_t *pool = aml_pool_init(4096);
sil_term_t *t = sil_search_image_term(si, pool, "example");
while(t && t->c.advance((atl_cursor_t*)t)) { sil_term_decode_positions(t); /* score */ }
aml_pool_destroy(pool);
sil_search_image_destroy(si);
```

---

**Questions / Issues?** Open an issue or contact maintainer.
