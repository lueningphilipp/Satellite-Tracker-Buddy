# Contributing

This is a hobby project, kept small on purpose - see `README.md` for what
it does and `firmware/README.md` for the firmware build.

- By submitting a pull request, you agree your contribution is licensed
  under this project's MIT license (see `LICENSE`).
- Keep the demo (`demo/sat_display_demo.py`) and the firmware renderer
  visually identical - if you're changing how something is drawn, change
  the Python demo first and check it there before porting it to
  `firmware/src/display/`.
- Please don't add a dependency without a short comment saying why it's
  needed.
- Open an issue before a large or invasive change - some parts of this
  (async web-server handlers, GPIO0, element parsing, secret fields) have
  hard-won constraints from real bugs on physical hardware; ask first if
  you're not sure whether one applies.
