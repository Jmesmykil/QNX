"""
NxTheme: read/write .nxtheme files.
Format: Yaz0-compressed SARC containing:
  info.json    REQUIRED
  layout.json  optional
  image.jpg    optional (1280x720 JPEG background)
  image.dds    optional (DXT1/ASTC DDS background)
  common.json  optional
"""
import os
import json
from typing import Dict, Optional

from .yaz0 import decompress as _yaz0_dec, compress as _yaz0_enc
from .sarc import unpack as _sarc_unpack, pack as _sarc_pack


class NxTheme:
    """Immutable-style container: set_file returns a new NxTheme instance."""

    def __init__(self, files: Optional[Dict[str, bytes]] = None):
        self._files: Dict[str, bytes] = {} if files is None else dict(files)

    # -----------------------------------------------------------------------
    # Factory / serialisation
    # -----------------------------------------------------------------------

    @classmethod
    def from_bytes(cls, data: bytes) -> "NxTheme":
        if data[:4] == b"Yaz0":
            sarc_bytes = _yaz0_dec(data)
        elif data[:4] == b"SARC":
            sarc_bytes = data
        else:
            raise ValueError(f"Unknown nxtheme magic: {data[:4]!r}")
        return cls(_sarc_unpack(sarc_bytes))

    @classmethod
    def from_file(cls, path: str) -> "NxTheme":
        with open(path, "rb") as fh:
            return cls.from_bytes(fh.read())

    def to_bytes(self, compress: bool = True) -> bytes:
        raw_sarc = _sarc_pack(self._files)
        return _yaz0_enc(raw_sarc) if compress else raw_sarc

    def to_file(self, path: str, compress: bool = True) -> None:
        parent = os.path.dirname(os.path.abspath(path))
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(self.to_bytes(compress))

    # -----------------------------------------------------------------------
    # Accessors (immutable style)
    # -----------------------------------------------------------------------

    @property
    def files(self) -> Dict[str, bytes]:
        return dict(self._files)

    def list_files(self):
        return sorted(self._files.keys())

    def get_file(self, name: str) -> Optional[bytes]:
        return self._files.get(name)

    def set_file(self, name: str, data: bytes) -> "NxTheme":
        new_files = dict(self._files)
        new_files[name] = data
        return NxTheme(new_files)

    def remove_file(self, name: str) -> "NxTheme":
        new_files = dict(self._files)
        new_files.pop(name, None)
        return NxTheme(new_files)

    # -----------------------------------------------------------------------
    # Validation
    # -----------------------------------------------------------------------

    def validate(self) -> None:
        if "info.json" not in self._files:
            raise ValueError("nxtheme: info.json is required but missing")
        try:
            manifest = json.loads(self._files["info.json"].decode("utf-8"))
        except Exception as exc:
            raise ValueError(f"nxtheme: info.json is not valid JSON: {exc}") from exc
        if "ThemeName" not in manifest:
            raise ValueError("nxtheme: info.json missing 'ThemeName'")
        has_content = any(
            k in self._files
            for k in ("layout.json", "image.jpg", "image.dds", "common.json")
        )
        if not has_content:
            raise ValueError(
                "nxtheme: must contain at least one of "
                "layout.json, image.jpg, image.dds, or common.json"
            )

    def info(self) -> dict:
        raw = self._files.get("info.json", b"{}")
        return json.loads(raw.decode("utf-8"))
