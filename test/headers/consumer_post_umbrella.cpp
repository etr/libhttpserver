// Negative test (Check A.4): including the umbrella must NOT leak the
// _HTTPSERVER_HPP_INSIDE_ macro to subsequent translation-unit-scope code.
// A consumer doing `#include <httpserver.hpp>` followed by a direct include
// of a public header must STILL hit the gate. This catches the bug where the
// umbrella defines _HTTPSERVER_HPP_INSIDE_ but does not #undef it.
#include <httpserver.hpp>
#include "httpserver/webserver.hpp"
int main() { return 0; }
