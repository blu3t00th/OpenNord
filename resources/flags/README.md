# Country flags

128 × 128 PNGs rendered from the square SVG flags in
[lipis/flag-icons](https://github.com/lipis/flag-icons), revision
`086f7e97d657358203916dbe84f61c2bccaa81eb`. The upstream MIT license is included
in `LICENSE.txt`, embedded in the application, and installed in `licenses/`.

All two-letter country/territory flags are included, plus upstream regional
codes. The `xx` placeholder is excluded. UK and EL are normalized to GB and GR
by the GUI. Flags are loaded from compiled Qt resources and work offline.

To regenerate: install `resvg-py==0.5.0` in a development environment and run
`python scripts/Generate-Flags.py`. This step is not needed to build OpenNord.
