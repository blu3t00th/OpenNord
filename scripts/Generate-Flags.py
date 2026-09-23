"""Regenerate bundled flags; development-only dependency: resvg-py==0.5.0.

Normal CMake builds use checked-in PNG files and do not need Python/network/SVG.
"""
import argparse
from io import BytesIO
from pathlib import Path
import re
import urllib.request
from zipfile import ZipFile

import resvg_py

REVISION = "086f7e97d657358203916dbe84f61c2bccaa81eb"
SOURCE = f"https://codeload.github.com/lipis/flag-icons/zip/{REVISION}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="Use an already downloaded upstream archive")
    args = parser.parse_args()
    data = args.archive.read_bytes() if args.archive else urllib.request.urlopen(SOURCE, timeout=30).read()
    root = Path(__file__).resolve().parents[1]
    destination = root / "resources" / "flags"
    destination.mkdir(parents=True, exist_ok=True)
    codes = []
    prefix = f"flag-icons-{REVISION}/"
    with ZipFile(BytesIO(data)) as archive:
        for name in sorted(archive.namelist()):
            if not name.startswith(prefix + "flags/1x1/"):
                continue
            code = Path(name).stem
            if not name.endswith(".svg") or not re.fullmatch(r"[a-z]{2}", code) or code == "xx":
                continue
            image = resvg_py.svg_to_bytes(svg_string=archive.read(name).decode("utf-8"),
                                         width=128, height=128, skip_system_fonts=True)
            (destination / f"{code}.png").write_bytes(image)
            codes.append(code)
        (destination / "LICENSE.txt").write_bytes(archive.read(prefix + "LICENSE"))
    assert len(codes) >= 250, "Incomplete upstream flag collection"
    entries = "\n".join(f'    <file alias="{code}.png">flags/{code}.png</file>' for code in codes)
    (root / "resources" / "flags.qrc").write_text(
        '<RCC>\n  <qresource prefix="/flags">\n' + entries
        + '\n    <file alias="LICENSE.txt">flags/LICENSE.txt</file>\n  </qresource>\n</RCC>\n', encoding="utf-8")
    print(f"Bundled {len(codes)} flag icons from {REVISION}")


if __name__ == "__main__":
    main()
