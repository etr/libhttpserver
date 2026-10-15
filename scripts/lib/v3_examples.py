"""Canonical runnable native examples (legacy v2 sources are historical)."""
SOURCES = {name: 'examples/' + name + '.cpp' for name in (
    'v3_routes', 'v3_request_data', 'v3_body_streaming', 'v3_auth',
    'v3_forms', 'v3_response_sources', 'v3_operations', 'v3_lifecycle',
    'v3_protocol_options', 'v3_external_event_loop', 'v3_websocket_echo',
    'v3_http2', 'v3_http3', 'v3_tls_profiles')}
SOURCES['v3_tls_rotation'] = 'test/examples/v3_tls_rotation.cpp'
