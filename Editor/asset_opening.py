"""Editor-local external application associations, independent of asset imports."""
from pathlib import Path
import sys

from .platform_services import ChooseApplication, LaunchApplication, ValidateApplication, OpenWithApplication


class ExternalAssetOpener:
    SettingsKey = "external_asset_applications"
    DefaultExtensions = frozenset({".cpp", ".lua"})

    def __init__(self, settings, save=None, extensions=None):
        self.Settings = settings
        self.Save = save
        self.Extensions = set(self.DefaultExtensions)
        if extensions is not None:
            self.Extensions = {self._Extension(value) for value in extensions}

    @staticmethod
    def _Extension(value):
        return "." + str(value).lower().lstrip(".")

    def RegisterExtension(self, extension):
        self.Extensions.add(self._Extension(extension))

    def Supports(self, path):
        return Path(path).suffix.lower() in self.Extensions

    def Associations(self):
        from copy import deepcopy
        value = self.Settings.get(self.SettingsKey, {})
        return deepcopy(value) if isinstance(value, dict) else {}

    def SetAssociation(self, extension, application):
        extension = self._Extension(extension)
        if extension not in self.Extensions:raise ValueError("Unsupported external asset format")
        application = str(ValidateApplication(application))
        self._Remember({**self.Associations(), extension: {"platform": sys.platform, "application": application}})

    def ResetAssociations(self, extension=None):
        values = self.Associations()
        if extension is None:values.clear()
        else:values.pop(self._Extension(extension), None)
        self._Remember(values)

    def Open(self, paths, parent, localization, choose=False):
        """Choose once per extension; cancellation never replaces an association."""
        paths = list(dict.fromkeys(Path(path).resolve() for path in paths))
        for path in paths:
            if not self.Supports(path):
                raise ValueError("Unsupported external asset format")
            if not path.is_file():
                raise FileNotFoundError(str(path))
        associations = self.Settings.get(self.SettingsKey, {})
        associations = dict(associations) if isinstance(associations, dict) else {}
        groups = {}
        for path in paths:
            groups.setdefault(path.suffix.lower(), []).append(path)
        for extension, files in groups.items():
            preferences = self.Settings.get("preferences", {})
            options = preferences.get("file_associations", {}) if isinstance(preferences, dict) else {}
            remember = options.get("remember", True) if isinstance(options, dict) else True
            saved = associations.get(extension)
            application = saved.get("application") if isinstance(saved, dict) and saved.get("platform") == sys.platform else None
            if not choose and remember and isinstance(application, str):
                try:
                    ValidateApplication(application)
                except (OSError, ValueError):
                    application = None
            else:
                application = None
            if not application:
                if sys.platform == "win32":
                    result = OpenWithApplication(files[0], parent)
                    if not result.Opened:continue
                    application = result.Application
                    if not application:
                        # A replacement was opened but not exposed by Windows.
                        # Do not reuse the old app on the next Open.
                        if remember and extension in associations:
                            associations.pop(extension)
                            self._Remember(associations)
                        for file in files[1:]:OpenWithApplication(file, parent)
                        continue
                    # SHOpenWithDialog already launched the first file.
                    if len(files) > 1:LaunchApplication(application, files[1:])
                else:
                    application = ChooseApplication(parent, localization, extension)
                    if not application:continue
                    LaunchApplication(application, files)
            else:
                LaunchApplication(application, files)
            # Launch before saving: a failed or invalid choice is never remembered.
            record = {"platform": sys.platform, "application": str(application)}
            if remember and associations.get(extension) != record:
                updated = {**associations, extension: record}
                self._Remember(updated)
                associations = updated

    def _Remember(self, updated):
        previous = self.Settings.get(self.SettingsKey)
        self.Settings[self.SettingsKey] = updated
        try:
            if self.Save is not None:self.Save(self.Settings)
        except (OSError, ValueError):
            if previous is None:self.Settings.pop(self.SettingsKey, None)
            else:self.Settings[self.SettingsKey] = previous
            raise
