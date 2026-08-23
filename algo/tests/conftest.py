"""Put the package root on the path so tests can `import planner` directly.

The modules import each other by bare name (`import config as cfg`), which keeps
them runnable as scripts during debugging; this makes that work from `tests/`
too, under both pytest and the plain unittest runner.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
