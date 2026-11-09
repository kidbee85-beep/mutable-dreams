#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Charles Vestal (LZMA, every SRAM load image)
# From Melodee (github.com/keremimo/melodee, tools/boot_audio.py), under MPL-2.0 here with permission; fm1-base's
# sections (.ram_text, .data: device/main_fm1.c fm1_cstart) and names, the rest as Melodee has it.
"""Pack the SRAM load images for boot and verify their decoder's XIP closure."""
import lzma
import re
import struct

MAGIC = 0x31414D4C  # LMA1: device/boot_lzma.h BOOT_LZMA_MAGIC
# Raw LZMA1, as boot_lzma.h decodes it: lc 1, lp 1, pb 1 (instructions are
# halfword aligned), with the end marker. The dictionary is the whole image.
FILTERS = [{'id': lzma.FILTER_LZMA1, 'preset': 9 | lzma.PRESET_EXTREME,
            'lc': 1, 'lp': 1, 'pb': 1, 'dict_size': 1 << 20}]
# The sections fm1_cstart unpacks, in their load order from _rt_load (device/app.ld, device/main_fm1.c)
SECTIONS = ('ramtext.bin', 'data.bin')


def pack(raw):
    """one load image: [MAGIC, packed bytes, raw bytes, 0], the stream, padded to a word"""
    data = lzma.compress(raw, format=lzma.FORMAT_RAW, filters=FILTERS)
    if lzma.decompress(data, format=lzma.FORMAT_RAW, filters=FILTERS) != raw:
        raise ValueError('boot image compression round trip failed')
    return struct.pack('<4I', MAGIC, len(data), len(raw), 0) + data + b'\0' * (-len(data) % 4)


def pack_images(blobs):
    """every section's load image, one after another (an empty section still has its record)"""
    return b''.join(pack(blob) for blob in blobs)


def unpack_images(image, count):
    """the packager's records read back (the tests): [(raw size, stream)]"""
    out, at = [], 0
    for _ in range(count):
        magic, n, size, reserved = struct.unpack_from('<4I', image, at)
        if magic != MAGIC or reserved:
            raise ValueError(f'no load image record at {at}')
        out.append((size, image[at + 16:at + 16 + n]))
        at += 16 + n + (-n % 4)
    return out


def check_decoder(syms, dis, xip_start, xip_end):
    addresses = {}
    for line in syms.splitlines():
        fields = line.split()
        if len(fields) >= 3 and re.fullmatch('[0-9a-f]+', fields[0]):
            addresses[fields[-1]] = int(fields[0], 16)
    functions = {}
    current = None
    for line in dis.splitlines():
        label = re.fullmatch(r'([\w.]+):', line)
        if label:
            current = label[1]
            functions[current] = []
        elif current:
            functions[current].append(line)
    errors, seen, pending = [], set(), ['boot_lzma']
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        if name not in functions or not xip_start <= addresses.get(name, 0) < xip_end:
            errors.append(f'boot decoder dependency {name} must execute from XIP')
            continue
        for line in functions[name]:
            call = re.search(r'\bcall\b', line)
            target = re.search(r'(?:\bcall|\bgoto) -?\d+ <([\w.]+)(?:\+[^ :]+)? :', line)
            if call and not target:
                errors.append(f'boot decoder {name} has an unresolved indirect call: {line.strip()}')
            if target and target[1] != name:
                pending.append(target[1])
    # Constant tables must also be available before executable SRAM is copied.
    for name, address in addresses.items():
        if name.startswith('boot_lzma.') and not xip_start <= address < xip_end:
            errors.append(f'boot decoder table {name} must be in XIP')
    return errors
