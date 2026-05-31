# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_sources(
    src/midi_smf.c
    src/midi_synth.c
)
zephyr_include_directories(src)
