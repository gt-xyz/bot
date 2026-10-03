# Development

## Container

`bee up {{name}}` builds and attaches the devcontainer, on image `{{image}}`:
python with uv, and node.

## Checks

    ruff check .
    pytest

Add node checks here if the project grows any.
