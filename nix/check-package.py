"""Check the installed package without a display, game disc, or user data."""
import ctypes
import re
import json
import pathlib
import subprocess
import sys
import tempfile
import runpy

root = pathlib.Path(sys.argv[1])
darwin = sys.platform == "darwin"
app = root / "Applications/Dusklight.app"
executable = app / "Contents/MacOS/Dusklight" if darwin else root / "bin/dusklight"
assets = app / "Contents/Resources" if darwin else root / "bin"
assert executable.is_file(), executable
assert (assets / "res").is_dir(), assets
for mod_id in ("dev.twilitrealm.luau", "dev.twilitrealm.cosmetics", "dev.twilitrealm.randomizer"):
    mod = assets / "mods" / mod_id
    assert json.loads((mod / "mod.json").read_text())["id"] == mod_id
    libraries = list((mod / "lib").glob("*/mod.so"))
    assert len(libraries) == 1, (mod_id, libraries)
    if darwin:
        subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(libraries[0])], check=True)
    else:
        output = subprocess.check_output(["ldd", str(libraries[0])], text=True)
        assert "not found" not in output, output
runpy.run_path(str(pathlib.Path(__file__).with_name("check-manifest.py")))["check_manifest"](executable)
# The manifest is loaded by name-based code hooks; losing it silently breaks mods.
if darwin:
    sections = subprocess.check_output(["/usr/bin/otool", "-l", str(executable)], text=True)
    assert "__symdb" in sections, "Missing embedded symbol manifest"
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)], check=True)
else:
    sections = subprocess.check_output(["readelf", "-S", "--wide", str(executable)], text=True)
    assert " symdb " in sections, "Missing embedded symbol manifest"
    output = subprocess.check_output(["ldd", str(executable)], text=True)
    assert "not found" not in output, output
    dynamic = subprocess.check_output(["readelf", "-d", str(executable)], text=True)
    rpaths = re.findall(r"(?:RUNPATH|RPATH).*\[(.*?)\]", dynamic)
    loaders = [pathlib.Path(directory) / "libvulkan.so.1" for rpath in rpaths
               for directory in rpath.split(":") if "vulkan-loader" in directory]
    assert loaders, "Vulkan loader missing from runtime search path"
    ctypes.CDLL(str(loaders[0]))
    assert (root / "share/applications/dev.twilitrealm.dusk.desktop").is_file()
    assert (root / "share/icons/hicolor/256x256/apps/dev.twilitrealm.dusk.png").is_file()
# --help exercises the final, fixed-up executable, including CLI ABI integration.
with tempfile.TemporaryDirectory() as cwd:
    result = subprocess.run([str(root / "bin/dusklight"), "--help"], cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30, check=True)
    assert "--mods" in result.stdout and "--backend" in result.stdout, result.stdout
    print(result.stdout)
print("Installed resources, bundled native mods, symbol manifest, dynamic dependencies and CLI verified")
