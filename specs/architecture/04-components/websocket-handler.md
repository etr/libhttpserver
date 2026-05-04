### 4.5 `websocket_handler`

**Responsibility:** Per-endpoint WebSocket protocol handler — `on_open`, `on_message`, `on_close`, etc.

**Implementation:** Public abstract base, unchanged from v1 in shape. v2.0's only change is ownership: `register_ws_resource(path, unique_ptr<websocket_handler>)` and the `shared_ptr` overload replace v1's raw-pointer registration. Lambda-first registration is **not** added (websockets are inherently stateful; the class form is the right shape).

**Related requirements:** PRD-HDL-REQ-003, PRD-HDL-REQ-005.

---
