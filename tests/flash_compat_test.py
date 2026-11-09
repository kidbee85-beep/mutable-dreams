"""Check rescue rejects unknown flash before reading or writing the device."""
import importlib.util
from pathlib import Path

script = Path(__file__).resolve().parents[1] / "base/bsp/tools/fm1_rescue.py"
spec = importlib.util.spec_from_file_location("fm1_rescue", script)
rescue = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rescue)


class StopBeforeRead(Exception):
    pass


class Device:
    def __init__(self, fid):
        self.fid = fid
        self.reads = self.writes = 0

    def inquiry(self):
        return "WL82", "FM-1"

    def start_loader(self, _loader):
        pass

    def chip(self):
        return 0x980F, 3, self.fid

    def read(self, *_args):
        self.reads += 1
        raise StopBeforeRead

    def write(self, *_args):
        self.writes += 1
        raise AssertionError("write before verification")


for fid in (0x856014, 0xB36014):
    device = Device(fid)
    try:
        rescue.rescue(device, b"", b"", False, "/tmp", say=lambda *_: None)
    except StopBeforeRead:
        assert device.reads == 1 and device.writes == 0
    else:
        raise AssertionError(f"known flash {fid:06X} was not accepted")

for fid in (0, 0xFFFFFF, 0xEF4014, 0xB36015):
    device = Device(fid)
    try:
        rescue.rescue(device, b"", b"", False, "/tmp", say=lambda *_: None)
    except rescue.Fail:
        assert device.reads == 0 and device.writes == 0
    else:
        raise AssertionError(f"unknown flash {fid:06X} was accepted")

print("rescue flash gates: both FM-1 IDs accepted; unknown IDs refused before I/O")
