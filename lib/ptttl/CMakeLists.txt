# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_sources(
    src/ptttl_parser.c
    src/ptttl_sample_generator.c
)
# Expose headers to all targets in the build
zephyr_include_directories(src)
