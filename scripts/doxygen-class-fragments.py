#!/usr/bin/env python3
"""Present the five legacy class-body includes in their owning Doxygen classes.

Doxygen otherwise parses these standalone fragments as global functions.
Only these named public fragments are expanded; private includes stay untouched.
"""
from pathlib import Path
import sys


CLASS_FRAGMENTS = {
    'create_webserver.hpp': ('create_webserver_setters.hpp',),
    'webserver.hpp': ('webserver_routes.hpp', 'webserver_runtime.hpp',
                      'webserver_websocket.hpp', 'webserver_hooks.hpp'),
}


def render(source):
    contents = source.read_text(encoding='utf-8')
    for name in CLASS_FRAGMENTS.get(source.name, ()):
        directive = f'#include "httpserver/{name}"'
        if directive in contents:
            fragment = (source.parent / name).read_text(encoding='utf-8')
            contents = contents.replace(directive, fragment.rstrip('\n'))
    return contents


if __name__ == '__main__':
    sys.stdout.write(render(Path(sys.argv[1])))
