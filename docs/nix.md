# Nix

The flake provides full Dusklight packages and development shells for
`x86_64-linux`, `aarch64-linux`, `aarch64-darwin`, and `x86_64-darwin`.
Intel macOS uses a separately locked Nixpkgs 26.05 input because unstable has
removed support for that platform. Update both inputs when maintaining the lock.

Build directly from Git, including submodules:

```sh
nix build 'git+https://github.com/TwilitRealm/dusklight?submodules=1'
```

For a local checkout:

```sh
git submodule update --init --recursive
nix build "git+file://$PWD?submodules=1"
nix flake check "git+file://$PWD?submodules=1"
```

Linux installs the executable at `result/bin/dusklight`, with built-in resources
and mods next to it, plus desktop and icon files under `result/share`. macOS
installs `result/Applications/Dusklight.app` and a `result/bin/dusklight` launcher
for `nix run`. User data stays in the normal
writable preference directory or the directory selected with `--user-dir`.
The package does not include a game disc or extracted game data; import a legally
obtained disc through the application. On non-NixOS Linux, host graphics drivers
may need a suitable [NixGL environment](https://github.com/nix-community/nixGL).

Code mods, the embedded symbol manifest, Luau, cosmetics, and the randomizer are
enabled. Linux curl explicitly includes HTTPS, HTTP/2, and WebSockets. macOS
uses the native URLSession backend. Dependencies are fixed-output downloads;
CMake runs with `FETCHCONTENT_FULLY_DISCONNECTED=ON`. Funchook's nested Capstone
download is replaced with its pinned local source. Keep Dawn, nod, RmlUi, Tracy,
Luau, and other vendored versions aligned with their CMake declarations when
updating the Aurora or Borealis submodules.

The Nix workflow builds every advertised platform. Each package build runs the
Aurora and Borealis tests, including six real WebSocket backend scenarios against
a loopback server. Package checks verify installed resources, bundled native
libraries, the symbol manifest and its runtime mapping, native hook installation
and removal, dynamic dependencies, the CLI, and macOS code
signatures after fixups. A separate Actions step exercises HTTPS through the same
packaged Borealis backend. No game assets or secrets are uploaded. These checks
do not cover gameplay, GPU rendering, disc import, interactive mod browsing, or
loading a third-party code mod in a running game.
