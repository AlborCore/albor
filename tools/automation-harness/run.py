import argparse
import os
import sys
import unittest
from pathlib import Path

# the repository root, found relative to this runner
_ROOT = Path(__file__).resolve().parents[2]

# the directory that holds the harness package
_PACKAGE_ROOT = _ROOT / "tools" / "automation-harness"

# the directory that holds the python suites
_TEST_ROOT = _ROOT / "tests" / "automation"


def parse_arguments() -> argparse.Namespace:
    # build the command line surface of the runner
    parser = argparse.ArgumentParser(description="run the albor automation compatibility suites")
    # the executable under test, otherwise the debug fixture app is used
    parser.add_argument("--app", help="the application executable the suites drive")
    # the discovery pattern, which keeps the default aligned with the style guide
    parser.add_argument("--pattern", default="*_test.py")
    # parsed arguments are returned to the caller
    return parser.parse_args()


def main() -> int:
    # read the command line before any path is resolved
    arguments = parse_arguments()
    # the harness package must import without an install step
    sys.path.insert(0, str(_PACKAGE_ROOT))
    # an explicit executable overrides the debug build default
    if arguments.app:
        os.environ["ALBOR_TEST_APPLICATION"] = str(Path(arguments.app).resolve())
    # discover both the client unit suite and the out of process suite
    suite = unittest.defaultTestLoader.discover(str(_TEST_ROOT), pattern=arguments.pattern, top_level_dir=str(_TEST_ROOT))
    # report every test on stdout so ci logs carry the evidence
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    # a failing or errored test makes the runner fail
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    # the runner is executable as a script
    sys.exit(main())
