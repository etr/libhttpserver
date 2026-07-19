# libhttpserver v2.0 — architecture docs

Architecture references for the v2.0 (`feature/v2.0`) codebase. Every topic is a Markdown page (Mermaid + tables, renders inline on GitHub); the two spatial diagrams additionally have a rich, self-contained HTML companion (no external assets, light/dark aware) for the full colour-coded detail.

### Diagrams

| Page | Rich companion | What it shows |
|---|---|---|
| [**class-map.md**](class-map.md) | [`class-map.html`](class-map.html) | `webserver` → `webserver_impl` composition root (5 state collaborators + 7 behavior services + MHD adapter), per-request state, `response_body` hierarchy, filesystem layout |
| [**request-flow.md**](request-flow.md) | [`request-flow.html`](request-flow.html) | the MHD callback spine, body-accumulation loop, WebSocket branch, four-tier route resolution, and where the 11 hook phases fire |

### Deep dives

| Page | For | Covers |
|---|---|---|
| [**threading.md**](threading.md) | contributors | DR-008 concurrency contract, full mutex inventory, lock ordering, Helgrind lane |
| [**errors.md**](errors.md) | contributors | DR-009 propagation, every 4xx/5xx origin, the handler-exception path, config knobs |
| [**hooks.md**](hooks.md) | API users | the 11 phases, context fields, short-circuit vs observe, `hook_handle`, recipes |
| [**features.md**](features.md) | packagers / API users | `HAVE_*` detection, gated symbols, `features()`, `feature_unavailable`, build matrix |

> **Colour language** (shared by the two diagrams): composition-root = blue · state collaborator = amber · behavior service = teal · MHD C-ABI adapter = purple · domain / value type = slate.

## Keeping these current

These describe `feature/v2.0` as of the DR-014 decomposition. They are **hand-authored, not generated** — when the composition root's collaborators, the callback spine, the route tiers, or the hook phases change, update the affected Markdown page and, for the two diagrams, its `.html` companion (and redeploy the live Claude artifact to keep the shared link in sync).
