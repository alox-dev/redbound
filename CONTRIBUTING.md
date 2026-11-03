# Contributing to Redbound

Thanks for your interest in Redbound. Issues and pull requests are welcome; every change is reviewed before it is
merged.

## Reporting issues

Open an issue with:

- the platform and device (for example "Android 13, Samsung Galaxy Note 10+" or "Debian 13, Intel Iris Plus"),
- the Redbound version or commit,
- what happened and what you expected,
- the console output (`redbound` in a terminal, or `adb logcat -s Redbound` on Android).

Never attach files from the original game.

## Branches

The project follows Git Flow:

- `master` holds released versions only.
- `develop` is the integration branch.
- Work happens in `feature/<name>` or `fix/<name>` branches created from `develop`, and pull requests target
  `develop`.
- Releases are merged from `develop` into `master` and tagged (`v0.1.0`).

## Pull requests

- Keep each pull request focused on one change.
- Build without warnings and make sure `ctest` passes (see `docs/building.md`).
- Test on at least one real device and describe what you checked.
- Code, identifiers, messages and documentation are written in English. The code has no comments: explain design
  decisions in `docs/` instead.
- Player facing text in the port menu goes in `assets/lang/*.lang`, never hardcoded.
- Do not include any data from the original game.

## AI assisted contributions

Redbound itself was written with the help of Claude (Anthropic) and is reviewed, tested and maintained by its
author. AI assisted contributions are welcome under the same rule: say so in the pull request, and make sure you
have reviewed, understood and tested every line you submit. Pull requests that look unreviewed (unrelated changes,
code that does not build, invented APIs, secrets or credentials) will be closed.

## License

By contributing you agree that your contribution is licensed under the GNU General Public License v2, the license of
the project.
