import threading
import unittest
from unittest import mock

from albor_automation import transport as transport_module
from albor_automation.errors import McpError
from albor_automation.transport import NamedPipeTransport, _LinePump


class LinePumpCancelChecks(unittest.TestCase):

    def test_cancel_is_a_no_op_off_windows(self) -> None:
        pump = _LinePump(lambda size: b"", mock.MagicMock())
        pump._native_id = 42
        kernel = mock.MagicMock()
        with mock.patch.object(transport_module.os, "name", "posix"), mock.patch.object(transport_module, "_KERNEL32", kernel):
            # act
            pump.cancel()
        # assert
        kernel.OpenThread.assert_not_called()

    def test_cancel_targets_the_native_reader_thread(self) -> None:
        pump = _LinePump(lambda size: b"", mock.MagicMock())
        pump._native_id = 42
        kernel = mock.MagicMock()
        kernel.OpenThread.return_value = 123
        with mock.patch.object(transport_module.os, "name", "nt"), mock.patch.object(transport_module, "_KERNEL32", kernel):
            # act
            pump.cancel()
        # assert
        kernel.OpenThread.assert_called_once_with(transport_module._THREAD_ACCESS, False, 42)
        kernel.CancelSynchronousIo.assert_called_once_with(123)
        kernel.CloseHandle.assert_called_once_with(123)

    def test_cancel_without_a_reader_does_nothing(self) -> None:
        pump = _LinePump(lambda size: b"", mock.MagicMock())
        kernel = mock.MagicMock()
        with mock.patch.object(transport_module.os, "name", "nt"), mock.patch.object(transport_module, "_KERNEL32", kernel):
            # act
            pump.cancel()
        # assert
        kernel.OpenThread.assert_not_called()


class NamedPipeCloseChecks(unittest.TestCase):

    def test_send_line_times_out_when_the_pipe_has_no_reader(self) -> None:
        blocked = threading.Event()
        transport = NamedPipeTransport("pipe")
        try:
            with mock.patch.object(transport_module, "_PIPE_WRITE_TIMEOUT", 0.1), mock.patch.object(transport, "_write_line", side_effect=lambda line, written: blocked.wait(2)):
                # act
                with self.assertRaises(McpError) as context:
                    transport.send_line("hello")
            # assert
            self.assertIn("did not accept a request", str(context.exception))
        finally:
            # the blocked worker is released so the suite can finish
            blocked.set()

    def test_close_cancels_the_reader_before_releasing_the_pipe(self) -> None:
        pipe = mock.MagicMock()
        pump = mock.MagicMock()
        transport = NamedPipeTransport("pipe")
        transport._pipe = pipe
        transport._pump = pump
        # act
        transport.close()
        # assert
        pump.cancel.assert_called_once_with()
        pipe.close.assert_called_once_with()
