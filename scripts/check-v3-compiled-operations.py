#!/usr/bin/env python3
"""Check the installed-library implementation contract for representative operations."""
import argparse
import subprocess

# These operations must have a library-owned definition even when no consumer
# instantiates them. Behavioral suites cover their semantics independently.
OPERATIONS = (
    'httpserver::http::fields::append(',
    'httpserver::server::server_options::validate(',
    'httpserver::server::resource_budget::reserve(',
    'httpserver::server::route_pattern::parse(',
    'httpserver::server::make_sync_route(',
    'httpserver::manual_executor::run_one(',
    'httpserver::detail::current_executor_slot(',
    'httpserver::detail::task_frame_base::finish(',
    'httpserver::stop_token::cancellation_awaiter::await_suspend(',
    'httpserver::detail::fire_trigger(',
    'httpserver::exchange::admit_body(',
    'httpserver::body_reader::collect(',
    'httpserver::response_writer::finish(',
    'httpserver::response_definition::owned_bytes(',
    'httpserver::send_definition(',
    'httpserver::detail::send_cursor::pull(',
    'httpserver::detail::http1_head_parser::',
    'httpserver::detail::hpack_decoder::',
    'httpserver::detail::qpack_decoder::',
    'httpserver::websocket::options::validate(',
    'httpserver::detail::http2_dispatch::post(',
    'httpserver::detail::valid_http1_host(',
    'httpserver::detail::unescape_buf_raw(',
    'httpserver::detail::owned_completion_storage::claim_read(',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library')
    args = parser.parse_args()
    result = subprocess.run(['nm', '-C', args.library],
                            check=True, text=True, capture_output=True)
    definitions = [line for line in result.stdout.splitlines()
                   if ' T ' in line]
    missing = [name for name in OPERATIONS if not any(name in line for line in definitions)]
    if missing:
        raise SystemExit('Missing compiled library operations:\n' + '\n'.join(missing))
    print('PASS representative operations have compiled library definitions')


if __name__ == '__main__':
    main()
