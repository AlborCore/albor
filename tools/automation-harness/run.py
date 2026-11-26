import argparse
import faulthandler
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

# the delay before a stalled suite dumps its thread stacks, below the ci step timeout
_STACK_DUMP_DELAY = 240.0


def parse_arguments() -> argparse.Namespace:
    # build the command line surface of the runner
    parser = argparse.ArgumentParser(description="run the albor automation compatibility suites")
    # the caller must name the executable explicitly
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
    # an explicit executable is published to the suites through the environment
    if arguments.app:
        os.environ["ALBOR_TEST_APPLICATION"] = str(Path(arguments.app).resolve())
    # a suite cannot drive an application that the caller never named
    if not os.environ.get("ALBOR_TEST_APPLICATION"):
        print("error: pass --app or set ALBOR_TEST_APPLICATION", file=sys.stderr)
        return 2
    # discover both the client unit suite and the out of process suite
    suite = unittest.defaultTestLoader.discover(str(_TEST_ROOT), pattern=arguments.pattern, top_level_dir=str(_TEST_ROOT))
    # report every test on stdout so ci logs carry the evidence
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    # a failing or errored test makes the runner fail
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    # a watchdog records where a suite stops answering before the ci step kills it
    faulthandler.dump_traceback_later(_STACK_DUMP_DELAY)
    try:
        # the runner is executable as a script
        sys.exit(main())
    finally:
        # the watchdog ends with the runner
        faulthandler.cancel_dump_traceback_later()
