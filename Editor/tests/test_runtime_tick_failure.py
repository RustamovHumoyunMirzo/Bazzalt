import unittest
from unittest.mock import Mock, patch

from Editor.runtime import RuntimeService


class RuntimeTickFailureTests(unittest.TestCase):
    def MakeRuntime(self):
        host = Mock()
        module = Mock()
        module.EditorHost.return_value = host
        with patch("Editor.runtime._LoadNativeModule", return_value=module):
            runtime = RuntimeService()
        return runtime, host

    def test_native_failure_is_reported_once_and_not_retried(self):
        runtime, host = self.MakeRuntime()
        host.tick.side_effect = RuntimeError("Unicode path conversion failed")
        errors = []
        runtime.ErrorOccurred.connect(errors.append)
        self.assertFalse(runtime.Tick())
        self.assertFalse(runtime.Tick())
        host.tick.assert_called_once()
        self.assertEqual(errors, ["Unicode path conversion failed"])
        self.assertEqual(runtime.LastError(), errors[0])

    def test_healthy_frames_are_not_suspended(self):
        runtime, host = self.MakeRuntime()
        self.assertTrue(runtime.Tick())
        self.assertTrue(runtime.Tick())
        self.assertEqual(host.tick.call_count, 2)


if __name__ == "__main__":
    unittest.main()
