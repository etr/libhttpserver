// Positive control (Check A.3): a consumer including only the umbrella header,
// without HTTPSERVER_COMPILATION, must compile cleanly. This proves the umbrella
// path is the supported entry point.
#include <httpserver.hpp>
int main() { return 0; }
