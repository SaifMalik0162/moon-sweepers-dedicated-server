"""A ready native state report must also have a fresh heartbeat."""
import json
from pathlib import Path
import sys
import time

try:
    path = Path("/data/logs/moon-host-state.json")
    state = json.loads(path.read_text())
    age = time.time() - path.stat().st_mtime
except (OSError, ValueError):
    sys.exit(1)
sys.exit(0 if state.get("ready") is True and -2 <= age <= 15 else 1)
