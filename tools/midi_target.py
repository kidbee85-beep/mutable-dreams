# SPDX-License-Identifier: MIT OR GPL-3.0-only
"""Select one exact MIDI pair before opening it; never fall back to another FM-1."""
DEFAULT_PORT = 'Mutable Dreams FM-1'

def select_port(mido, requested=None):
    name = requested or DEFAULT_PORT
    inputs, outputs = mido.get_input_names(), mido.get_output_names()
    if inputs.count(name) != 1 or outputs.count(name) != 1:
        raise ValueError(f'Need one unique input/output named {name!r}; no device opened. '
                         'Use an exact --port name, or disconnect the duplicate device.')
    return name
