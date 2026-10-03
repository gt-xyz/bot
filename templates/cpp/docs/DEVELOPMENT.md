# Development

## Tools

Work happens in a container image shared by every project, and nothing can be
installed into it from inside. It carries cmake, ninja, clang and gcc.

Pin what the project depends on in the repository itself, in a lockfile where
the language has one. A tool that is missing from the image is written down
here with the version wanted, for the owner to add; it is not worked around.

## Checks

These three are the whole verification story.

    cmake -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

`test/smoke.cpp` exists so a fresh repo has something real to build. Replace it
rather than adding around it.
