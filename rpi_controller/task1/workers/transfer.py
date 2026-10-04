"""Optional RPi-to-PC image-transfer worker for Task 1."""

from __future__ import annotations

from dataclasses import dataclass
import os
import subprocess
import threading
from typing import Optional

from task1.events import Event, EventType


@dataclass(frozen=True)
class TransferConfig:
    host: str
    username: str
    destination: str
    identity_file: Optional[str] = None

    @classmethod
    def from_env(cls) -> Optional["TransferConfig"]:
        host, username, destination = os.getenv("MDP_PC_HOST"), os.getenv("MDP_PC_USER"), os.getenv("MDP_PC_DEST")
        if not all((host, username, destination)):
            return None
        return cls(host, username, destination, os.getenv("MDP_PC_SSH_KEY"))


class ImageTransferWorker(threading.Thread):
    """Owns PC transfer. Failures retain the RPi files and only emit events."""

    def __init__(self, requests, events, shutdown, config: Optional[TransferConfig]):
        super().__init__(name="IMAGE-TRANSFER")
        self.requests, self.events, self.shutdown, self.config = requests, events, shutdown, config

    def run(self):
        while True:
            request = self.requests.get()
            if request is None:
                return
            if self.config is None:
                self.events.put(Event(EventType.IMAGE_TRANSFER_ERROR, {"image_path": request.image_path, "error": "PC transfer is not configured"}))
                continue
            cmd = ["scp"]
            if self.config.identity_file:
                cmd.extend(["-i", self.config.identity_file])
            cmd.extend([request.image_path, request.metadata_path, "%s@%s:%s" % (self.config.username, self.config.host, self.config.destination)])
            try:
                subprocess.run(cmd, check=True, timeout=30, capture_output=True, text=True)
            except (OSError, subprocess.SubprocessError) as error:
                self.events.put(Event(EventType.IMAGE_TRANSFER_ERROR, {"image_path": request.image_path, "error": str(error)}))


