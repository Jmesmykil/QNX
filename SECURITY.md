# Security

This is a homebrew project for Nintendo Switch. It runs only inside Atmosphere CFW. It does not phone home. It does not handle credentials. It does not have a network surface other than what the operating system itself exposes.

If you find something concerning anyway:

## Reporting

Open a private security advisory on the GitHub Security tab. If that is not available, open a regular issue with `[security]` in the title and avoid posting exploit details in the public thread until the maintainer responds.

## Scope

- Q OS uMenu binary
- Q OS uSystem binary
- Q OS uLoader (the hbloader replacement) binary
- Q OS uManager NRO
- The romfs assets shipped in the release bundle

Out of scope:

- Atmosphere CFW itself (report to the Atmosphere project)
- libnx (report to the switchbrew project)
- Hekate (report to the Hekate project)
- The Switch operating system itself
- Anything that requires modifying the Switch hardware

## Fix timeline

This is a solo project. Fix timelines depend on severity and the maintainer's availability. Critical issues that put user data at risk get prioritized.

## Disclosure

The maintainer prefers coordinated disclosure. Report privately, give time to ship a fix, then disclose publicly with credit to the reporter if they want it.
