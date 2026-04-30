// Negative test (Check A.1): a consumer compiling this TU WITHOUT the umbrella
// header AND WITHOUT HTTPSERVER_COMPILATION must hit the inclusion-gate #error.
// The build recipe inverts exit status and greps for the gate text to ensure
// the failure is for the right reason.
#include "httpserver/webserver.hpp"
int main() { return 0; }
