import queue
import threading
import unittest

from connectors.android import AndroidConnector
from task1_android import AndroidRXWorker
from task1_events import Event,EventType
from task1_runtime import Task1Kernel

class FakeBluetooth:
    def connect(self):
        return True


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
                "robot_direction": 1,
                "obstacles": [
                    {
                        "id": 1,
                        "x": 5,
                        "y": 13,
                        "d": 4,
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
    def test_android_arena_starts_algo_request(self):
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

        self.assertEqual(
            kernel.state.value,
            "WAITING_FOR_ALGO",
        )

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
            "COMPLETE",
        )

if __name__ == "__main__":
    unittest.main()
