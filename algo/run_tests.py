#!/usr/bin/env python3
"""Run the whole test suite with nothing installed but Python.

`python3 -m pytest tests/ -v` gives nicer output, but pytest is a dependency and
this is the module a teammate clones and runs cold. Keep both working.
"""

import os
import sys
import unittest

ROOT = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.join(ROOT, "tests")
sys.path.insert(0, ROOT)
sys.path.insert(0, TESTS)


def main() -> int:
    suite = unittest.TestLoader().discover(start_dir=TESTS, top_level_dir=TESTS)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
