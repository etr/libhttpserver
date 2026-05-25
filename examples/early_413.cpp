// Demonstrates the solution to issue #273: short-circuit large uploads
// with a 413 BEFORE any body bytes are consumed.
//
// Register a `request_received` hook that inspects Content-Length; if
// it exceeds the configured cap, return a respond_with(413) action.
// libhttpserver aborts the upload -- the resource handler is not
// invoked and the body bytes never cross the daemon's I/O boundary.

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <httpserver.hpp>

namespace hs = httpserver;

namespace {
constexpr std::size_t kMaxUploadBytes = 1 * 1024 * 1024;   // 1 MB
}  // namespace

class upload_resource : public hs::http_resource {
 public:
    hs::http_response render_post(const hs::http_request&) override {
        return hs::http_response::string("UPLOAD OK");
    }
};

int main() {
    hs::webserver ws{hs::create_webserver(8080)};

    auto h = ws.add_hook(hs::hook_phase::request_received,
        std::function<hs::hook_action(hs::request_received_ctx&)>(
            [](hs::request_received_ctx& ctx) {
                std::string cl{ctx.request->get_header("Content-Length")};
                if (cl.empty()) return hs::hook_action::pass();
                try {
                    if (std::stoull(cl) > kMaxUploadBytes) {
                        return hs::hook_action::respond_with(
                            hs::http_response::empty().with_status(413));
                    }
                } catch (...) {
                    // Malformed Content-Length: let the normal pipeline
                    // produce a 400 elsewhere.
                }
                return hs::hook_action::pass();
            }));

    auto resource = std::make_shared<upload_resource>();
    ws.register_path("/upload", resource);

    ws.start(true);   // blocking
    return 0;
}
