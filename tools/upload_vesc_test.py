"""Build and flash only the standalone fake VESC CYD firmware."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from upload_test_sender import run  # noqa: E402

if __name__ == "__main__":
    run("vesc")
