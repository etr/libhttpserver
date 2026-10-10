#!/usr/bin/env python3
"""The Doxygen filter expands only class-body fragments, never private includes."""
from pathlib import Path
import subprocess
import tempfile
import unittest


FILTER = Path(__file__).with_name('doxygen-class-fragments.py')


class ClassFragmentTests(unittest.TestCase):
    def render(self, name, contents, fragments):
        self.assertTrue(FILTER.is_file(), 'missing Doxygen class-fragment filter')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / name
            source.write_text(contents)
            for fragment, body in fragments.items():
                (source.parent / fragment).write_text(body)
            result = subprocess.run(['python3', str(FILTER), str(source)],
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stdout

    def test_builder_members_stay_inside_their_class(self):
        source = 'class create_webserver {\n#include "httpserver/create_webserver_setters.hpp"\n};\n'
        self.assertEqual(self.render('create_webserver.hpp', source,
                                    {'create_webserver_setters.hpp': 'void callback();\n'}),
                         'class create_webserver {\nvoid callback();\n};\n')

    def test_all_server_fragments_expand_in_place(self):
        names = ('routes', 'runtime', 'websocket', 'hooks')
        source = 'class webserver {\n' + ''.join(
            f'#include "httpserver/webserver_{name}.hpp"\n' for name in names) + '};\n'
        fragments = {f'webserver_{name}.hpp': f'void {name}();\n' for name in names}
        self.assertEqual(self.render('webserver.hpp', source, fragments),
                         'class webserver {\n' + ''.join(f'void {name}();\n' for name in names) + '};\n')

    def test_other_includes_and_unrelated_files_are_untouched(self):
        contents = '#include "httpserver/detail/secret.hpp"\n#include <string>\n'
        self.assertEqual(self.render('webserver.hpp', contents, {}), contents)
        include = '#include "httpserver/create_webserver_setters.hpp"\n'
        self.assertEqual(self.render('unrelated.hpp', include, {}), include)

    def test_missing_class_fragment_fails_generation(self):
        self.assertTrue(FILTER.is_file(), 'missing Doxygen class-fragment filter')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'create_webserver.hpp'
            source.write_text('#include "httpserver/create_webserver_setters.hpp"\n')
            result = subprocess.run(['python3', str(FILTER), str(source)],
                                    capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
