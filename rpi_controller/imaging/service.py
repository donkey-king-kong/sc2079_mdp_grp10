"""Capture and inference orchestration for the controller."""

from __future__ import annotations

from datetime import datetime
from pathlib import Path
import re
from collections import defaultdict
from dataclasses import dataclass
from statistics import median

from PIL import Image, ImageDraw

from .camera import CameraCapture
from .detector import (
    DetectionResult,
    LocalYoloDetector,
    is_bullseye_target,
    symbol_for_target,
    valid_detections,
)
from .enhancement import contrast_variants, map_rotated_bbox_to_source, rotate_frame


DEFAULT_MODEL_PATH = Path(__file__).with_name("model") / "best.pt"
DATA_DIR = Path(__file__).parents[1] / "imaging" / "data"


@dataclass(frozen=True)
class CandidateAggregate:
    """One class merged over physical camera frames, not inference passes."""

    target_id: str
    frame_scores: tuple[float, ...]
    detections: tuple[DetectionResult, ...]
    best_frame: object
    best_detection: DetectionResult

    @property
    def frames_seen(self) -> int:
        return len(self.detections)

    @property
    def avg_confidence(self) -> float:
        # frame_scores includes zeros for physical frames where this class was absent.
        return sum(self.frame_scores) / len(self.frame_scores)

    @property
    def median_bbox_area(self) -> float:
        return median(_bbox_area(detection.bbox) for detection in self.detections)

    @property
    def median_center_x(self) -> float:
        return median(_bbox_center_x(detection.bbox) for detection in self.detections)


def _bbox_area(bbox):
    assert bbox is not None
    return (bbox[2] - bbox[0]) * (bbox[3] - bbox[1])


def _bbox_center_x(bbox):
    assert bbox is not None
    return (bbox[0] + bbox[2]) / 2


def _image_path(symbol: str | None, data_dir: Path) -> Path:
    """Create a collision-safe, second-resolution image filename."""
    data_dir.mkdir(parents=True, exist_ok=True)
    safe_symbol = re.sub(r"[^A-Za-z0-9_-]+", "_", str(symbol or "no_symbol")).strip("_") or "no_symbol"
    stem = f"{datetime.now():%Y%m%d_%H%M%S}_{safe_symbol}"
    path = data_dir / f"{stem}.jpg"
    suffix = 2
    while path.exists():
        path = data_dir / f"{stem}_{suffix}.jpg"
        suffix += 1
    return path


def save_detection(frame, result: DetectionResult, obstacle_id: object, data_dir: Path) -> Path:
    """Save one detected target with its bounding box, ID and symbol."""
    image = Image.fromarray(frame)
    draw = ImageDraw.Draw(image)
    draw.rectangle(result.bbox, outline="lime", width=6)

    symbol = symbol_for_target(result.target_id)
    label = f"ID {result.target_id}" + (f" ({symbol})" if symbol else "")
    x1, y1, _, _ = result.bbox
    label_y = max(0, y1 - 20)
    draw.rectangle((x1, label_y, x1 + len(label) * 7, label_y + 20), fill="black")
    draw.text((x1 + 2, label_y + 2), label, fill="lime")

    detected_symbol = symbol_for_target(result.target_id) or result.target_id
    path = _image_path(detected_symbol, data_dir)
    image.save(path, format="JPEG")
    print(f"[IMAGING] Saved {path}")
    return path


def save_capture(frame, data_dir: Path) -> Path:
    """Persist a SNAP image even when no target symbol was detected."""
    path = _image_path("no_symbol", data_dir)
    Image.fromarray(frame).save(path, format="JPEG")
    print(f"[IMAGING] Saved {path}")
    return path


class ImagingService:
    """Run one camera capture and, when available, one local inference."""

    def __init__(
        self,
        model_path: str | Path = DEFAULT_MODEL_PATH,
        confidence: float = 0.30,
        width: int = 1640,
        height: int = 1232,
        warmup_seconds: float = 1.0,
        sample_count: int = 5,
        min_frames_seen: int = 3,
        min_avg_confidence: float = 0.70,
        bbox_area_similarity_ratio: float = 0.10,
        confidence_tie_epsilon: float = 0.01,
        rotation_angles: tuple[int, ...] = (-45, -30, -15, 0, 15, 30, 45),
        max_bbox_area_ratio: float = 0.50,
        data_dir: str | Path = DATA_DIR,
        use_contrast_variants: bool = True,
    ):
        self.model_path = Path(model_path)
        self.width = width
        self.height = height
        self.warmup_seconds = warmup_seconds
        if sample_count < 1:
            raise ValueError("sample_count must be at least 1")
        if not 1 <= min_frames_seen <= sample_count:
            raise ValueError("min_frames_seen must be between 1 and sample_count")
        if not 0.0 <= min_avg_confidence <= 1.0:
            raise ValueError("min_avg_confidence must be between 0 and 1")
        if not 0.0 <= bbox_area_similarity_ratio < 1.0:
            raise ValueError("bbox_area_similarity_ratio must be in [0, 1)")
        if confidence_tie_epsilon < 0.0:
            raise ValueError("confidence_tie_epsilon must be non-negative")
        if not rotation_angles or 0 not in rotation_angles or any(abs(angle) > 45 for angle in rotation_angles):
            raise ValueError("rotation_angles must include 0 and stay within +/-45 degrees")
        if not 0.0 < max_bbox_area_ratio <= 1.0:
            raise ValueError("max_bbox_area_ratio must be in (0, 1]")
        self.sample_count = sample_count
        self.min_frames_seen = min_frames_seen
        self.min_avg_confidence = min_avg_confidence
        self.bbox_area_similarity_ratio = bbox_area_similarity_ratio
        self.confidence_tie_epsilon = confidence_tie_epsilon
        self.rotation_angles = rotation_angles
        self.use_contrast_variants = use_contrast_variants
        self.max_bbox_area_ratio = max_bbox_area_ratio
        self.data_dir = Path(data_dir)
        self.detector = LocalYoloDetector(self.model_path, confidence) if self.model_path.is_file() else None
        self._camera: CameraCapture | None = None

    def start(self) -> None:
        """Open the camera and load the model once for the worker lifetime."""
        if self._camera is None:
            self._camera = CameraCapture(self.width, self.height, self.warmup_seconds)
            print("[IMAGING] Camera ready")
        # Load weights before the first SNAP, rather than adding that delay to
        # the first requested capture. Inference itself remains per SNAP.
        if self.detector is not None:
            self.detector.load()
            print("[IMAGING] Model ready")

    def close(self) -> None:
        """Release the persistent camera session during worker shutdown."""
        if self._camera is not None:
            self._camera.close()
            self._camera = None

    def capture_and_predict(
        self,
        obstacle_id: object,
        *,
        save_on_no_detection: bool = True,
        log_no_detection: bool = True,
        expected_region: str | None = None,
    ) -> dict[str, object | None]:
        """Capture one frame and return the existing controller result shape.

        SNAP requests save a frame even without a target. Continuous polling
        skips blank frames to avoid filling the RPi disk.
        """
        # Task1's CameraCVWorker calls start() once. Retain this short-lived
        # fallback so existing one-shot callers still work unchanged.
        frames = self.capture_samples()
        return self.predict_captured(
            obstacle_id,
            frames,
            save_on_no_detection=save_on_no_detection,
            log_no_detection=log_no_detection,
            expected_region=expected_region,
        )

    def capture_samples(self):
        """Capture the physical frames for a SNAP, without running YOLO."""
        return self._capture_samples()

    def predict_captured(
        self,
        obstacle_id: object,
        frames,
        *,
        save_on_no_detection: bool = True,
        log_no_detection: bool = True,
        expected_region: str | None = None,
    ) -> dict[str, object | None]:
        """Run inference and persistence for frames captured earlier."""
        frame, result = self._select_from_samples(frames, expected_region=expected_region)

        image_path = None
        if self.detector is None and log_no_detection:
            print(f"[IMAGING] Model weights not found at {self.model_path}; capture only")
        elif not result.found and log_no_detection:
            print("[IMAGING] No target detected")
        elif result.bbox is not None:
            image_path = save_detection(frame, result, obstacle_id, self.data_dir)

        if image_path is None and save_on_no_detection:
            image_path = save_capture(frame, self.data_dir)

        return {
            "obstacle_id": str(obstacle_id),
            "image_id": result.target_id,
            "confidence": result.confidence,
            "bbox": result.bbox,
            "image_path": str(image_path) if image_path else None,
        }

    def _capture_samples(self):
        """Capture the configurable burst using the existing camera session."""
        if self._camera is not None:
            return tuple(self._camera.capture() for _ in range(self.sample_count))
        with CameraCapture(self.width, self.height, self.warmup_seconds) as camera:
            return tuple(camera.capture() for _ in range(self.sample_count))

    def _select_from_samples(self, frames, *, expected_region: str | None = None):
        """Select a reliable target using physical-frame persistence then geometry."""
        if self.detector is None:
            return frames[0], DetectionResult.not_found()

        scores: dict[str, list[float]] = defaultdict(lambda: [0.0] * len(frames))
        observations: dict[str, list[DetectionResult]] = defaultdict(list)
        best_by_class: dict[str, tuple[object, DetectionResult]] = {}
        for sample_index, frame in enumerate(frames, start=1):
            print(f"[IMAGING] Sample {sample_index}:")
            # Rotation and contrast are inference passes for one physical frame,
            # never additional temporal votes.  Bboxes return to upright space.
            per_class: dict[str, tuple[object, DetectionResult]] = {}
            for angle in self.rotation_angles:
                rotated_frame = rotate_frame(frame, angle)
                inference_frames = (
                    (rotated_frame, *contrast_variants(rotated_frame))
                    if self.use_contrast_variants
                    else (rotated_frame,)
                )
                for inference_frame in inference_frames:
                    raw_detections = self.detector.detect_all(inference_frame)
                    for detection in raw_detections:
                        label = detection.target_id or "unknown"
                        confidence = detection.confidence or 0.0
                        if detection.target_id is not None and is_bullseye_target(detection.target_id):
                            print(f"[IMAGING]   {label} {confidence:.2f} -> ignored (bullseye)")
                        else:
                            print(f"[IMAGING]   {label} {confidence:.2f}")
                    for detection in valid_detections(raw_detections, inference_frame.shape):
                        target_id = detection.target_id
                        assert target_id is not None and detection.bbox is not None
                        upright_detection = DetectionResult(
                            True, target_id, detection.confidence,
                            map_rotated_bbox_to_source(detection.bbox, frame.shape, inference_frame.shape, angle),
                        )
                        if _bbox_area(upright_detection.bbox) > frame.shape[0] * frame.shape[1] * self.max_bbox_area_ratio:
                            print(f"[IMAGING]   {target_id} ignored (bbox covers too much of frame)")
                            continue
                        if (target_id not in per_class or
                                (upright_detection.confidence or 0.0) > (per_class[target_id][1].confidence or 0.0)):
                            per_class[target_id] = (frame, upright_detection)

            for target_id, (best_frame, detection) in per_class.items():
                scores[target_id][sample_index - 1] = detection.confidence or 0.0
                observations[target_id].append(detection)
                if (target_id not in best_by_class or
                        (detection.confidence or 0.0) > (best_by_class[target_id][1].confidence or 0.0)):
                    best_by_class[target_id] = (best_frame, detection)

        aggregates = tuple(
            CandidateAggregate(
                target_id=target_id,
                frame_scores=tuple(sample_scores),
                detections=tuple(observations[target_id]),
                best_frame=best_by_class[target_id][0],
                best_detection=best_by_class[target_id][1],
            )
            for target_id, sample_scores in scores.items()
        )
        for candidate in sorted(aggregates, key=lambda item: item.target_id):
            print(
                "[IMAGING] Aggregated: %s: seen=%d/%d avg=%.2f median_area=%.0f center_x=%.0f"
                % (candidate.target_id, candidate.frames_seen, len(frames), candidate.avg_confidence,
                   candidate.median_bbox_area, candidate.median_center_x)
            )
        if not aggregates:
            return frames[0], DetectionResult.not_found()

        reliable = tuple(
            candidate for candidate in aggregates
            if candidate.frames_seen >= self.min_frames_seen
            and candidate.avg_confidence >= self.min_avg_confidence
        )
        for candidate in aggregates:
            if candidate not in reliable:
                print(
                    "[IMAGING] Rejected %s: seen=%d/%d (need %d), avg=%.2f (need %.2f)"
                    % (candidate.target_id, candidate.frames_seen, len(frames), self.min_frames_seen,
                       candidate.avg_confidence, self.min_avg_confidence)
                )
        if not reliable:
            print("[IMAGING] RETRY: no reliable target")
            return frames[0], DetectionResult.not_found()

        region = _normalise_region(expected_region)
        if region is not None:
            in_region = tuple(candidate for candidate in reliable if _is_in_region(candidate, region, frames[0].shape[1]))
            if not in_region:
                print(f"[IMAGING] RETRY: no reliable target in expected {region} region")
                return frames[0], DetectionResult.not_found()
            reliable = in_region

        selected = _select_by_geometry(
            reliable,
            similarity_ratio=self.bbox_area_similarity_ratio,
            confidence_tie_epsilon=self.confidence_tie_epsilon,
        )
        if selected is None:
            print("[IMAGING] RETRY: reliable targets remain geometrically ambiguous")
            return frames[0], DetectionResult.not_found()

        assert not is_bullseye_target(selected.target_id), "bullseye must never be selected"
        selected_result = DetectionResult(
            found=True,
            target_id=selected.target_id,
            confidence=selected.avg_confidence,
            bbox=selected.best_detection.bbox,
        )
        print(
            "[IMAGING] Selected: %s (avg=%.2f median_area=%.0f)"
            % (selected.target_id, selected.avg_confidence, selected.median_bbox_area)
        )
        return selected.best_frame, selected_result


def _normalise_region(expected_region: str | None) -> str | None:
    if expected_region is None:
        return None
    region = expected_region.upper()
    if region not in {"LEFT", "CENTER", "RIGHT"}:
        raise ValueError("expected_region must be LEFT, CENTER, RIGHT, or None")
    return region


def _is_in_region(candidate: CandidateAggregate, region: str, image_width: int) -> bool:
    """Use equal image thirds as the optional scan-direction ROI convention."""
    left_boundary, right_boundary = image_width / 3, image_width * 2 / 3
    if region == "LEFT":
        return candidate.median_center_x < left_boundary
    if region == "RIGHT":
        return candidate.median_center_x > right_boundary
    return left_boundary <= candidate.median_center_x <= right_boundary


def _select_by_geometry(
    candidates: tuple[CandidateAggregate, ...], *, similarity_ratio: float, confidence_tie_epsilon: float,
) -> CandidateAggregate | None:
    """Prefer the nearest reliable target; confidence only breaks area ties."""
    if len(candidates) == 1:
        return candidates[0]
    largest_area = max(candidate.median_bbox_area for candidate in candidates)
    similar = tuple(
        candidate for candidate in candidates
        if candidate.median_bbox_area >= largest_area * (1 - similarity_ratio)
    )
    if len(similar) == 1:
        return similar[0]
    highest_confidence = max(candidate.avg_confidence for candidate in similar)
    confidence_winners = tuple(
        candidate for candidate in similar
        if highest_confidence - candidate.avg_confidence <= confidence_tie_epsilon
    )
    return confidence_winners[0] if len(confidence_winners) == 1 else None
