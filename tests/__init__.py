import os
import sys

# path to the automation framework
_harness = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "tools", "automation-harness"))
# ensure the source files are included
if _harness not in sys.path:
    sys.path.insert(0, _harness)
