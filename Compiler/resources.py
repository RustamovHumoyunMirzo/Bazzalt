"""Compiler resources shared by source and standalone builds."""
from pathlib import Path


def BrandingPNG():
    try:
        from ._resources import BRANDING_PNG
    except ImportError:
        return (Path(__file__).resolve().parents[1] / "bazzalt/assets/branding/BazzaltLauncher.png").read_bytes()
    return BRANDING_PNG
