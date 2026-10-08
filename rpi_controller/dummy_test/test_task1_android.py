import queue
import threading
import unittest

from connectors.android import AndroidConnector
from connectors.bluetooth import BluetoothDisconnectedError
from task1.workers.android import AndroidRXWorker
from task1.events import Event, EventType
from task1.runtime import Task1Kernel

class FakeBluetooth:
    def connect(self):
        return True


class DisconnectingBluetooth(FakeBluetooth):
    def __init__(self, shutdown):
        self.shutdown = shutdown
        self.disconnect_calls = 0

    def read_messages(self):
        raise BluetoothDisconnectedError("test disconnect")

    def disconnect(self):
        self.disconnect_calls += 1


class AndroidRXWorkerTests(unittest.TestCase):

    def test_send_arena_creates_kernel_event(self):
        events = queue.Queue()

        worker = AndroidRXWorker(
            FakeBluetooth(),
            AndroidConnector(),
            events,
            threading.Event(),
        )

        worker._handle_message({
            "cat": "sendArena",
            "value": {
                "robot_x": 1,
                "robot_y": 1,
                "robot_direction": 0,
                "obstacles": [
                    {
                        "id": 1,
                        "x": 5,
                        "y": 13,
                        "d": 3,
                    }
                ],
            },
        })

        event = events.get_nowait()

        self.assertEqual(
            event.type,
            EventType.ANDROID_ARENA_RECEIVED,
        )

        self.assertEqual(
            event.payload["robot"],
            {
                "x": 1,
                "y": 1,
                "dir": "N",
            },
        )

        self.assertEqual(
            event.payload["obstacles"][0],
            {
                "id": 1,
                "x": 5,
                "y": 13,
                "dir": "W",
            },
        )

    def test_manual_stm_command_creates_event(self):
        events = queue.Queue()

        worker = AndroidRXWorker(
            FakeBluetooth(),
            AndroidConnector(),
            events,
            threading.Event(),
        )

        worker._handle_message({
            "cat": "stm",
            "value": "<FW010>",
        })

        event = events.get_nowait()

        self.assertEqual(
            event.type,
            EventType.ANDROID_STM_COMMAND,
        )

        self.assertEqual(
            event.payload["command"],
            "SF010",
        )

    def test_start_command_creates_start_event(self):
        events = queue.Queue()
        worker = AndroidRXWorker(
            FakeBluetooth(),
            AndroidConnector(),
            events,
            threading.Event(),
        )

        worker._handle_message({"cat": "stm", "value": "beginExplore"})

        event = events.get_nowait()
        self.assertEqual(event.type, EventType.ANDROID_START_REQUEST)
        self.assertEqual(event.payload["mode"], "beginExplore")

    def test_disconnect_resets_bluetooth_service_and_reconnects(self):
        events = queue.Queue()
        shutdown = threading.Event()
        bluetooth = DisconnectingBluetooth(shutdown)
        reset_calls = []

        def reset_service():
            reset_calls.append(True)
            shutdown.set()

        worker = AndroidRXWorker(
            bluetooth,
            AndroidConnector(),
            events,
            shutdown,
            on_disconnect=reset_service,
            reconnect_delay_seconds=0,
        )

        worker.run()

        self.assertEqual(reset_calls, [True])
        self.assertEqual(bluetooth.disconnect_calls, 1)
class AndroidKernelTests(unittest.TestCase):

    def test_manual_android_command_reaches_stm_queue(self):
        algo = queue.Queue()
        stm = queue.Queue()
        camera = queue.Queue()
        events = queue.Queue()

        kernel = Task1Kernel(
            algo,
            stm,
            camera,
            events,
            threading.Event(),
        )

        kernel.handle_event(
            Event(
                EventType.ANDROID_STM_COMMAND,
                {"command": "SF010"},
            )
        )

        command = stm.get_nowait()

        self.assertEqual(
            command.command,
            "SF010",
        )
    def test_android_start_after_arena_starts_algo_request(self):
        algo = queue.Queue()
        stm = queue.Queue()
        camera = queue.Queue()
        events = queue.Queue()

        kernel = Task1Kernel(
            algo,
            stm,
            camera,
            events,
            threading.Event(),
            wait_for_start=True,
        )

        kernel.handle_event(
            Event(
                EventType.ANDROID_ARENA_RECEIVED,
                {
                    "robot": {
                        "x": 1,
                        "y": 1,
                        "dir": "N",
                    },
                    "obstacles": [
                        {
                            "id": 1,
                            "x": 5,
                            "y": 13,
                            "dir": "W",
                        }
                    ],
                },
            )
        )

        self.assertTrue(algo.empty())
        self.assertEqual(kernel.state.value, "IDLE")

        kernel.handle_event(
            Event(
                EventType.ANDROID_START_REQUEST,
                {"mode": "beginExplore"},
            )
        )

        request = algo.get_nowait()

        self.assertEqual(
            request.robot,
            {
                "x": 1,
                "y": 1,
                "dir": "N",
            },
        )

        self.assertEqual(
            len(request.obstacles),
            1,
        )
        self.assertEqual(kernel.state.value, "WAITING_FOR_ALGO")

    def test_android_arena_immediately_starts_algo_by_default(self):
        algo = queue.Queue()
        kernel = Task1Kernel(
            algo,
            queue.Queue(),
            queue.Queue(),
            queue.Queue(),
            threading.Event(),
        )

        kernel.handle_event(
            Event(
                EventType.ANDROID_ARENA_RECEIVED,
                {
                    "robot": {"x": 1, "y": 1, "dir": "N"},
                    "obstacles": [],
                },
            )
        )

        self.assertEqual(kernel.state.value, "WAITING_FOR_ALGO")
        self.assertEqual(algo.get_nowait().robot, {"x": 1, "y": 1, "dir": "N"})

    def test_cv_result_queues_target_for_android(self):
        algo = queue.Queue()
        stm = queue.Queue()
        camera = queue.Queue()
        events = queue.Queue()
        android = queue.Queue()

        kernel = Task1Kernel(
            algo,
            stm,
            camera,
            events,
            threading.Event(),
            android,
        )

        kernel.start(
            {"x": 1, "y": 1, "dir": "N"},
            [{"id": 1, "x": 5, "y": 13, "dir": "W"}],
        )

        kernel.handle_event(
            Event(
                EventType.ALGO_ROUTE_RECEIVED,
                {"commands": ["SNAP1", "FIN"]},
            )
        )

        # Remove ZH command and simulate STM initialization.
        stm.get_nowait()
        kernel.handle_event(
            Event(EventType.STM_ZH_COMPLETE)
        )

        # SNAP1 should now be waiting for CV.
        camera.get_nowait()

        kernel.handle_event(
            Event(
                EventType.CV_RESULT_ACCEPTED,
                {
                    "obstacle_id": "1",
                    "result": {
                        "image_id": "38",
                        "image_path": "/tmp/image1.jpg",
                    },
                },
            )
        )

        message = android.get_nowait()

        self.assertEqual(
            message.message,
            "TARGET,1,38\n",
        )

    def test_fin_queues_images_for_android(self):
        algo = queue.Queue()
        stm = queue.Queue()
        camera = queue.Queue()
        events = queue.Queue()
        android = queue.Queue()

        kernel = Task1Kernel(
            algo,
            stm,
            camera,
            events,
            threading.Event(),
            android,
        )

        kernel.detected_images = [
            "/tmp/image1.jpg",
            "/tmp/image2.jpg",
        ]

        kernel.route = ["FIN"]
        kernel.route_index = 0

        kernel._dispatch_route_item()

        request = android.get_nowait()

        self.assertEqual(
            request.image_paths,
            [
                "/tmp/image1.jpg",
                "/tmp/image2.jpg",
            ],
        )

        self.assertEqual(
            kernel.state.value,
            "WAITING_FOR_ANDROID_IMAGE_TRANSFER",
        )

        kernel.handle_event(
            Event(EventType.ANDROID_IMAGE_TRANSFER_COMPLETE)
        )

        self.assertEqual(
            kernel.state.value,
            "COMPLETE",
        )

if __name__ == "__main__":
    unittest.main()
