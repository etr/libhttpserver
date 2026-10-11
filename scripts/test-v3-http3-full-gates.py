#!/usr/bin/env python3
"""Build selection preserves every required full-gate suite and fixture."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('full_gates', Path(__file__).with_name('run-v3-http3-full-gates.py'))
GATES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATES)


class NativeBuildSelection(unittest.TestCase):
    def test_source_first_and_all_automake_program_selections_are_bounded(self):
        commands = []
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / 'build'
            build.mkdir()
            (build / 'config.h').write_text('#define NATIVE_V3_TLS 1\n')
            with patch.object(GATES, 'checked', side_effect=lambda command, *_: commands.append(command) or {}), \
                    patch.object(GATES, 'run_native_suites', return_value={}), \
                    patch.object(GATES, 'build_configuration', return_value={}), \
                    patch.object(GATES, 'evidence_tree', return_value=[]):
                GATES.native_stage(build, GATES.ON_SUITES, root / 'stage', fixtures=True)
        self.assertEqual(commands[0], ['make', '-j4', '-C', build / 'src'])
        targets = GATES.ON_SUITES + ['http3_udp_fixture', 'http3_semantic_parity_fixture', 'quic_interop_endpoint']
        for variable in ('TESTS', 'check_PROGRAMS', 'noinst_PROGRAMS'):
            self.assertIn(variable + '=' + ' '.join(targets), commands[1])
        self.assertEqual(commands[1][-len(targets):], targets)


if __name__ == '__main__':
    unittest.main()
