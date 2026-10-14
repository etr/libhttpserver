#!/usr/bin/env python3
"""Execute or independently validate the two packaged release capability families."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent / 'lib'))
import v3_native_benchmark as bench
import v3_release_package_adapters as adapter


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--family', choices=(adapter.PUBLIC, adapter.BENCHMARK), required=True)
    for name in ('archive', 'source', 'build', 'prefix', 'provider-prefix', 'quic-go', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('archive-sha256', 'compiler', 'client-python', 'node', 'run-id'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--tls', choices=('yes', 'no'), required=True)
    parser.add_argument('--validate', type=Path)
    parser.add_argument('--require-full-workload', action='store_true')
    for name, default in bench.DEFAULTS.items():
        parser.add_argument('--' + name.replace('_', '-'), type=int, default=default)
    args = parser.parse_args()
    workload = {key: getattr(args, key) for key in bench.DEFAULTS}
    adapter.check_workload(workload, args.require_full_workload)
    if args.validate:
        expected = adapter.identity(args, workload)
        validate = adapter.validate_public_receipt if args.family == adapter.PUBLIC else adapter.validate_benchmark_receipt
        if args.family == adapter.PUBLIC: validate(args.validate, expected, args.run_id)
        else: validate(args.validate, expected, args.run_id, args.require_full_workload)
        print('VALIDATED', args.family, args.run_id)
    elif args.family == adapter.PUBLIC:
        adapter.run_public_capabilities(args, args.output, args.run_id)
    else:
        adapter.run_native_benchmarks(args, args.output, args.run_id, workload)


if __name__ == '__main__': main()
