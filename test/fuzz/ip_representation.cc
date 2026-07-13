#include <httpserver.hpp>

using namespace httpserver;
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (!size)
		return 0;
	std::string str(reinterpret_cast<const char *>(data), size);
	try {
		http::ip_representation test(str);
	} catch (std::exception &e) {
		return 0;
	}
	return 0;
}
