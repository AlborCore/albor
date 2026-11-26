import os
import tempfile
import unittest
from pathlib import Path

from albor_automation.test_case import AlborTestCase


class FakeSessionApp:

    def __init__(self) -> None:
        self.artifacts: list[str] = []
        self.closed = False

    def is_running(self) -> bool:
        return not self.closed

    def collect_artifacts(self, directory: str) -> str:
        self.artifacts.append(directory)
        return directory

    def close(self) -> None:
        self.closed = True


class PassingCase(AlborTestCase):

    def setUp(self) -> None:
        super().setUp()
        self.start_session(FakeSessionApp())

    def test_passing_run(self) -> None:
        self.assertTrue(self._app.is_running())


class AlborTestCaseChecks(unittest.TestCase):

    def setUp(self) -> None:
        self._artifacts = tempfile.TemporaryDirectory()
        self.addCleanup(self._artifacts.cleanup)
        self._previous = os.environ.get("ALBOR_TEST_ARTIFACTS")
        os.environ["ALBOR_TEST_ARTIFACTS"] = self._artifacts.name
        if self._previous is None:
            self.addCleanup(os.environ.pop, "ALBOR_TEST_ARTIFACTS", None)
        else:
            self.addCleanup(os.environ.__setitem__, "ALBOR_TEST_ARTIFACTS", self._previous)

    def test_clean_run_closes_the_application_without_artifacts(self) -> None:
        # arrange
        case = PassingCase("test_passing_run")
        result = unittest.TestResult()
        # act
        case.run(result)
        # assert
        self.assertTrue(result.wasSuccessful())
        self.assertTrue(case._app.closed)
        self.assertEqual(case._app.artifacts, [])
        self.assertIsNone(case._session)

    def test_failed_run_collects_artifacts_named_after_the_case(self) -> None:
        # arrange
        class FailingCase(AlborTestCase):

            def setUp(self) -> None:
                super().setUp()
                self.start_session(FakeSessionApp())

            def test_failing_run(self) -> None:
                self.fail("deliberate failure")
        case = FailingCase("test_failing_run")
        result = unittest.TestResult()
        # act
        case.run(result)
        # assert
        self.assertEqual(len(result.failures), 1)
        self.assertEqual(case._app.artifacts, [str(Path(self._artifacts.name) / case.id())])
        self.assertTrue(case._app.closed)


if __name__ == "__main__":
    unittest.main()
