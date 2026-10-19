"""Source-checkout entry point that works without installing the package."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from phantomcrash_research.cli import main  # noqa: E402

sys.exit(main())
