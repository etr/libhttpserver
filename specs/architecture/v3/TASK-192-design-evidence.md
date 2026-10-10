# TASK-192 interface decision and doubts

Constraints: owned byte-transparent cookie pairs, no v2 classes, ordinary ordered fields, libhttpserver outcomes, one mandatory render validation boundary. Inspection finds no reusable public `result<T>`; existing HTTP helpers use optional or named result records.

| Alternative | Depth | Locality | Seam |
|---|---|---|---|
| Validated class with fluent setters and static parsing | Hides validation but many entry points | Every setter plus raw parse bypass | Raw input needs a second state representation |
| Owned aggregate plus explicit parse/render | Two operations hide tokenization, validation and date formatting | All emission checks in one renderer | Matches callers composing ordered fields |
| Field adapter parsing/adding Cookie/Set-Cookie automatically | Convenient repeated fields, wider interface | Couples cookie and field policies | Hides allocation and application failure decisions |

Choose owned `http::cookie`, `parse_cookie_header(string_view) -> vector<cookie>` and `render_set_cookie(cookie, string&) -> outcome`. The output reference follows atomic state replacement: errors leave output unchanged. Raw construction/mutation is explicit; validation is deferred until rendering, including parsed values. Applications append a successful value once as an ordinary Set-Cookie field. This deliberately migrates v2 fluent setters/throwing exceptions, not preserved wire semantics. No generic result framework or adapter.

Decision to refute: a single renderer can safely validate every raw construction path without losing lenient parser behavior.

| Doubt | Discriminator/clearing evidence required |
|---|---|
| Borrowed input outlives parsing | Destroy/overwrite original input then render owned copies |
| Parsed CR or mutable attributes bypass safety | Explicit CR/name preconditions; typed failure and unchanged sentinel for CR/LF/NUL/semicolon and comma attributes |
| Whitespace/quotes change legacy parsing | Legacy code inspection: trim only SP/HT at token/name ends; retain value-leading whitespace, first equals, strip exactly one matching quote pair |
| Date overflows or depends on locale/timezone | C++20 civil date conversion with bounded years 0000..9999; epoch, negative second, leap day, extrema and int64 overflow tests; fixed English spelling |
| Unrelated executable proves named parity | Command path/suite match and real binary SHA-256, native positive execution, adversarial absent/mismatched/wrong-case/count/skip/completion fixtures |
| Allocation grows beyond input | Linear scan, one owned pair per nonempty input token; no cache/global state; input remains governed by existing field budgets |
| Reflection creates another wire message | Independent raw TCP managed/external consumer checks exact repeated response fields, rejected attempted injection and one response |

Rollback: revert the additive helper/registration before v3 API freeze. No persistent state changes. All interface doubts are cleared by the named native cases, real positive/provenance runner contracts, four lane receipts and installed managed/external raw-wire consumers in [implementation evidence](TASK-192-implementation-evidence.md). Ownership, injection, formatting and storage discriminators have no unresolved result.
