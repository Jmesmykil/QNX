# Contributing

This is a solo project but pull requests are welcome. The codebase is GPLv2 and any contribution ships under the same terms.

## What to read first

1. `README.md` for what Q OS is and what it can already do
2. `CREDITS.md` for the upstream chain you are standing on
3. The Issues tab for what is already tracked

## Bug reports

Use the bug-report issue template. Include hardware, firmware, Atmosphere version, and the relevant log section if one was written.

## Feature requests

Use the feature-request issue template. Read the "designed but not yet implemented" section in the README first. Some requests are architecturally out of scope under the current Atmosphere display stack, particularly anything that needs NRO framebuffer capture.

## Pull requests

1. Fork the repo
2. Branch off `unew`
3. Build locally with `make package` against devkitPro / devkitA64 and confirm it produces the same `SdOut/` layout
4. Open the PR with the template filled in

The maintainer will respond. If hardware testing is needed and you do not have a Switch handy, say so in the PR and the maintainer will run it.

## Style

- Match the existing C++ style. The repo has no automated formatter.
- No em-dashes in markdown. Use periods or commas.
- No marketing fluff in commit messages. Subject-first sentences. Describe what changed and why.
- One change per commit. Mixed commits get sent back.

## License

GPLv2. Plutonium and Atmosphere-libs are GPLv2 and propagate through static linking, so the whole project must stay GPLv2. See `CREDITS.md` for the propagation chain.
