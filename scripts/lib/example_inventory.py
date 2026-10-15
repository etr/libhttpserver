"""Canonical runnable native examples (legacy v2 sources are historical)."""
SOURCES = {name: 'examples/' + name + '.cpp' for name in (
    'routes', 'request_data', 'body_streaming', 'auth',
    'forms', 'response_sources', 'operations', 'lifecycle',
    'protocol_options', 'external_event_loop', 'websocket_echo',
    'http2', 'http3', 'tls_profiles')}
SOURCES['tls_rotation'] = 'test/examples/tls_rotation.cpp'

HEADERS = ('example_host.hpp', 'example_reply.hpp', 'example_ws.hpp',
           'host_poll.hpp', 'route_handlers.hpp', 'tls_unavailable.hpp')
