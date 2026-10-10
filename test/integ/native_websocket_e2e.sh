#!/bin/sh
set -eu
# The default gate uses a stdlib-only independent wire implementation.
# The external websockets client is a separate explicitly provisioned lane.
python3 "$(dirname "$0")/native_websocket_client.py" ./native_websocket_fixture
exec python3 "$(dirname "$0")/native_websocket_regression.py" ./native_websocket_fixture
