{
  description = "Dusklight — native PC port of the Twilight Princess decompilation";

  inputs.nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  # Unstable no longer supports Intel macOS; 26.05 receives fixes through 2026.
  inputs.nixpkgs-darwin.url = "github:NixOS/nixpkgs/nixpkgs-26.05-darwin";
  inputs.self.submodules = true;

  outputs =
    { self, nixpkgs, nixpkgs-darwin }:
    let
      inherit (nixpkgs) lib;

      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = lib.genAttrs supportedSystems;

      dawnVersion = "v20260807.225922";
      nodVersion = "v2.0.0-alpha.12";
      versionSuffix = "nix-" + (self.shortRev or self.dirtyShortRev or "dirty");

      dawnInfo = {
        "x86_64-linux" = {
          triple = "linux-x86_64";
          hash = "sha256-deRtiZ221q6PO9zejJBwa56fCM63KEh6y2p7nM+MOYU=";
        };
        "aarch64-linux" = {
          triple = "linux-aarch64";
          hash = "sha256-WUs7dDxNbQtt5x8AIDmVuFWhcZVgSyUUuRJvr5yrREo=";
        };
        "aarch64-darwin" = {
          triple = "darwin-arm64";
          hash = "sha256-pM15OoUdHZ84Y9iORsvgahE6FzvQFOtjry0nNWvIqHo=";
        };
        "x86_64-darwin" = {
          triple = "darwin-x86_64";
          hash = "sha256-4qDs7eeEw89oEr37H5/vpjLHWaZf8216flaLmhyx5GY=";
        };
      };

      # Keep these releases in sync with AuroraDependencyVersions.cmake.
      nodPrebuiltInfo = {
        "aarch64-linux" = {
          triple = "linux-aarch64";
          hash = "sha256-UDmGNlaYjTmaGMJRuZMiZ4sNm6HBvsKVAiEF7bJJTVg=";
        };
        "x86_64-darwin" = {
          triple = "macos-x86_64";
          hash = "sha256-GXgGLvIMc+a5AeslvdqWMWlIBRH/mR0mTxOOA2urfQE=";
        };
        "x86_64-linux" = {
          triple = "linux-x86_64";
          hash = "sha256-UepkfyagQmv2DGt4BOkl8g4xX+TxGNd0IXV93Xbmca4=";
        };
        "aarch64-darwin" = {
          triple = "macos-arm64";
          hash = "sha256-drh+u90TCMNzHbFOzjFW44+uPH5WRE+flvW0nW5rdwM=";
        };
      };

      perSystem =
        system:
        let
          pkgs = import (if system == "x86_64-darwin" then nixpkgs-darwin else nixpkgs) { inherit system; };
          inherit (pkgs.stdenv.hostPlatform) isDarwin;
          # Borealis requires WSS in addition to HTTPS and HTTP/2. Nixpkgs
          # disables WebSockets by default, even in the full curl package.
          curl = pkgs.curl.override { websocketSupport = true; };

          dawn = pkgs.fetchzip {
            url = "https://github.com/encounter/dawn/releases/download/${dawnVersion}/dawn-${dawnInfo.${system}.triple}.tar.gz";
            hash = dawnInfo.${system}.hash;
            stripRoot = false;
          };
          nod = pkgs.fetchzip {
            url = "https://github.com/encounter/nod/releases/download/${nodVersion}/libnod-${nodPrebuiltInfo.${system}.triple}.tar.gz";
            hash = nodPrebuiltInfo.${system}.hash;
            stripRoot = false;
          };

          symgenInfo = {
            x86_64-linux = { asset = "linux-x86_64"; hash = "sha256-I2vFzUvaA6jdU6RTHv/x/sO80ZsJiQiTubb49a538EE="; };
            aarch64-linux = { asset = "linux-aarch64"; hash = "sha256-eJFEZQXE3UKPS4THzu0UQXGdkQs9WkfTVAqokFOESa4="; };
            x86_64-darwin = { asset = "macos-x86_64"; hash = "sha256-kyGPeG000+xY+WYk4ElP9gXmECzbMc18mSI2TPx4haU="; };
            aarch64-darwin = { asset = "macos-arm64"; hash = "sha256-a4NvT9flf8GYymv7PgFhUEyY8r4mIoGYSb/D8mXt9K8="; };
          };
          symgen = pkgs.stdenv.mkDerivation {
            pname = "symgen";
            version = "1.3.6";
            src = pkgs.fetchurl {
              url = "https://github.com/encounter/symgen/releases/download/v1.3.6/symgen-${symgenInfo.${system}.asset}";
              hash = symgenInfo.${system}.hash;
            };
            dontUnpack = true;
            nativeBuildInputs = lib.optionals (!isDarwin) [ pkgs.autoPatchelfHook ];
            buildInputs = lib.optionals (!isDarwin) [ pkgs.stdenv.cc.cc.lib ];
            installPhase = "install -Dm755 $src $out/bin/symgen";
          };

          # funchook's inner ExternalProject is not covered by FetchContent's
          # disconnected mode. Supply both disassemblers without a nested download.
          funchookSource = pkgs.runCommand "funchook-1.1.3-source" { } ''
            cp -R ${pkgs.fetchzip { url = "https://github.com/kubo/funchook/archive/refs/tags/v1.1.3.tar.gz"; hash = "sha256-zlHREpnDLHrx2CkAsR3BcFltFZzTOS8UKR8feJpK7bA="; }} $out
            chmod -R u+w $out
            mkdir -p $out/distorm
            cp -R ${pkgs.fetchzip { url = "https://github.com/gdabah/distorm/archive/ab59d6e193948cfa5d1482fb6c7e64870e9e93b9.tar.gz"; hash = "sha256-Fhvxag2UN5wXEySP1n1pCahMQR/SfssywikeLmiASwQ="; }}/. $out/distorm/
            cp -R ${pkgs.fetchzip { url = "https://github.com/aquynh/capstone/archive/refs/tags/4.0.2.tar.gz"; hash = "sha256-XMwQ7UaPC8YYu4yxsE4bbR3leYPfBHu5iixSLz05r3g="; }} $out/capstone
            chmod -R u+w $out/capstone
            sed -i '/cmake_policy(SET CMP0048 OLD)/d' $out/capstone/CMakeLists.txt
            ${pkgs.python3}/bin/python3 ${./nix/prepare-funchook.py} $out
          '';

          fetchContentDirs = {
            DAWN_PREBUILT = dawn;
            NOD_PREBUILT = nod;


            PICOSHA2 = pkgs.fetchzip {
              url = "https://github.com/okdshin/PicoSHA2/archive/refs/tags/v1.0.1.tar.gz";
              hash = "sha256-3psCzbrwR+vO9TyTKOx+gEaWuHDx6pSgLOQ3DqrJsnI=";
            };
            LUAU = pkgs.fetchzip {
              url = "https://github.com/luau-lang/luau/archive/refs/tags/0.734.tar.gz";
              hash = "sha256-s3UdKdMVc7PNKZpD+ZutC/+ux3UsGW7HSDjhbagTJgM=";
            };
            LUABRIDGE3 = pkgs.fetchzip {
              url = "https://github.com/TwilitRealm/LuaBridge3/archive/refs/tags/twilit-v1.zip";
              hash = "sha256-09sjxnCl1ZOJHTFUIBhwpR4zK6+uCScknjE7YDlUvJw=";
            };
            YAML-CPP = pkgs.fetchzip {
              url = "https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.tar.gz";
              hash = "sha256-+FOsPQY44h1g9tEw3O281LkiYKXdW2jnFKw+oTRkhGw=";
            };
            BASE64PP = pkgs.fetchzip {
              url = "https://github.com/matheusgomes28/base64pp/archive/refs/tags/v0.2.0-rc0.tar.gz";
              hash = "sha256-DYdnjbdZmQFOizg2SwAu35kWA0F72tE6ywe00azlqxk=";
            };
            BATTERY-EMBED = pkgs.fetchzip {
              url = "https://github.com/TwilitRealm/embed/archive/refs/tags/twilit-v2.tar.gz";
              hash = "sha256-xsEtUwYgCnYZoV5xVmVdWLgKPl/LztFGVKZZA1jG+uk=";
            };
            FUNCHOOK = funchookSource;
            GOOGLETEST = pkgs.gtest.src;

            MINIZ = pkgs.fetchzip {
              url = "https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip";
              hash = "sha256-DXysXkQEmoDAMMg1F8KexkwpXNyiHNzLJqXR9SMEkxk=";
              stripRoot = false;
            };

            FMT = pkgs.fetchzip {
              url = "https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz";
              hash = "sha256-ZmI1Dv0ZabPlxa02OpERI47jp7zFfjpeWCy1WyuPYZ0=";
            };
            TRACY = pkgs.fetchzip {
              url = "https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.zip";
              hash = "sha256-vcLI9jb7eYcR162LgBQ2P4A0oiZuYRfYRQiDhlAk5TI=";
            };
            IMGUI = pkgs.fetchFromGitHub {
              owner = "ocornut";
              repo = "imgui";
              rev = "v1.91.9b-docking";
              hash = "sha256-mQOJ6jCN+7VopgZ61yzaCnt4R1QLrW7+47xxMhFRHLQ=";
            };
            SQLITE3 = pkgs.fetchzip {
              url = "https://sqlite.org/2026/sqlite-amalgamation-3510300.zip";
              hash = "sha256-pNMR8zxaaqfAzQ0AQBOXMct4usdjey1Q0Gnitg06UhM=";
            };
            RMLUI = pkgs.fetchzip {
              url = "https://github.com/encounter/RmlUi/archive/f00a0fee38391b2f927114e11bea18dc0a7dba1e.tar.gz";
              hash = "sha256-pyTD/WXdmJmcCgqMjn11bYFHrEshZ4/SiJVOVGSs9M0=";
            };
          };

          dusklight =
            pkgs.stdenv.mkDerivation {
              pname = "dusklight";
              version = versionSuffix;
              __darwinAllowLocalNetworking = true;
              src = ./.;
              postPatch = ''
                echo 'add_subdirectory(nix)' >> CMakeLists.txt
              '';

                nativeBuildInputs = [
                  pkgs.cmake
                  pkgs.ninja
                  pkgs.pkg-config
                  pkgs.python3
                  pkgs.python3Packages.markupsafe
                ]
                ++ lib.optionals (!isDarwin) [ pkgs.autoPatchelfHook ]
                ++ lib.optionals isDarwin [ pkgs.makeWrapper ];

                buildInputs = [
                  pkgs.sdl3
                  pkgs.freetype
                  pkgs.zstd
                  (pkgs.cxxopts.override { enableUnicodeHelp = false; })
                  pkgs.nlohmann_json
                  pkgs.xxhash
                  pkgs.abseil-cpp
                  pkgs.zlib
                  pkgs.libpng
                  pkgs.libjpeg_turbo
                  curl
                  pkgs.openssl
                ]
                ++ lib.optionals isDarwin [
                  pkgs.apple-sdk_15
                  pkgs.libiconv
                ]
                ++ lib.optionals (!isDarwin) [
                  pkgs.libGL
                  pkgs.libGLU
                  pkgs.libglvnd
                  pkgs.vulkan-loader
                  pkgs.libX11
                  pkgs.libxcb
                  pkgs.libXcursor
                  pkgs.libxi
                  pkgs.libxrandr
                  pkgs.libxscrnsaver
                  pkgs.libxtst
                  pkgs.libxinerama
                  pkgs.libxkbcommon
                  pkgs.wayland
                  pkgs.libdecor
                  pkgs.alsa-lib
                  pkgs.libpulseaudio
                  pkgs.pipewire
                  pkgs.dbus
                  pkgs.udev
                  pkgs.libusb1
                  pkgs.libunwind
                  pkgs.gtk3
                ];

                cmakeBuildType = "RelWithDebInfo";
                ninjaFlags = [ "dusklight" "dusklight_mods" "nix_network_smoke" ] ++ testTargets;
                doCheck = true;
                checkPhase = ''
                  runHook preCheck
                  ctest --test-dir extern/aurora --output-on-failure -j "$NIX_BUILD_CORES"
                  ctest --test-dir extern/borealis --output-on-failure -j "$NIX_BUILD_CORES" -E '^WebSocketBackendTest\.'
                  python3 ../nix/test-websocket.py extern/borealis/tests/borealis_ws_backend_test
                  nix/nix_network_smoke
                  runHook postCheck
                '';
                preConfigure = ''
                  cmakeFlagsArray+=("-DCMAKE_INSTALL_PREFIX=$out/${if isDarwin then "Applications" else "bin"}")
                '';

                cmakeFlags = [
                  "-DBOREALIS_APP_VERSION_OVERRIDE=${versionSuffix}"
                  "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
                  "-DAURORA_DAWN_PROVIDER=package"
                  "-DAURORA_DAWN_LINKAGE=static"
                  "-DAURORA_NOD_PROVIDER=package"
                  "-DAURORA_NOD_LINKAGE=static"
                  "-DAURORA_SDL3_PROVIDER=system"
                  "-DBOREALIS_CURL_PROVIDER=system"
                  "-DDUSK_ENABLE_CODE_MODS=ON"
                  "-DAURORA_ENABLE_TESTS=ON"
                  "-DBOREALIS_BUILD_TESTS=ON"
                  "-DSYMGEN_PATH=${symgen}/bin/symgen"
                  "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
                  "-DBUILD_SHARED_LIBS=OFF"
                ]
                ++ lib.optionals (!isDarwin) [ "-DBOREALIS_HTTP_BACKEND=curl" ]
                ++ lib.mapAttrsToList (key: src: "-DFETCHCONTENT_SOURCE_DIR_${key}=${src}") fetchContentDirs;

                installPhase = ''
                  runHook preInstall
                  cmake --install .
                '' + lib.optionalString isDarwin ''
                  makeWrapper "$out/Applications/Dusklight.app/Contents/MacOS/Dusklight" "$out/bin/dusklight"
                '' + lib.optionalString (!isDarwin) ''
                  install -Dm644 "$src/platforms/freedesktop/dev.twilitrealm.dusk.desktop" \
                    "$out/share/applications/dev.twilitrealm.dusk.desktop"
                  for size in 16 32 48 64 128 256 512 1024; do
                    install -Dm644 "$src/platforms/freedesktop/''${size}x''${size}/apps/dev.twilitrealm.dusk.png" \
                      "$out/share/icons/hicolor/''${size}x''${size}/apps/dev.twilitrealm.dusk.png"
                  done
                '' + ''
                  runHook postInstall
                '';

                # Dawn opens Vulkan dynamically; retain it in the runtime closure.
                runtimeDependencies = lib.optionals (!isDarwin) [ (lib.getLib pkgs.vulkan-loader) ];
                postFixup = lib.optionalString isDarwin ''
                  # Nix's Mach-O fixups run after CMake's signing/prepatching.
                  find "$out/Applications/Dusklight.app/Contents/Resources/mods" -name '*.so' \
                    -exec /usr/bin/codesign --force --sign - {} \;
                  /usr/bin/codesign --force --sign - --entitlements \
                    "$src/platforms/macos/Dusklight.entitlements" "$out/Applications/Dusklight.app"
                '';

                dontStrip = true;

                meta = {
                  description = "Dusklight — native PC port of the Twilight Princess decompilation";
                  homepage = "https://github.com/TwilitRealm/dusklight";
                  platforms = supportedSystems;
                  mainProgram = "dusklight";
                };
              };

          testTargets = [
            "io_tests" "time_tests" "os_time_tests" "os_alloc_tests" "dvd_tests"
            "gx_fifo_tests" "gx_texture_cache_tests" "texture_replacement_streaming_tests"
            "render_worker_tests" "gfx_recording_tests"
          ] ++ map (name: "borealis_${name}_test") [
            "version" "task" "url" "presentation" "log" "io" "file_select" "data"
            "disc" "crash" "sentry" "discord" "http" "net" "ws" "ws_backend" "update" "cli"
          ];

          # Tooling common to every supported host (Linux and macOS).
          commonDevTools = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
            pkgs.git
            pkgs.python3
            pkgs.python3Packages.markupsafe
            pkgs.rustc
            pkgs.cargo
            pkgs.sccache
          ];

          # Linux-only system libraries — mirrors the apt deps from .github/workflows/build.yml
          # so the cmake presets resolve the same set of headers as CI.
          linuxDevDeps = [
            # Compilers / linkers
            pkgs.clang
            pkgs.lld
            # C/C++ utilities
            curl
            pkgs.openssl
            pkgs.zlib
            pkgs.libpng
            pkgs.libjpeg_turbo
            pkgs.freetype
            pkgs.zstd
            pkgs.fmt
            pkgs.tracy
            pkgs.cxxopts
            pkgs.abseil-cpp
            pkgs.sdl3
            pkgs.ncurses
            pkgs.libunwind
            pkgs.libusb1
            pkgs.fuse
            # Wayland / display server
            pkgs.wayland
            pkgs.wayland-protocols
            pkgs.libxkbcommon
            pkgs.libdecor
            # OpenGL / Vulkan
            pkgs.libGL
            pkgs.libGLU
            pkgs.libglvnd
            pkgs.vulkan-headers
            pkgs.vulkan-loader
            # X11
            pkgs.libX11
            pkgs.libxcb
            pkgs.libXcursor
            pkgs.libxi
            pkgs.libxrandr
            pkgs.libxscrnsaver
            pkgs.libxtst
            pkgs.libxinerama
            # Audio
            pkgs.alsa-lib
            pkgs.libpulseaudio
            pkgs.pipewire
            # System integration
            pkgs.dbus
            pkgs.udev
            pkgs.gtk3
          ];

          # On macOS we deliberately avoid pulling Nix's cc-wrapper so CMake picks up
          # Apple Clang and the Xcode SDK directly, matching the macOS CI workflow.
          darwinShell = pkgs.mkShellNoCC {
            packages = commonDevTools;
            shellHook = ''
              echo "Dusklight dev shell (macOS)"
              echo "Requires Xcode Command Line Tools for Apple Clang and the macOS SDK."
              echo "Configure: cmake --preset macos-default-relwithdebinfo"
              echo "Build:     cmake --build --preset macos-default-relwithdebinfo"
            '';
          };

          linuxShell = pkgs.mkShell {
            packages = commonDevTools ++ linuxDevDeps;
            shellHook = ''
              echo "Dusklight dev shell (Linux)"
              echo "Configure: cmake --preset linux-default-relwithdebinfo"
              echo "           cmake --preset linux-clang-relwithdebinfo"
              echo "Build:     cmake --build --preset <preset>"
            '';
          };
        in
        {
          packages = {
            default = dusklight;
            dusklight = dusklight;
          };
          checks.default = dusklight;
          checks.package = pkgs.runCommand "dusklight-package-check" {
            nativeBuildInputs = [ pkgs.python3 ] ++ lib.optionals (!isDarwin) [ pkgs.binutils pkgs.glibc.bin ];
          } ''
            ${pkgs.python3}/bin/python3 ${./nix}/check-package.py ${dusklight}
            touch $out
          '';

          devShells.default = if isDarwin then darwinShell else linuxShell;
        };

      systems = forAllSystems perSystem;
    in
    {
      packages = lib.mapAttrs (_: s: s.packages) systems;
      checks = lib.mapAttrs (_: s: s.checks) systems;
      devShells = lib.mapAttrs (_: s: s.devShells) systems;
    };
}
