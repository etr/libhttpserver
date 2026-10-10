#!/usr/bin/env python3
"""Dedicated package workflow contract and mutation checks."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parent.parent


def violations(text):
    errors = []
    for platform in ('linux', 'macos', 'mingw64', 'freebsd'):
        if not re.search(r'^  ' + platform + r':$', text, re.M):
            errors.append('missing platform: ' + platform)
    if text.count('scripts/check-v3-installed-consumer.py') != 4:
        errors.append('each platform must invoke the same complete matrix')
    if text.count('if: always()') != 4 or text.count('actions/upload-artifact@') != 4:
        errors.append('failure receipts must be uploaded on every platform')
    if 'mingw-w64-x86_64-python' not in text or 'msystem: MINGW64' not in text:
        errors.append('native Windows Python and readiness host required')
    for use in re.findall(r'uses:\s*(\S+)', text):
        if not re.search(r'@[0-9a-f]{40}$', use):
            errors.append('unpinned action: ' + use)
    if 'continue-on-error' in text or '--disable-v3-tls' in text:
        errors.append('required matrix may not be reduced or advisory')
    if text.count('scripts/prepare-v3-package-ci.py') != 4:
        errors.append('genuine selected provider prerequisites required')
    for marker in ('pull_request:', 'push:', 'v3', 'workflow_dispatch:', 'vmactions/freebsd-vm@', '14.3'):
        if marker not in text:
            errors.append('missing ' + marker)
    return errors


class WorkflowTest(unittest.TestCase):
    def setUp(self):
        self.text = (ROOT / '.github/workflows/v3-package-validation.yml').read_text()

    def test_complete_workflow(self):
        self.assertEqual([], violations(self.text))

    def test_missing_platform_matrix_provider_or_failure_upload_is_rejected(self):
        for original, replacement in (('  freebsd:', '  missing:'),
                                      ('scripts/check-v3-installed-consumer.py', 'missing.py'),
                                      ('mingw-w64-x86_64-python', 'python'),
                                      ('if: always()', 'if: success()'),
                                      ('scripts/prepare-v3-package-ci.py', 'missing.py')):
            self.assertTrue(violations(self.text.replace(original, replacement, 1)))

    def test_provider_downloads_are_checksummed(self):
        script = (ROOT / 'scripts/prepare-v3-package-ci.py').read_text()
        self.assertIn('hashlib.sha256', script)
        self.assertIn('7816b57aae199cf5c3645e8770e1be5f0a4dfafbcb24b3772173dc4ee634126a', script)
        self.assertIn('shared', script)
        self.assertIn('3.5.9', script)


if __name__ == '__main__':
    unittest.main()
