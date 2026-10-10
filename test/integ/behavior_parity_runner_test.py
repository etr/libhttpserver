"""TASK-179 receipts must prove each applicable capability and corpus cell."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('behavior_parity', ROOT / 'scripts/run-v3-behavior-parity.py')
AUDIT = importlib.util.module_from_spec(SPEC)
try:
    SPEC.loader.exec_module(AUDIT)
except FileNotFoundError:
    AUDIT = None


class Receipts(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(AUDIT, 'TASK-179 receipt checker is missing')
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / 'test/parity/transcripts').mkdir(parents=True)
        self.corpus = self.root / 'test/parity/transcripts/routing.tseq'
        self.corpus.write_text('profile routing_basic\ncase hello\nsend GET / HTTP/1.1\\r\\n\\r\\n\n'
                               'expect status 200\nexpect header Content-Type: text/plain\n'
                               'expect body "OK"\nexpect framing content-length\nexpect connection keep-alive\n')
        (self.root / 'inventory.md').write_text('| Wire behavior | Source | Transcript | Status | Note |\n'
                                               '|---|---|---|---|---|\n'
                                               '| Exact routing | routes.hpp | routing.tseq:hello | pinned | preserve |\n')
        (self.root / 'migration.md').write_text('# Framing\nMultiplexed DATA replaces HTTP/1 framing.\n')
        self.inventory = {'v': 1, 'source': 'inventory.md', 'capabilities': [
            {'id': 'routing-001', 'behavior': 'Exact routing', 'case': 'routing.tseq:hello',
             'cells': {mode: {'applicable': True} for mode in AUDIT.MODES}}]}
        self.receipt = {'v': 1, 'commands': [{'id': 'wire', 'argv': ['fixture'], 'exit': 0,
                                            'tool': 'fixture 1', 'log': 'wire.log'}],
                        'observations': [], 'outcomes': [], 'inputs': {}}
        (self.root / 'wire.log').write_text('observed wire\n')
        for mode in AUDIT.MODES:
            observation = {'id': mode, 'command': 'wire', 'case': 'routing.tseq:hello', 'mode': mode,
                           'kind': 'quic' if mode == 'h3-quic' else ('tls' if mode.endswith('tls') else 'tcp'),
                           'connection': '1', 'followup': {'connection': '1', 'status': 204},
                           'responses': [{'status': 200, 'status_line': 'HTTP/1.1 200 OK',
                                          'headers': [['Content-Type', 'text/plain']], 'trailers': [],
                                          'body_hex': '4f4b', 'framing': 'data' if mode.startswith(('h2', 'h3')) else 'content-length',
                                          'ended': True, 'stream': 1}]}
            self.receipt['observations'].append(observation)
            cell = {'capability': 'routing-001', 'mode': mode, 'classification': 'preserved', 'evidence': [mode]}
            if mode.startswith(('h2', 'h3')):
                cell.update(classification='migration', reference='migration.md#framing',
                            rationale='DATA framing with stream completion', changes=['framing'])
            self.receipt['outcomes'].append(cell)
        self.refresh_inputs()

    def refresh_inputs(self):
        self.receipt['inputs'] = AUDIT.input_hashes(self.root, self.inventory)

    def errors(self):
        return AUDIT.check_receipt(self.root, self.inventory, self.receipt)

    def test_complete_observed_cells_pass(self):
        self.assertEqual([], self.errors())

    def test_execution_requires_fresh_receipt_directory(self):
        self.assertTrue(hasattr(AUDIT, 'run_audit'), 'audit execution is missing')
        with self.assertRaisesRegex(ValueError, 'fresh receipt directory'):
            AUDIT.run_audit(self.root, self.inventory, self.root, self.root, self.root, self.root, self.root)

    def test_peer_policy_scheduling_is_explicit_in_command_receipt(self):
        self.assertTrue(hasattr(AUDIT, 'suite_command'))
        self.assertEqual([self.root / 'families'], AUDIT.suite_command(self.root / 'families'))
        command = AUDIT.suite_command(self.root / 'peer_policy')
        self.assertEqual(self.root / 'peer_policy', command[-1])
        if len(command) > 1:
            self.assertEqual('-c', command[1])
            self.assertTrue(command[2].isdigit())

    def test_missing_and_duplicate_capabilities_fail(self):
        for replacement in ([], self.inventory['capabilities'] * 2):
            with self.subTest(replacement=replacement):
                saved = self.inventory['capabilities']
                self.inventory['capabilities'] = replacement
                self.assertTrue(self.errors())
                self.inventory['capabilities'] = saved

    def test_new_corpus_case_requires_outcomes(self):
        self.corpus.write_text(self.corpus.read_text() + 'case new\nsend GET /new HTTP/1.1\\r\\n\\r\\n\nexpect status 200\n')
        self.refresh_inputs()
        self.assertTrue(self.errors())

    def test_missing_protocol_cell_fails(self):
        del self.inventory['capabilities'][0]['cells'][AUDIT.MODES[0]]
        self.assertTrue(self.errors())

    def test_inventory_delta_and_completion_rows_cannot_be_omitted(self):
        with (self.root / 'inventory.md').open('a') as output:
            output.write('| Runtime revalidation | policy.hpp | — | delta | new behavior |\n'
                         '| Remaining documented HTTP/1 contract | Evidence |\n|---|---|\n'
                         '| Cookies | cookie tests |\n')
        self.refresh_inputs()
        self.assertTrue(self.errors())

    def test_two_cases_in_one_capability_require_both_observations(self):
        self.corpus.write_text(self.corpus.read_text() + 'case sibling\nsend GET /new HTTP/1.1\\r\\n\\r\\n\n'
                               'expect status 200\nexpect body "OK"\n')
        capability = self.inventory['capabilities'][0]
        capability['cases'] = ['routing.tseq:hello', 'routing.tseq:sibling']
        del capability['case']
        self.refresh_inputs()
        self.assertTrue(self.errors())

    def test_missing_outcome_fails(self):
        self.receipt['outcomes'].pop()
        self.assertTrue(self.errors())

    def test_duplicate_observation_and_outcome_fail(self):
        for key in ('observations', 'outcomes'):
            with self.subTest(key=key):
                self.receipt[key].append(copy.deepcopy(self.receipt[key][0]))
                self.assertTrue(self.errors())
                self.receipt[key].pop()

    def test_duplicate_case_identity_and_uncorrelated_streams_fail(self):
        duplicate = copy.deepcopy(self.receipt['observations'][0])
        duplicate['id'] = 'another-observation-id'
        self.receipt['observations'].append(duplicate)
        self.assertTrue(self.errors())

    def test_pipelined_multiplexed_responses_require_distinct_streams(self):
        self.corpus.write_text(self.corpus.read_text() + 'expect status 200\nexpect body "OK"\n')
        for observation in self.receipt['observations']:
            observation['responses'].append(copy.deepcopy(observation['responses'][0]))
        self.refresh_inputs()
        self.assertTrue(self.errors())

    def test_missing_migration_page_and_anchor_fail(self):
        cell = self.receipt['outcomes'][-1]
        for reference in ('absent.md#framing', 'migration.md#absent'):
            cell['reference'] = reference
            self.assertTrue(self.errors())

    def test_missing_command_receipt_and_log_fail(self):
        self.receipt['commands'] = []
        self.assertTrue(self.errors())
        self.setUp()
        (self.root / 'wire.log').unlink()
        self.assertTrue(self.errors())

    def test_nonzero_command_exit_fails(self):
        self.receipt['commands'][0]['exit'] = 1
        self.assertTrue(self.errors())

    def test_malformed_empty_and_unfinished_observations_fail(self):
        for responses in ([], [{}], [dict(self.receipt['observations'][0]['responses'][0], ended=False)]):
            with self.subTest(responses=responses):
                self.receipt['observations'][0]['responses'] = responses
                self.assertTrue(self.errors())

    def test_skipped_applicable_and_retained_defect_fail(self):
        cell = self.receipt['outcomes'][0]
        for classification in ('skipped', 'defect', 'unavailable'):
            cell['classification'] = classification
            self.assertTrue(self.errors())

    def test_body_status_and_application_fields_cannot_be_waived(self):
        observation = self.receipt['observations'][-1]['responses'][0]
        cell = self.receipt['outcomes'][-1]
        for key, value in (('body_hex', '4e4f'), ('status', 500), ('headers', [['Content-Type', 'text/html']])):
            with self.subTest(key=key):
                previous = observation[key]
                observation[key] = value
                cell['changes'] = ['framing', key]
                self.assertTrue(self.errors())
                observation[key] = previous

    def test_persistence_requires_a_successful_same_connection_followup(self):
        observation = self.receipt['observations'][0]
        for followup in (None, {'connection': '2', 'status': 204}, {'connection': '1', 'status': 500}):
            observation['followup'] = followup
            self.assertTrue(self.errors())

    def test_engine_only_evidence_cannot_replace_real_transport(self):
        self.receipt['observations'][0]['kind'] = 'engine-frame'
        self.assertTrue(self.errors())

    def test_stale_inputs_and_malformed_receipt_fail(self):
        self.receipt['inputs'] = {}
        self.assertTrue(self.errors())
        self.assertTrue(AUDIT.check_receipt(self.root, self.inventory, {}))

    def test_changed_file_fixture_invalidates_receipt(self):
        fixture = self.root / 'test/test_content'
        fixture.write_text('pinned file bytes')
        self.refresh_inputs()
        fixture.write_text('changed file bytes')
        self.assertTrue(self.errors())

    def add_named_fixture(self):
        with (self.root / 'inventory.md').open('a') as output:
            output.write('| Prefix family | routes.hpp | — | inventory-only | preserve |\n')
        self.inventory['capabilities'].append({'id': 'prefix', 'behavior': 'Prefix family', 'cases': [],
            'tests': [{'suite': 'families', 'name': 'prefix_matching', 'kind': 'semantic'}],
            'cells': {mode: {'applicable': True} for mode in AUDIT.MODES}})
        (self.root / 'families.log').write_text('Running test (1): prefix_matching\n'
            '- Time spent during "prefix_matching": 1 ms\n1 tests executed\n3 checks\n-> 3 successes\n-> 0 failures\n-> 0 skipped\n')
        self.receipt['commands'].append({'id': 'families', 'argv': ['./families'], 'exit': 0,
            'tool': 'families built source hash', 'log': 'families.log', 'cwd': str(self.root)})
        binary = self.root / 'families'
        binary.write_bytes(b'synthetic mutation fixture')
        self.bind_execution(self.receipt['commands'][-1], binary, 'families')
        self.receipt['supporting'] = [{'id': 'prefix-check', 'command': 'families', 'suite': 'families',
            'name': 'prefix_matching', 'kind': 'semantic'}]
        self.receipt['outcomes'] += [{'capability': 'prefix', 'mode': mode,
            'classification': 'preserved', 'evidence': ['prefix-check']} for mode in AUDIT.MODES]
        self.refresh_inputs()

    def bind_execution(self, command, binary, suite):
        log = (self.root / command['log']).read_text()
        command['named_execution'] = {'suite': suite, 'executable': str(binary.resolve()),
            'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
            'log_sha256': hashlib.sha256(log.encode()).hexdigest(),
            'tests': len(AUDIT.executed_tests(log)),
            'checks': int(AUDIT.re.search(r'^(\d+) checks$', log, AUDIT.re.MULTILINE)[1])}

    def test_inventory_only_requires_named_executed_checks(self):
        self.add_named_fixture()
        self.assertEqual([], self.errors())
        valid = (self.root / 'families.log').read_text()
        for log in ('fixture started\n', 'Running test (1): other\n-> 0 failures\n-> 0 skipped\n',
                    valid.replace('prefix_matching', 'another_case'),
                    valid.replace('1 tests executed', '2 tests executed'),
                    valid.replace('-> 0 failures', '-> 1 failures'),
                    valid.replace('-> 0 skipped', '-> 1 skipped'),
                    valid.replace('- Time spent during "prefix_matching": 1 ms\n', ''),
                    valid.replace('3 checks', '0 checks'),
                    valid + valid):
            with self.subTest(log=log):
                (self.root / 'families.log').write_text(log)
                self.receipt['commands'][-1]['named_execution']['log_sha256'] = hashlib.sha256(log.encode()).hexdigest()
                self.assertTrue(self.errors())

    def test_named_counts_and_completion_cannot_be_forged(self):
        valid = ('Running test (1): first\n- Time spent during "first": 1 ms\n'
                 '1 tests executed\n3 checks\n-> 3 successes\n-> 0 failures\n-> 0 skipped\n')
        self.assertEqual({'first'}, AUDIT.executed_tests(valid))
        for log in (valid.replace('-> 3 successes', '-> 2 successes'),
                    valid.replace('test (1)', 'test (2)'),
                    valid + '1 tests executed\n', valid + '3 checks\n',
                    valid + '-> 1 failures\n', valid + '-> 1 skipped\n'):
            with self.subTest(log=log):
                self.assertEqual(set(), AUDIT.executed_tests(log))

    def test_named_suite_requires_matching_executable_and_command_provenance(self):
        self.add_named_fixture()
        valid = copy.deepcopy(self.receipt['commands'][-1])
        mutations = [dict(valid, argv=['./unrelated']), dict(valid, argv=[]),
            dict(valid, named_execution={}), dict(valid, named_execution=None),
            dict(valid, named_execution=[]), dict(valid, named_execution='unrelated'),
            dict(valid, cwd=str(self.root / 'elsewhere'))]
        for key, value in (('suite', 'cookie_render'), ('sha256', 'wrong'),
                           ('log_sha256', 'wrong'), ('tests', 2), ('checks', 4),
                           ('executable', str(self.root / 'absent'))):
            mutated = copy.deepcopy(valid)
            mutated['named_execution'][key] = value
            mutations.append(mutated)
        for command in mutations:
            with self.subTest(command=command):
                self.receipt['commands'][-1] = command
                self.assertTrue(self.errors())
        self.receipt['commands'][-1] = valid
        self.receipt['supporting'][0]['suite'] = 'absent_suite'
        self.assertTrue(self.errors())
        self.receipt['supporting'][0]['suite'] = 'families'
        (self.root / 'families').unlink()
        self.assertTrue(self.errors())

    def test_real_current_cookie_execution_proves_three_inventory_witnesses(self):
        self.add_named_fixture()
        binary = Path(os.environ.get('TASK192_COOKIE_BINARY', Path.cwd() / 'http_cookie')).resolve()
        if not binary.is_file():
            binary = self.root / 'http_cookie'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-I' + str(ROOT / 'src'),
                '-I' + str(ROOT / 'test'), str(ROOT / 'test/unit/http_cookie_test.cpp'),
                str(ROOT / 'src/cookie.cpp'), '-o', str(binary)], check=True, capture_output=True, timeout=120)
        result = subprocess.run([str(binary)], capture_output=True, text=True, check=True, cwd=binary.parent, timeout=30)
        names = ('parse_multi_semicolon_separated', 'render_full_attribute_ordering',
                 'render_guard_rejects_cr_in_parsed_value')
        self.assertTrue(set(names).issubset(AUDIT.executed_tests(result.stdout)))
        command = self.receipt['commands'][-1]
        command.update(argv=[str(binary)], cwd=str(binary.parent))
        (self.root / command['log']).write_text(result.stdout)
        actual = binary.parent / '.libs' / binary.name
        self.bind_execution(command, actual if actual.is_file() else binary, 'http_cookie')
        self.inventory['capabilities'][-1]['tests'] = [
            {'suite': 'http_cookie', 'name': name, 'kind': 'semantic'} for name in names]
        self.receipt['supporting'] = [{'id': name, 'command': 'families', 'suite': 'http_cookie',
                                      'name': name, 'kind': 'semantic'} for name in names]
        for outcome in self.receipt['outcomes']:
            if outcome['capability'] == 'prefix':
                outcome['evidence'] = list(names)
        self.refresh_inputs()
        self.assertEqual([], self.errors())

    def test_allow_auth_captures_and_hook_fields_survive_framing_translation(self):
        observation = copy.deepcopy(self.receipt['observations'][-1])
        response = observation['responses'][0]
        response['headers'] = [['Allow', 'GET, HEAD'], ['WWW-Authenticate', 'Basic realm="test"'], ['X-Hook', 'yes']]
        expects = [('status', '200', 1), ('header', 'Allow: GET, HEAD', 2),
            ('header', 'WWW-Authenticate: Basic realm="test"', 3), ('header', 'X-Hook: yes', 4), ('body', '"id=42"', 5)]
        response['body_hex'] = b'id=42'.hex()
        self.assertEqual([], AUDIT.check_expectations(self.root, expects, observation, ['framing']))
        for index in range(3):
            saved = response['headers'][index][1]
            response['headers'][index][1] = 'changed'
            self.assertTrue(AUDIT.check_expectations(self.root, expects, observation, ['framing']))
            response['headers'][index][1] = saved
        response['body_hex'] = b'id=41'.hex()
        self.assertTrue(AUDIT.check_expectations(self.root, expects, observation, ['framing']))

    def test_extended_connect_requires_observed_echo_close_and_ordinary_followup(self):
        observation = copy.deepcopy(self.receipt['observations'][-1])
        observation['case'] = 'websocket.tseq:upgrade_handshake_fixed_key'
        response = observation['responses'][0]
        response.update(status=200, headers=[], body_hex='8106706172697479880203e8')
        observation['followup']['status'] = 200
        observation['websocket'] = {'echo_hex': '706172697479', 'clean_close': True}
        expects = [('status', '101', 1), ('header', 'Upgrade: websocket', 2),
                   ('header', 'Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=', 3), ('connection', 'close', 4)]
        self.assertEqual([], AUDIT.check_expectations(self.root, expects, observation, ['websocket-handshake']))
        for mutation in ({'clean_close': False}, {'echo_hex': '00'}):
            observation['websocket'].update(mutation)
            self.assertTrue(AUDIT.check_expectations(self.root, expects, observation, ['websocket-handshake']))
        response['status'] = 101
        self.assertTrue(AUDIT.check_expectations(self.root, expects, observation, ['websocket-handshake']))

    def test_http11_websocket_requires_echo_and_clean_close_witness(self):
        observation = copy.deepcopy(self.receipt['observations'][1])
        observation['case'] = 'websocket.tseq:upgrade_handshake_fixed_key'
        observation['responses'][0]['status'] = 101
        observation['closed'] = True
        expects = [('status', '101', 1), ('connection', 'close', 2)]
        self.assertTrue(AUDIT.check_expectations(self.root, expects, observation, []))
        observation['websocket'] = {'echo_hex': '706172697479', 'clean_close': True}
        self.assertEqual([], AUDIT.check_expectations(self.root, expects, observation, []))

    def test_live_policy_witness_rejects_boolean_byte_count(self):
        with (self.root / 'inventory.md').open('a') as output:
            output.write('| Live policy | policy.hpp | — | delta | revalidate |\n')
        self.inventory['capabilities'].append({'id': 'policy', 'behavior': 'Live policy', 'cases': [],
            'tests': [{'suite': 'behavior-wire', 'name': 'runtime_deny_refuses_next_request', 'kind': 'tcp'}],
            'cells': {mode: {'applicable': True} for mode in AUDIT.MODES}})
        self.receipt['supporting'] = [{'id': 'policy-proof', 'command': 'wire', 'suite': 'behavior-wire',
            'name': 'runtime_deny_refuses_next_request', 'kind': 'tcp', 'mode': 'h1.0-tcp',
            'witness': {'allow_status': 200, 'mutation_status': 204, 'refused': True,
                        'application_bytes': False, 'connection': '1'}}]
        self.receipt['outcomes'] += [{'capability': 'policy', 'mode': mode, 'classification': 'preserved',
            'evidence': ['policy-proof']} for mode in AUDIT.MODES]
        self.refresh_inputs()
        self.assertIn('missing live policy refusal witness', self.errors())

    def test_http10_close_delimiting_preserves_body_and_requires_observed_eof(self):
        observation = copy.deepcopy(self.receipt['observations'][0])
        observation['case'] = 'file_resp.tseq:pipe_body'
        observation['responses'][0]['framing'] = 'close-delimited'
        observation['closed'] = True
        observation['followup'] = None
        expects = [('status', '200', 1), ('header', 'Transfer-Encoding: chunked', 2),
                   ('body', '"OK"', 3), ('framing', 'chunked', 4), ('connection', 'keep-alive', 5)]
        self.assertEqual([], AUDIT.check_expectations(self.root, expects, observation, ['http1.0-close-delimited']))
        observation['closed'] = False
        self.assertTrue(AUDIT.check_expectations(self.root, expects, observation, ['http1.0-close-delimited']))

    def test_http10_unknown_length_migration_does_not_override_known_length_sibling(self):
        observation = copy.deepcopy(self.receipt['observations'][0])
        observation['case'] = 'file_resp.tseq:iovec_body'
        expects = [('status', '200', 1), ('body', '"OK"', 2),
                   ('framing', 'content-length', 3), ('connection', 'keep-alive', 4)]
        self.assertEqual([], AUDIT.check_expectations(self.root, expects, observation, ['http1.0-close-delimited']))


if __name__ == '__main__':
    unittest.main()
