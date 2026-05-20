// Copyright 2026 Sebastiano Merlino
// libhttpserver hello-world example — the lambda form (PRD §3.4).
// Compiles in ten lines including main(), with no http_resource subclass
// and no raw-pointer ownership. Production code typically qualifies names
// explicitly; the `using namespace` here is a one-off so this file can
// document the shortest possible end-to-end demo. See shared_state.cpp
// for the class-based pattern that is appropriate when handlers must
// share mutable state.
#include <httpserver.hpp>
using namespace httpserver;  // NOLINT(build/namespaces) - keep the demo at <=10 LOC
int main() {
    webserver ws{create_webserver(8080)};
    ws.on_get("/hello", [](const http_request&) {
        return http_response::string("Hello, World!");
    });
    ws.start(true);
}
