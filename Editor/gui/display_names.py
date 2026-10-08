"""Presentation labels never change component/property identifiers."""
import re

def Humanize(value: str) -> str:
    value=re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", " ",str(value))
    return value.replace("_"," ").replace("Gui ","GUI ")

def DisplayName(localization, value: str, category: str = "component") -> str:
    key=f"{category}.{value}";translated=localization.Translate(key) if localization else key
    return Humanize(value) if translated==key else translated
