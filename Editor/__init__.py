"""BAZZALT editor package."""

__all__ = ["Editor", "LocalizationManager", "ResourceManager"]


def __getattr__(name):
    # The content compiler shares non-UI helpers in this package. Importing those
    # helpers must not initialize Qt or require an editor installation.
    from importlib import import_module
    modules = {
        "Editor": ".gui.application",
        "LocalizationManager": ".localization",
        "ResourceManager": ".resources",
    }
    if name not in modules:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    value = getattr(import_module(modules[name], __name__), name)
    globals()[name] = value
    return value
