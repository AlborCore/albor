import unittest
from pathlib import Path

from albor_automation.session import TestSession


class FakeApp:

    def __init__(self) -> None:
        self.closed = False
        self.artifacts = ""

    def close(self) -> None:
        self.closed = True

    def collect_artifacts(self, directory: str) -> str:
        self.artifacts = directory
        return directory


class SessionChecks(unittest.TestCase):

    def test_finish_closes_the_application(self) -> None:
        # arrange
        app = FakeApp()
        session = TestSession(app, "a-test", "artifacts")
        # act
        session.finish(False)
        # assert
        self.assertTrue(app.closed)
        self.assertEqual(app.artifacts, "")

    def test_finish_collects_artifacts_on_failure(self) -> None:
        # arrange
        app = FakeApp()
        session = TestSession(app, "a-test", "artifacts")
        # act
        session.finish(True)
        # assert
        self.assertEqual(app.artifacts, str(Path("artifacts") / "a-test"))

    def test_context_manager_closes_on_exit(self) -> None:
        # arrange
        app = FakeApp()
        # act
        with TestSession(app, "a-test", "artifacts") as entered:
            self.assertIs(entered, app)
        # assert
        self.assertTrue(app.closed)

    def test_context_manager_collects_on_an_exception(self) -> None:
        # arrange
        app = FakeApp()
        # assert
        with self.assertRaises(ValueError):
            with TestSession(app, "a-test", "artifacts"):
                raise ValueError("failed")
        self.assertEqual(app.artifacts, str(Path("artifacts") / "a-test"))


if __name__ == "__main__":
    unittest.main()
