# Development

## Tools

Work happens in a container image shared by every project, and nothing can be
installed into it from inside. It carries python with uv, ruff and pytest, and node with npm.

Pin what the project depends on in the repository itself, in a lockfile where
the language has one. A tool that is missing from the image is written down
here with the version wanted, for the owner to add; it is not worked around.

## Checks

    ruff check .
    pytest

Add node checks here if the project grows any.
