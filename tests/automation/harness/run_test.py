import io
import os
import sys
import unittest
from contextlib import redirect_stderr
from unittest import mock

import run


class RunnerChecks(unittest.TestCase):

    def test_main_requires_an_explicit_executable(self) -> None:
        # arrange
        errors = io.StringIO()
        with mock.patch.dict(os.environ, {}, clear=True), mock.patch.object(sys, "argv", ["run.py"]), redirect_stderr(errors):
            # act
            code = run.main()
        # assert
        self.assertEqual(code, 2)
        self.assertIn("--app", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
