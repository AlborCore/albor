import os
import unittest

import albor_automation
from albor_automation.errors import AlborAssertionError
from albor_automation.waiting import reports_test_frames, wait_for

# the package root hidden from a reported traceback
_FRAMEWORK_ROOT = os.path.dirname(os.path.abspath(albor_automation.__file__))


class WaitingChecks(unittest.TestCase):

    def test_wait_for_returns_when_the_condition_holds(self) -> None:
        # act
        wait_for(lambda: True, timeout=0.2, timeout_message="should not fail")

    def test_wait_for_times_out_with_message_and_state(self) -> None:
        # assert
        with self.assertRaises(AlborAssertionError) as context:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="waiting for the dialog")
        self.assertIn("waiting for the dialog", str(context.exception))
        self.assertIn("last observed: unknown", str(context.exception))
        self.assertIn("waited: 0.2 seconds", str(context.exception))

    def test_wait_for_reports_the_observed_state(self) -> None:
        # assert
        with self.assertRaises(AlborAssertionError) as context:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="waiting for ready state", observe=lambda: "state ready")
        self.assertIn("last observed: state ready", str(context.exception))

    def test_wait_for_reports_unknown_when_the_observer_fails(self) -> None:
        # assert
        with self.assertRaises(AlborAssertionError) as context:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="waiting for the panel", observe=lambda: 1 / 0)
        self.assertIn("last observed: unknown", str(context.exception))

    def test_wait_for_failure_reports_only_test_frames(self) -> None:
        # act
        try:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="frame check")
            self.fail("wait_for must raise on timeout")
        except AlborAssertionError as error:
            filenames = []
            entry = error.__traceback__
            while entry is not None:
                filenames.append(entry.tb_frame.f_code.co_filename)
                entry = entry.tb_next
            # assert
            self.assertTrue(filenames)
            for filename in filenames:
                self.assertFalse(filename.startswith(_FRAMEWORK_ROOT), filename)

    def test_reports_test_frames_preserves_non_assertion_errors(self) -> None:
        # arrange
        @reports_test_frames
        def raising_function() -> None:
            raise ValueError("unexpected")
        # assert
        with self.assertRaises(ValueError) as context:
            raising_function()
        self.assertEqual(str(context.exception), "unexpected")

    def test_reports_test_frames_trims_assertion_frames(self) -> None:
        # arrange
        @reports_test_frames
        def raising_function() -> None:
            raise AlborAssertionError("decorated failure")
        # assert
        with self.assertRaises(AlborAssertionError) as context:
            raising_function()
        self.assertEqual(str(context.exception), "decorated failure")


if __name__ == "__main__":
    unittest.main()
