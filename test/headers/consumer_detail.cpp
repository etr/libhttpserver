// Negative test (Check A.2): a consumer including a detail header directly,
// even when _HTTPSERVER_HPP_INSIDE_ is defined (simulating the umbrella state),
// must hit the gate when HTTPSERVER_COMPILATION is not defined.
//
// NOTE: pre-Phase-3 the detail gate is dual-mode (accepts either macro), so
// this TU defines _HTTPSERVER_HPP_INSIDE_ to exercise the strictest
// post-cleanup behavior. After TASK-014 lands the PIMPL split, the gate may
// drop the _HTTPSERVER_HPP_INSIDE_ acceptor altogether; this test should keep
// passing because the consumer-style invocation also lacks HTTPSERVER_COMPILATION.
//
// For TASK-002 we keep the dual-mode gate (per the plan's Phase 3a-i), so this
// TU is built WITHOUT defining _HTTPSERVER_HPP_INSIDE_ — the detail gate then
// fires for the same reason as A.1.
#include "httpserver/details/http_endpoint.hpp"
int main() { return 0; }
