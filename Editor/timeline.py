"""Versioned timeline asset validation and atomic authoring writes."""

import copy
import json
import math
import os
from pathlib import Path
import tempfile
import uuid
import yaml

ZERO = "00000000-0000-0000-0000-000000000000"


def NewSequence():
    return dict(
        version=1,
        name="Timeline",
        duration=10.0,
        frameRate=30,
        tracks=[],
        cameras=[],
        signals=[],
    )


def Number(value, minimum=0, maximum=86400):
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not minimum <= value <= maximum
        or not math.isfinite(value)
    ):
        raise ValueError("Timeline number is outside its range")
    return value


def Identity(value, root=False):
    try:
        identity = uuid.UUID(value)
    except (TypeError, ValueError, AttributeError):
        raise ValueError("Invalid timeline UUID")
    if not root and identity.int == 0:
        raise ValueError("Timeline binding must identify an entity")
    return str(identity)


def Name(value):
    if (
        not isinstance(value, str)
        or len(value.encode("utf-8")) > 256
        or any(ord(c) < 32 for c in value)
    ):
        raise ValueError("Invalid timeline name")


def Vector(value, count):
    if not isinstance(value, list) or len(value) != count:
        raise ValueError("Invalid timeline vector")
    for number in value:
        Number(number, -3.4e38, 3.4e38)


def Record(value):
    if not isinstance(value, dict):
        raise ValueError("Timeline records must be mappings")
    return value


def Validate(document):
    if (
        not isinstance(document, dict)
        or isinstance(document.get("version"), bool)
        or document.get("version") != 1
    ):
        raise ValueError("Unsupported timeline document")
    Name(document.get("name", "Timeline"))
    duration = Number(document.get("duration"), 0.000001)
    fps = Number(document.get("frameRate"), 1, 240)
    if int(fps) != fps:
        raise ValueError("Frame rate must be an integer")
    for key, limit in (("tracks", 1024), ("cameras", 4096), ("signals", 65536)):
        if not isinstance(document.get(key), list) or len(document[key]) > limit:
            raise ValueError("Timeline list exceeds its budget")
    targets = set()
    count = 0
    for track in document["tracks"]:
        Record(track)
        Name(track.get("name", ""))
        target = Identity(track.get("target"))
        if target in targets:
            raise ValueError("A transform can have only one timeline track")
        targets.add(target)
        if not isinstance(track.get("worldSpace", False), bool) or not isinstance(
            track.get("muted", False), bool
        ):
            raise ValueError("Invalid track state")
        if not isinstance(track.get("keys"), list) or not track["keys"]:
            raise ValueError("Transform tracks need keys")
        previous = -1
        for key in track["keys"]:
            Record(key)
            time = Number(key.get("time"), 0, duration)
            if time <= previous:
                raise ValueError("Keys must be sorted with unique times")
            previous = time
            count += 1
            for name, size in (("position", 3), ("rotation", 4), ("scale", 3)):
                Vector(key.get(name), size)
            if sum(v * v for v in key["rotation"]) < 1e-10 or any(
                abs(v) < 1e-6 for v in key["scale"]
            ):
                raise ValueError("Invalid key rotation or scale")
            mode = key.get("interpolation", 1)
            if isinstance(mode, bool) or mode not in (0, 1, 2):
                raise ValueError("Invalid timeline interpolation")
    if count > 100000:
        raise ValueError("Timeline key budget exceeded")
    previous = 0
    for shot in document["cameras"]:
        Record(shot)
        Name(shot.get("name", ""))
        Identity(shot.get("camera"))
        start = Number(shot.get("start"), 0, duration)
        end = Number(shot.get("end"), 0, duration)
        if start < previous or end <= start:
            raise ValueError("Camera shots must be ordered and nonoverlapping")
        Number(shot.get("blendIn", 0), 0, end - start)
        previous = end
    previous = -1
    for signal in document["signals"]:
        Record(signal)
        Name(signal.get("name", ""))
        Identity(signal.get("event"), True)
        time = Number(signal.get("time"), 0, duration)
        if time < previous:
            raise ValueError("Signal times must be sorted")
        previous = time
        args = signal.get("arguments", [])
        if not isinstance(args, list) or len(args) > 64:
            raise ValueError("Too many signal arguments")
        for arg in args:
            Record(arg)
            kind, value = arg.get("type"), arg.get("value")
            if kind == "bool":
                if not isinstance(value, bool):
                    raise ValueError("Invalid signal boolean")
            elif kind == "int":
                Number(value, -(2**63), 2**63 - 1)
                if not isinstance(value, int):
                    raise ValueError("Invalid signal integer")
            elif kind == "number":
                Number(value, -1.7976931348623157e308, 1.7976931348623157e308)
            elif kind == "string":
                Name(value)
            elif kind == "uuid":
                Identity(value, True)
            elif kind in ("vec2", "vec3", "vec4", "quaternion"):
                Vector(value, dict(vec2=2, vec3=3, vec4=4, quaternion=4)[kind])
            else:
                raise ValueError("Unknown signal argument type")
    return document


def Load(path):
    path = Path(path)
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("Timeline exceeds 16 MiB")
    try:
        document = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (yaml.YAMLError, RecursionError) as error:
        raise ValueError("Invalid timeline YAML") from error
    return Validate(document)


def Save(path, document):
    Validate(document)
    path = Path(path)
    text = yaml.safe_dump(document, sort_keys=False, allow_unicode=True)
    if len(text.encode("utf-8")) > 16 * 1024 * 1024:
        raise ValueError("Timeline exceeds 16 MiB")
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return False
    descriptor, temporary = tempfile.mkstemp(
        prefix="." + path.name, suffix=".pending", dir=path.parent
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as file:
            file.write(text)
            file.flush()
            os.fsync(file.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return True


def Source(document):
    Validate(document)
    return json.dumps(
        document, ensure_ascii=False, separators=(",", ":"), allow_nan=False
    )


def Snap(time, document):
    return max(
        0,
        min(
            document["duration"],
            round(time * document["frameRate"]) / document["frameRate"],
        ),
    )
