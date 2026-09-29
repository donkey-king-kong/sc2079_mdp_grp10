import queue
import sys
import tempfile
import threading
import types
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
if "serial" not in sys.modules:
    serial_stub = types.ModuleType("serial")
    serial_stub.SerialException = OSError
    sys.modules["serial"] = serial_stub

from task1_events import CameraRequest, Event, EventType, ImageTransferRequest
from task1_runtime import CameraCVWorker, ImageTransferWorker, MissionState, Task1Kernel, Task1Runtime


class KernelTests(unittest.TestCase):
    def setUp(self):
        self.algo, self.stm = queue.Queue(), queue.Queue()
        self.camera, self.events = queue.Queue(), queue.Queue()
        self.kernel = Task1Kernel(self.algo, self.stm, self.camera, self.events, threading.Event())
        self.kernel.start({"x": 1, "y": 1, "dir": "N"}, [{"id": 1, "x": 5, "y": 13, "dir": "W"}])

    def load_route(self, commands):
        self.kernel.handle_event(Event(EventType.ALGO_ROUTE_RECEIVED, {"commands": commands}))

    def test_route_zh_then_strict_sequential_movement(self):
        self.load_route(["SF010", "RF090", "FIN"])
        self.assertEqual(self.stm.get_nowait().command, "ZH")
        self.kernel.handle_event(Event(EventType.STM_ZH_COMPLETE, {"raw": "A ZH"}))
        self.assertEqual(self.stm.get_nowait().command, "SF010")
        self.assertTrue(self.stm.empty())
        self.kernel.handle_event(Event(EventType.STM_MOVEMENT_COMPLETE, {"raw": "A d=10.0 e=0"}))
        self.assertEqual(self.stm.get_nowait().command, "RF090")
        self.kernel.handle_event(Event(EventType.STM_MOVEMENT_COMPLETE, {"raw": "A a=90.0 e=0"}))
        self.assertEqual(self.kernel.state, MissionState.COMPLETE)

    def test_snap_queues_camera_request_and_waits_for_cv_result(self):
        self.load_route(["SNAP1", "SF010", "FIN"])
        self.stm.get_nowait()
        self.kernel.handle_event(Event(EventType.STM_ZH_COMPLETE))
        self.assertEqual(self.kernel.state, MissionState.WAITING_FOR_CV)
        self.assertEqual(self.camera.get_nowait().obstacle_id, "1")
        self.assertTrue(self.stm.empty())
        self.kernel.handle_event(Event(EventType.CV_RESULT_ACCEPTED, {"obstacle_id": "1"}))
        self.assertEqual(self.stm.get_nowait().command, "SF010")

    def test_abort_and_timeout_do_not_advance(self):
        for event_type in (EventType.STM_ABORT, EventType.STM_TIMEOUT):
            with self.subTest(event_type=event_type):
                self.setUp()
                self.load_route(["SF010", "RF090", "FIN"])
                self.stm.get_nowait()
                self.kernel.handle_event(Event(EventType.STM_ZH_COMPLETE))
                self.stm.get_nowait()
                self.kernel.handle_event(Event(event_type, {"raw": "A d=1 ABORT"}))
                self.assertEqual(self.kernel.state, MissionState.FAILED)
                self.assertTrue(self.stm.empty())

    def test_malformed_algo_route_fails_without_crashing(self):
        self.kernel.handle_event(Event(EventType.ALGO_ROUTE_RECEIVED, {"commands": "SF010"}))
        self.assertEqual(self.kernel.state, MissionState.FAILED)

    def test_unknown_stm_response_fails_without_crashing(self):
        self.load_route(["SF010", "FIN"])
        self.kernel.handle_event(Event(EventType.STM_UNKNOWN_RESPONSE, {"raw": "garbage"}))
        self.assertEqual(self.kernel.state, MissionState.FAILED)


class CameraWorkerTests(unittest.TestCase):
    def test_accepted_detection_persists_metadata_and_queues_transfer(self):
        with tempfile.TemporaryDirectory() as directory:
            image_path = Path(directory) / "capture.jpg"
            image_path.write_bytes(b"jpeg")

            class FakeImaging:
                def capture_and_predict(self, obstacle_id):
                    return {"image_id": "38", "confidence": 0.94, "bbox": (1, 2, 3, 4), "image_path": str(image_path)}

            requests, transfers, events = queue.Queue(), queue.Queue(maxsize=1), queue.Queue()
            requests.put(CameraRequest("1"))
            requests.put(None)
            worker = CameraCVWorker(FakeImaging(), requests, transfers, events, threading.Event(), Path(directory) / "detections")
            worker.start()
            worker.join(1)
            transfer = transfers.get_nowait()
            self.assertEqual(transfer.image_path, str(image_path))
            self.assertTrue(Path(transfer.metadata_path).is_file())
            self.assertEqual(events.get_nowait().type, EventType.CV_RESULT_ACCEPTED)


class ShutdownTests(unittest.TestCase):
    def test_runtime_shutdown_unblocks_and_joins_workers(self):
        class FakeAlgo:
            def navigate(self, **_kwargs):
                return {"type": "NAVIGATION", "data": {"commands": ["FIN"]}}

        class FakeSerial:
            is_open = True
            def write(self, _data): pass
            def flush(self): pass
            def readline(self): return b""

        runtime = Task1Runtime(FakeAlgo(), FakeSerial())
        runtime.start()
        runtime.stop()
        self.assertTrue(all(not worker.is_alive() for worker in runtime.workers))

    def test_unconfigured_transfer_keeps_local_image(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "image.jpg"
            metadata = Path(directory) / "image.json"
            image.write_bytes(b"jpeg"); metadata.write_text("{}")
            requests, events = queue.Queue(), queue.Queue()
            requests.put(ImageTransferRequest(str(image), str(metadata)))
            requests.put(None)
            worker = ImageTransferWorker(requests, events, threading.Event(), None)
            worker.start(); worker.join(1)
            self.assertTrue(image.is_file())
            self.assertEqual(events.get_nowait().type, EventType.IMAGE_TRANSFER_ERROR)

if __name__ == "__main__":
    unittest.main()
