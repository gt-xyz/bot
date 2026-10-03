# Development

## Container

`bee up {{name}}` builds and attaches the devcontainer, on image `{{image}}`.

## Checks

These three are the whole verification story.

    cmake -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

`test/smoke.cpp` exists so a fresh repo has something real to build. Replace it
rather than adding around it.

## Extra dependencies

Add a `.devcontainer/Dockerfile` starting `FROM {{image}}`, then swap the
`"image"` key in `devcontainer.json` for `"build": { "dockerfile": "Dockerfile" }`.
The images are shared by every project on this profile, so never edit one for a
single project's sake.
