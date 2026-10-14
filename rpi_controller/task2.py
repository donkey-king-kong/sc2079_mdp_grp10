"""Task 2 Pi orchestrator for TASK2_PI_HANDOVER.md (12 October).

Edit the configuration block below on the Raspberry Pi, then run:

    python3 task2.py
"""
from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from enum import Enum
import json
from pathlib import Path
import subprocess
import time
from typing import TYPE_CHECKING, Protocol

from connectors.stm import STMConnector, fields, parse_response

if TYPE_CHECKING:
    from imaging.service import ImagingService

DEFAULT_TASK2_MODEL = Path(__file__).resolve().parent / "imaging" / "model" / "best_JH.pt"


# ---- Task 2 run configuration ----------------------------------------------
# Keep these settings in the same style as task1.py.  In particular, change
# BLUETOOTH_DEVICE to "/dev/rfcomm1" here if the Bluetooth service creates
# that device on this Pi.
STM_PORT = "/dev/ttyACM0"
BLUETOOTH_DEVICE = "/dev/rfcomm0"
# Same startup behaviour as task1.py. The tablet reconnects after this reset.
RESET_BLUETOOTH_ON_START = True
BLUETOOTH_SERVICE = "mdp-bluetooth.service"

# Competition: False.  Bench-only: True bypasses Android beginFastest; it will
# immediately send ZH then GO, so use only when the robot is ready to move.
CLI_START_OVERRIDE = False
ENABLE_BLUETOOTH = True
# The current tablet Task 2 button sends a valid `sendArena` JSON message.
# Treat its first arrival as START as well as the newer `beginFastest` signal.
# This is intentionally limited to known tablet commands, not arbitrary bytes.
ACCEPT_SEND_ARENA_AS_START = True
# Try to recognize obstacle 1 while the robot is stationary and the operator
# is preparing to press Start. If absent/unconfirmed, use the normal S1 scan.
PRESTART_FIRST_ARROW_SCAN = True
PRESTART_SCAN_TIMEOUT_SECONDS = 2.0

# Model weights live outside Git and must be copied to imaging/model/ on Pi.
CV_MODEL_FILENAME = "best_JH.pt"
ZH_TIMEOUT_SECONDS = 2.0
APPROACH_TIMEOUT_SECONDS = 25.0
FINISH_TIMEOUT_SECONDS = 60.0
SCAN_TIMEOUT_SECONDS = 10.0
CV_CONFIDENCE = 0.6
CV_MIN_BBOX_AREA = 900
CV_CROP_Y = None  # Set after physical vertical crop calibration.
# Fewer physical captures reduce recognition latency. Two valid consecutive
# detections are still required, even when this is 1.
CV_SAMPLE_COUNT = 3
# Number of agreeing fresh detections required before replying to STM. Set to
# 1 for the fastest one-capture/one-inference scan per arrow.
CV_CONFIRMATION_FRAMES = 1
# Each fresh crop is inferred once as raw plus each enabled CLAHE variant.
# Disable a variant to reduce per-frame inference work.
CV_USE_GRAY_CLAHE = False
CV_USE_LAB_CLAHE = False
# Scan 2 crop (STM firmware step 8+). At the scan 2 stop the robot now turns to
# face the arrow card, so the card is near the image centre: 13 Oct, arrow 1 =
# L, card centres at x = 822 / 908 / 1008 / >1064 of 1640 (0-9 deg right of
# centre). The old side crop (x 1000-1640) missed it. Left edges of the 640 px
# crop, in 1640 px units, tried one per frame in turn (all three together cover
# x ~180-1480). First entry = most likely position for that lane.
SCAN2_CROP_X = {"L": (620, 900, 340), "R": (380, 100, 660)}


def reset_bluetooth_service() -> None:
    """Make Task 2 establish RFCOMM exactly as Task 1 does."""
    if not RESET_BLUETOOTH_ON_START or not ENABLE_BLUETOOTH:
        return
    print(f"[BT] Restarting {BLUETOOTH_SERVICE}...")
    subprocess.run(["sudo", "systemctl", "restart", BLUETOOTH_SERVICE], check=True)
    print("[BT] Service restarted; waiting for Android to connect...")


class Task2State(str, Enum):
    INIT = "INIT"; READY = "READY"; WAITING_FOR_TABLET = "WAITING_FOR_TABLET"; START = "START"; ZH = "ZH"
    GO = "GO"; SCAN_1 = "SCAN_1"; TO_SCAN_2 = "TO_SCAN_2"; SCAN_2 = "SCAN_2"; FINISHING = "FINISHING"
    FINISHED = "FINISHED"; FAULT = "FAULT"; ABORTED = "ABORTED"


class Task2Fault(RuntimeError): pass
class RecognitionError(Task2Fault): pass


@dataclass(frozen=True)
class Task2Config:
    zh_timeout_s: float = 2.0
    approach_timeout_s: float = 25.0
    finish_timeout_s: float = 60.0
    scan_timeout_s: float = 10.0
    prestart_scan_timeout_s: float = 1.5
    confirmation_frames: int = 1
    confidence: float = .80
    min_bbox_area: int = 900
    crop_size: int = 640
    crop_y: int | None = None  # calibrate T3; None uses the image midline
    clahe_variants: tuple[str, ...] = ("grayscale_clahe", "lab_clahe")
    fallback_policy: str = "fail_closed"  # never silently use handover's AR fallback

    def __post_init__(self):
        if self.confirmation_frames not in (1, 2):
            raise ValueError("confirmation_frames must be 1 or 2")


@dataclass(frozen=True)
class ArrowDecision:
    direction: str
    confidence: float
    evidence_path: str = ""


class STMPort(Protocol):
    def send_command(self, command: str) -> None: ...
    def read_ack(self) -> str | None: ...


class ArrowScanner:
    """Post-stop Task 2 scanning using the existing live camera and YOLO model."""
    def __init__(self, imaging: "ImagingService", evidence_dir: Path, config: Task2Config, log):
        self.imaging, self.evidence_dir, self.config, self.log = imaging, evidence_dir, config, log
        self._pending = None; self._crop_x = None

    def start(self):
        self.imaging.start()
        if self.imaging.detector is None: raise RecognitionError("model weights unavailable")
        # Warm a normal 640px inference before the tablet starts its timer.
        # Kept lazy so host-side protocol tests do not require NumPy/YOLO.
        import numpy as np
        started = time.monotonic()
        self.imaging.detector.detect_all(np.zeros((self.config.crop_size, self.config.crop_size, 3), dtype=np.uint8))
        self.log("cv_warm", monotonic=started, ms=(time.monotonic() - started) * 1000)

    def close(self): self.imaging.close()

    def scan(self, scan_number: int, lane_direction: str | None = None, timeout_s: float | None = None) -> ArrowDecision:
        timeout_s = self.config.scan_timeout_s if timeout_s is None else timeout_s
        deadline, prior, attempts, last_frame = time.monotonic() + timeout_s, None, 0, None
        while time.monotonic() < deadline:
            capture_started = time.monotonic()
            try: frames = self.imaging.capture_samples()
            except Exception as error: raise RecognitionError("camera capture failed: %s" % error) from error
            self.log("capture", scan=scan_number, monotonic=capture_started, ms=(time.monotonic() - capture_started) * 1000)
            for frame in frames:
                last_frame = frame; attempts += 1
                selected = self._detect(frame, scan_number, lane_direction, attempts)
                if selected is None:
                    prior = None; continue
                image, detection = selected; direction = "L" if detection.target_id == "39" else "R"
                if self.config.confirmation_frames == 1 or prior == direction:
                    self._pending = (image, detection, scan_number)
                    return ArrowDecision(direction, detection.confidence or 0.0)
                prior = direction
        # Fail closed is deliberate: the handover's highest-score/AR timeout
        # fallback is configurable only by code change because it risks DQ.
        self._pending = None
        if last_frame is not None: self._save_raw(last_frame)
        raise RecognitionError("scan %d was not confirmed within %.1fs" % (scan_number, timeout_s))

    def persist_last(self) -> str:
        """Save evidence only after the controller has sent AL/AR."""
        if self._pending is None: return ""
        from imaging.detector import DetectionResult
        from imaging.service import save_detection
        frame, detection, scan = self._pending
        self._pending = None
        return str(save_detection(frame, DetectionResult(True, detection.target_id, detection.confidence, detection.bbox), "task2_%d" % scan, self.evidence_dir))

    def _save_raw(self, frame):
        from imaging.service import save_capture
        save_capture(frame, self.evidence_dir)

    def _detect(self, frame, scan, lane, attempt):
        assert self.imaging.detector is not None
        # One fresh physical frame produces one calibrated Task 2 crop.  Run
        # raw + explicitly enabled preprocessing only: 1, 2, or 3 inferences
        # total per frame, never a full-frame fallback or late retry burst.
        image = next(iter(self._crops(frame, scan, lane, attempt)))
        variants = [("raw", image)]
        if self.config.clahe_variants:
            from imaging.preprocessing import task1_preprocessing_variants
            prepared = task1_preprocessing_variants(image)
            variants.extend((name, prepared[name]) for name in self.config.clahe_variants)
        candidates = []
        for variant_name, variant in variants:
            started = time.monotonic()
            for detection in self.imaging.detector.detect_all(variant):
                if self._valid(detection, variant.shape): candidates.append((image, detection))
            self.log("inference", scan=scan, monotonic=started, ms=(time.monotonic() - started) * 1000, variant=variant_name)
        self.log("scan_frame", scan=scan, attempt=attempt, crop_x=self._crop_x, candidates=[d.target_id for _, d in candidates])
        return candidates[0] if len(candidates) == 1 else None

    def _crops(self, frame, scan, lane, attempt=1):
        height, width = frame.shape[:2]; size = self.config.crop_size
        if height < size or width < size: raise RecognitionError("frame smaller than native 640px crop")
        if scan == 1: x = round(width * 500 / 1640)
        elif lane in SCAN2_CROP_X:
            xs = SCAN2_CROP_X[lane]; x = round(width * xs[(attempt - 1) % len(xs)] / 1640)
        else: raise RecognitionError("scan 2 missing first-arrow lane direction")
        x = max(0, min(width - size, x)); self._crop_x = x; y = (height - size) // 2 if self.config.crop_y is None else self.config.crop_y
        y = max(0, min(height - size, y)); crop = frame[y:y + size, x:x + size]
        if crop.shape[:2] != (size, size): raise RecognitionError("invalid Task 2 crop")
        return (crop,)

    def _valid(self, detection, shape):
        if detection.target_id not in {"38", "39"} or detection.confidence is None or detection.confidence < self.config.confidence or detection.bbox is None: return False
        x1, y1, x2, y2 = detection.bbox; height, width = shape[:2]; area = (x2 - x1) * (y2 - y1); cx, cy = (x1+x2)/2, (y1+y2)/2
        return x1 < x2 <= width and y1 < y2 <= height and area >= self.config.min_bbox_area and width*.1 <= cx <= width*.9 and height*.1 <= cy <= height*.9


class Task2Controller:
    def __init__(self, port: STMPort, scanner, config: Task2Config, log):
        self.port, self.scanner, self.config, self.log = port, scanner, config, log
        self.state, self.decisions = Task2State.INIT, []
        self.preloaded_first: ArrowDecision | None = None

    def prepare(self):
        self.scanner.start(); self._transition(Task2State.READY)

    def preload_first_arrow(self) -> None:
        """Best-effort pre-start recognition; failure never blocks normal S1 scan."""
        if not PRESTART_FIRST_ARROW_SCAN:
            return
        try:
            started = time.monotonic()
            self.preloaded_first = self.scanner.scan(1, timeout_s=self.config.prestart_scan_timeout_s)
            self.log("prestart_scan_confirmed", direction=self.preloaded_first.direction, monotonic=started)
        except RecognitionError as error:
            self.preloaded_first = None
            self.log("prestart_scan_unconfirmed", error=str(error))

    def run(self, *, explicit_start: bool, prepared: bool = False) -> Task2State:
        if not explicit_start: raise Task2Fault("a valid tablet start message is required")
        try:
            if not prepared: self.prepare()
            started = time.monotonic(); self._transition(Task2State.START)
            self._transition(Task2State.ZH); self._wait("ZH", "heading", self.config.zh_timeout_s)
            self._transition(Task2State.GO); self._wait("GO", "s1", self.config.approach_timeout_s)
            self._transition(Task2State.SCAN_1)
            first = self.preloaded_first
            if first is None:
                self.log("scan_1_normal")
                first = self.scanner.scan(1)
            else:
                self.log("scan_1_preloaded", direction=first.direction)
            self._transition(Task2State.TO_SCAN_2)
            self._wait("A" + first.direction, "s2", self.config.approach_timeout_s); first = self._persist(first); self.decisions.append(first)
            self._transition(Task2State.SCAN_2); second = self.scanner.scan(2, first.direction); self._transition(Task2State.FINISHING)
            self._wait("A" + second.direction, "fin", self.config.finish_timeout_s); second = self._persist(second); self.decisions.append(second)
            self._transition(Task2State.FINISHED); self.log("total", seconds=time.monotonic() - started)
        except KeyboardInterrupt: self._abort(Task2Fault("operator interrupt"))
        except Task2Fault as error: self._abort(error)
        except Exception as error: self._abort(Task2Fault("unexpected RPi error: %s" % error))
        finally: self.scanner.close()
        return self.state

    def _persist(self, decision):
        path = self.scanner.persist_last() if hasattr(self.scanner, "persist_last") else decision.evidence_path
        return ArrowDecision(decision.direction, decision.confidence, path)

    def _wait(self, command, expected, timeout):
        self.port.send_command(command); self.log("stm_tx", command=command, expected=expected, monotonic=time.monotonic())
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self.port.read_ack()
            if raw is None: continue
            response = parse_response(raw); self.log("stm_rx", raw=raw, kind=response.kind, fields=fields(response.body))
            if response.kind == "abort": raise Task2Fault("STM aborted: " + response.body)
            if response.kind in {"error", "unknown"}: raise Task2Fault("STM failure: " + response.body)
            if response.kind == expected: return response.body
            self.log("stm_ignored", raw=raw, expected=expected)
        raise Task2Fault("timeout waiting for %s; command was not resent" % command)

    def _abort(self, error):
        try: self.port.send_command("!"); self.log("stm_tx", command="!")
        except Exception as abort_error: self.log("abort_write_failed", error=str(abort_error))
        self.state = Task2State.ABORTED if "aborted" in str(error).lower() else Task2State.FAULT; self.log("fault", state=self.state.value, error=str(error))

    def _transition(self, state): self.state = state; self.log("state", state=state.value)


def _is_tablet_start(message) -> bool:
    if not isinstance(message, dict):
        return False
    if (message.get("cat") == "stm" and message.get("value") == "beginFastest"):
        return True
    if message.get("cat") == "beginFastest":
        return True
    return ACCEPT_SEND_ARENA_AS_START and message.get("cat") == "sendArena"


def wait_for_tablet_start(bluetooth, log):
    from connectors.bluetooth import BluetoothDisconnectedError
    log("tablet_wait")
    while True:
        try: messages = bluetooth.read_messages()
        except BluetoothDisconnectedError as error:
            log("tablet_disconnected", error=str(error)); bluetooth.disconnect(); reset_bluetooth_service(); bluetooth.connect(); continue
        for message in messages:
            if _is_tablet_start(message): log("tablet_start", monotonic=time.monotonic()); return
            log("tablet_ignored", message=message)


def deliver_evidence(paths, evidence_dir: Path, bluetooth, log):
    """Always retain local stitched evidence; Bluetooth failure is non-destructive."""
    from connectors.android import AndroidImageSender
    from imaging.stitcher import stitch_images
    stitched = stitch_images(paths, evidence_dir / "task2_stitched.jpg")
    if stitched is None: raise Task2Fault("cannot stitch Task 2 evidence")
    try:
        if not AndroidImageSender(bluetooth).send_image(stitched):
            raise OSError("Android image transfer failed")
        log("android_evidence", image=str(stitched))
    except Exception as error: log("android_evidence_failed", image=str(stitched), error=str(error))
    return stitched


def _run_dir():
    path = Path(__file__).resolve().parent / "imaging" / "data" / ("task2_" + datetime.now().strftime("%Y%m%d_%H%M%S")); path.mkdir(parents=True, exist_ok=False); return path


def main() -> int:
    from connectors.bluetooth import BluetoothConnector
    from imaging.service import ImagingService
    evidence = _run_dir(); logfile = evidence / "task2.jsonl"
    def log(event, **data):
        record = {"time": datetime.now().isoformat(timespec="milliseconds"), "event": event, **data}; print("[TASK2]", record); logfile.open("a", encoding="utf-8").write(json.dumps(record, default=str) + "\n")
    stm, bluetooth, controller = STMConnector(port=STM_PORT), None, None
    try:
        reset_bluetooth_service()
        stm.connect()
        if ENABLE_BLUETOOTH:
            bluetooth = BluetoothConnector(device=BLUETOOTH_DEVICE); bluetooth.connect()
        clahe_variants = tuple(name for name, enabled in (
            ("grayscale_clahe", CV_USE_GRAY_CLAHE),
            ("lab_clahe", CV_USE_LAB_CLAHE),
        ) if enabled)
        config = Task2Config(
            zh_timeout_s=ZH_TIMEOUT_SECONDS,
            approach_timeout_s=APPROACH_TIMEOUT_SECONDS,
            finish_timeout_s=FINISH_TIMEOUT_SECONDS,
            scan_timeout_s=SCAN_TIMEOUT_SECONDS,
            prestart_scan_timeout_s=PRESTART_SCAN_TIMEOUT_SECONDS,
            confirmation_frames=CV_CONFIRMATION_FRAMES,
            confidence=CV_CONFIDENCE,
            min_bbox_area=CV_MIN_BBOX_AREA,
            crop_y=CV_CROP_Y,
            clahe_variants=clahe_variants,
        )
        model_path = Path(__file__).resolve().parent / "imaging" / "model" / CV_MODEL_FILENAME
        print(f"[IMAGING] configured model: {model_path}")
        scanner = ArrowScanner(ImagingService(model_path=model_path, data_dir=evidence, sample_count=CV_SAMPLE_COUNT, min_frames_seen=1, rotation_angles=(0,), use_contrast_variants=False), evidence, config, log)
        controller = Task2Controller(stm, scanner, config, log); controller.prepare()
        controller.preload_first_arrow()
        if not CLI_START_OVERRIDE:
            if bluetooth is None: raise Task2Fault("tablet start requires ENABLE_BLUETOOTH = True")
            controller._transition(Task2State.WAITING_FOR_TABLET); wait_for_tablet_start(bluetooth, log)
        state = controller.run(explicit_start=True, prepared=True)
        if state is Task2State.FINISHED and bluetooth is not None: deliver_evidence([d.evidence_path for d in controller.decisions], evidence, bluetooth, log)
        return 0 if state is Task2State.FINISHED else 1
    except KeyboardInterrupt:
        try: stm.send_command("!")
        except Exception: pass
        return 130
    finally:
        if controller is not None: controller.scanner.close()
        if bluetooth: bluetooth.disconnect()
        stm.disconnect()


if __name__ == "__main__": raise SystemExit(main())
