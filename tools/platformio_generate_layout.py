from __future__ import annotations

import subprocess
import sys
from pathlib import Path


Import("env")  # type: ignore[name-defined]  # noqa: F821

ROOT = Path(env["PROJECT_DIR"])  # type: ignore[name-defined]  # noqa: F821
for script in ("generate_layout_header.py", "logo/generate_splash.py"):
    subprocess.check_call([sys.executable, str(ROOT / "tools" / script)])
