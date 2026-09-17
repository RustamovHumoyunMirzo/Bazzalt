"""
Theming for the node editor.

Colors are deliberately muted / "casual-modern" rather than neon or high
contrast: desaturated blue-greys for the canvas, soft accent colors for
sockets and node headers. Everything is overridable - construct a Theme
and pass it to NodeWorkspace(theme=...), or mutate workspace.theme and
call workspace.apply_theme().
"""

from dataclasses import dataclass, field, replace


@dataclass
class Theme:
    name: str = "custom"

    # Canvas
    background: str = "#232629"
    grid_fine: str = "#2b2f33"
    grid_coarse: str = "#33383d"
    grid_fine_spacing: int = 24
    grid_coarse_spacing: int = 120

    # Node body
    node_bg: str = "#31353a"
    node_bg_selected: str = "#3a3f46"
    node_border: str = "#44494f"
    node_border_selected: str = "#8fb3d9"
    node_header_text: str = "#eef0f2"
    node_body_text: str = "#c7ccd1"
    node_corner_radius: float = 8.0

    # Default header color (per-node-type color overrides this)
    node_header_default: str = "#54595f"

    # Sockets
    socket_border: str = "#1c1e21"
    socket_label_text: str = "#a9afb6"

    # Connections
    connection_default: str = "#9aa1a8"
    connection_selected: str = "#dfe3e6"
    connection_dragging: str = "#c9ced3"
    connection_width: float = 2.4

    # Selection / interaction
    selection_rubber_band: str = "#5b8fb955"
    selection_rubber_band_border: str = "#8fb3d9"

    # Chrome
    text_muted: str = "#8b9096"
    panel_bg: str = "#2a2d31"
    panel_border: str = "#3a3e43"
    accent: str = "#7ea6c9"

    def variant(self, **overrides) -> "Theme":
        """Return a copy of this theme with the given fields overridden."""
        return replace(self, **overrides)


DARK_THEME = Theme(
    name="dark",
    background="#232629",
    grid_fine="#2b2f33",
    grid_coarse="#33383d",
    node_bg="#31353a",
    node_bg_selected="#3a3f46",
    node_border="#44494f",
    node_border_selected="#8fb3d9",
    node_header_text="#eef0f2",
    node_body_text="#c7ccd1",
    node_header_default="#54595f",
    socket_border="#1c1e21",
    socket_label_text="#a9afb6",
    connection_default="#9aa1a8",
    connection_selected="#dfe3e6",
    connection_dragging="#c9ced3",
    text_muted="#8b9096",
    panel_bg="#2a2d31",
    panel_border="#3a3e43",
    accent="#7ea6c9",
)

LIGHT_THEME = Theme(
    name="light",
    background="#eef0f2",
    grid_fine="#e2e5e8",
    grid_coarse="#d5d9dc",
    node_bg="#fbfbfc",
    node_bg_selected="#ffffff",
    node_border="#c9ced3",
    node_border_selected="#5b8fb9",
    node_header_text="#26292c",
    node_body_text="#3c4045",
    node_header_default="#c3c9ce",
    socket_border="#8b9096",
    socket_label_text="#5c6166",
    connection_default="#8a9099",
    connection_selected="#3c6e94",
    connection_dragging="#5c6166",
    text_muted="#70757a",
    panel_bg="#e4e7e9",
    panel_border="#ccd0d4",
    accent="#3c6e94",
)

BUILTIN_THEMES = {"dark": DARK_THEME, "light": LIGHT_THEME}


def resolve_theme(theme) -> Theme:
    if isinstance(theme, Theme):
        return theme
    if isinstance(theme, str):
        try:
            return BUILTIN_THEMES[theme]
        except KeyError:
            raise ValueError(
                f"Unknown built-in theme '{theme}'. Use one of "
                f"{list(BUILTIN_THEMES)} or pass a Theme instance."
            )
    raise TypeError("theme must be a Theme instance or one of 'dark'/'light'")
