## 7) Feature availability and runtime fallbacks

| Build flag | When disabled | Public-API behavior |
|---|---|---|
| `HAVE_BAUTH` | Basic-auth disabled | `get_user`, `get_pass` return empty `string_view`; `webserver::features().basic_auth == false`; `create_webserver::basic_auth(true)` throws `feature_unavailable` at `webserver` construction time (consistent with other feature flags) |
| `HAVE_DAUTH` | Digest-auth disabled | `get_digested_user` returns empty; `check_digest_auth` returns a sentinel result; features().digest_auth == false |
| `HAVE_GNUTLS` | TLS disabled | All `get_client_cert_*` return empty / -1 / false; features().tls == false; `create_webserver::use_ssl(true)` throws `feature_unavailable` |
| `HAVE_WEBSOCKET` | WebSocket disabled | `register_ws_resource` throws `feature_unavailable`; features().websocket == false |

`feature_unavailable` derives from `std::runtime_error` (PRD-FLG-REQ-005). Its `what()` names both the feature and the build flag (PRD-FLG-REQ-004).

---
