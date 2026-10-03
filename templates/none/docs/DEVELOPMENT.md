# Development

## Tools

Work happens in a container image shared by every project, and nothing can be
installed into it from inside. This profile is notes and documents, so it needs no toolchain.

Pin what the project depends on in the repository itself, in a lockfile where
the language has one. A tool that is missing from the image is written down
here with the version wanted, for the owner to add; it is not worked around.

## Checks

There are none, and that is deliberate: a check that passes without running
anything makes a green result meaningless.
