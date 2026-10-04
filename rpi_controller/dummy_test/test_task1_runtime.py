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

from task1.events import CameraRequest, Event, EventType, ImageTransferRequest
from task1.runtime import MissionState, Task1Kernel, Task1Runtime
from task1.workers.camera import CameraCVWorker
from task1.workers.transfer import ImageTransferWorker


class KernelTests(unittest.TestCase):
    def setUp(self):
        self.algo, self.stm = queue.Queue(), queue.Queue()
        self.camera, self.events = queue.Queue(), queue.Queue()
        self.kernel = Task1Kernel(
            self.algo,
            self.stm,
            self.camera,
            self.events,
            threading.Event(),
            parallel_inference=False,
        )
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

    def test_no_detection_saves_capture_and_advances_route(self):
        self.load_route(["SNAP1", "FIN"])
        self.stm.get_nowait()
        self.kernel.handle_event(Event(EventType.STM_ZH_COMPLETE))
        self.assertEqual(self.camera.get_nowait().obstacle_id, "1")
        self.kernel.handle_event(Event(EventType.CV_RETRY_REQUIRED, {"obstacle_id": "1"}))
        self.assertEqual(self.kernel.state, MissionState.COMPLETE)

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
    def test_worker_starts_and_closes_persistent_imaging_once(self):
        class FakeImaging:
            def __init__(self):
                self.started = self.closed = 0

            def start(self):
                self.started += 1

            def close(self):
                self.closed += 1

        imaging = FakeImaging()
        requests, transfers, events = queue.Queue(), queue.Queue(), queue.Queue()
        requests.put(None)
        worker = CameraCVWorker(imaging, requests, transfers, events, threading.Event(), Path("."))
        worker.start(); worker.join(1)
        self.assertEqual((imaging.started, imaging.closed), (1, 1))

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
            worker = CameraCVWorker(
                FakeImaging(),
                requests,
                transfers,
                events,
                threading.Event(),
                Path(directory) / "detections",
                parallel_inference=False,
            )
            worker.start()
            worker.join(1)
            transfer = transfers.get_nowait()
            self.assertEqual(transfer.image_path, str(image_path))
            self.assertTrue(Path(transfer.metadata_path).is_file())
            self.assertEqual(events.get_nowait().type, EventType.CV_RESULT_ACCEPTED)

    def test_no_symbol_capture_is_still_queued_for_transfer(self):
        with tempfile.TemporaryDirectory() as directory:
            image_path = Path(directory) / "capture_no_symbol.jpg"
            image_path.write_bytes(b"jpeg")

            class FakeImaging:
                def capture_and_predict(self, obstacle_id):
                    return {"image_id": None, "confidence": None, "bbox": None, "image_path": str(image_path)}

            requests, transfers, events = queue.Queue(), queue.Queue(maxsize=1), queue.Queue()
            requests.put(CameraRequest("1"))
            requests.put(None)
            worker = CameraCVWorker(
                FakeImaging(),
                requests,
                transfers,
                events,
                threading.Event(),
                Path(directory),
                parallel_inference=False,
            )
            worker.start()
            worker.join(1)
            self.assertEqual(transfers.get_nowait().image_path, str(image_path))
            self.assertEqual(events.get_nowait().type, EventType.CV_RETRY_REQUIRED)

    def test_continuous_scan_saves_detection_without_a_snap_event(self):
        with tempfile.TemporaryDirectory() as directory:
            image_path = Path(directory) / "continuous.jpg"
            image_path.write_bytes(b"jpeg")
            called = threading.Event()
            observed = {}

            class FakeImaging:
                def capture_and_predict(self, obstacle_id, **options):
                    observed.update(obstacle_id=obstacle_id, options=options)
                    called.set()
                    return {"image_id": "38", "confidence": 0.94, "bbox": (1, 2, 3, 4), "image_path": str(image_path)}

            requests, transfers, events = queue.Queue(), queue.Queue(maxsize=1), queue.Queue()
            worker = CameraCVWorker(
                FakeImaging(), requests, transfers, events, threading.Event(), Path(directory),
                continuous_scan=True, continuous_scan_interval_seconds=0.01,
            )
            worker.start()
            self.assertTrue(called.wait(1))
            requests.put(None)
            worker.join(1)
            self.assertEqual(observed["obstacle_id"], "continuous")
            self.assertFalse(observed["options"]["save_on_no_detection"])
            self.assertEqual(transfers.get_nowait().image_path, str(image_path))
            self.assertEqual(events.get_nowait().type, EventType.CV_CONTINUOUS_RESULT)


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
