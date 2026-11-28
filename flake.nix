{
  description = "SilexNib — Prebuilt binary distribution flake for NixOS, nix-darwin, and Home Manager (closed-source freeware; open ILP1/.snb spec)";

  # Pin nixpkgs to match the official release build ABI (glibc / libstdc++).
  # Avoid overriding `inputs.nixpkgs.follows` unless your host nixpkgs has a compatible glibc/libstdc++.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/151fa4e8ddfdd8dd25d945ad94ed54a13de9f6e4";

  outputs = { self, nixpkgs }:
    let
      release = builtins.fromJSON (builtins.readFile ./release.json);

      mkLinuxPkg = pkgs:
        pkgs.stdenvNoCC.mkDerivation rec {
          pname = "silex";
          version = release.version;

          src = pkgs.fetchurl {
            url = release.linux_x86_64.url;
            hash = release.linux_x86_64.hash;
          };

          nativeBuildInputs = [
            pkgs.autoPatchelfHook
            pkgs.makeWrapper
          ];

          buildInputs = [
            pkgs.stdenv.cc.cc.lib
            pkgs.libGL
            pkgs.libdrm
            pkgs.libgbm
            pkgs.vulkan-loader
            pkgs.wayland
            pkgs.libx11
            pkgs.libxcb
            pkgs.libxkbcommon
            pkgs.fontconfig
            pkgs.expat
            pkgs.dbus
            pkgs.systemdLibs
            pkgs.zlib
          ];

          appendRunpaths = [
            "${placeholder "out"}/lib/silex/lib"
          ];

          dontConfigure = true;
          dontBuild = true;

          installPhase = ''
            runHook preInstall
            mkdir -p "$out/bin"
            cp -a lib share "$out/"
            makeWrapper "$out/lib/silex/silex" "$out/bin/silex" \
              --set QT_PLUGIN_PATH "$out/lib/silex/plugins" \
              --prefix XDG_DATA_DIRS : "$out/share"
            makeWrapper "$out/lib/silex/silex-syncd" "$out/bin/silex-syncd" \
              --set QT_PLUGIN_PATH "$out/lib/silex/plugins"
            runHook postInstall
          '';

          meta = {
            description = "Pure-native Markdown knowledge base, daily notes, PDF viewer & physical handwriting notebook";
            homepage = "https://keithxc.github.io/silexnib/";
            license = pkgs.lib.licenses.unfreeRedistributable;
            platforms = [ "x86_64-linux" ];
            mainProgram = "silex";
          };
        };

      mkDarwinPkg = pkgs:
        pkgs.stdenvNoCC.mkDerivation rec {
          pname = "silex";
          version = release.version;

          src = pkgs.fetchurl {
            url = release.macos_arm64.url;
            hash = release.macos_arm64.hash;
          };

          nativeBuildInputs = [ pkgs.unzip ];
          sourceRoot = ".";
          unpackPhase = ''
            unzip -q "$src"
          '';

          # Preserve Apple Developer ID code signature & notarization ticket intact.
          dontConfigure = true;
          dontBuild = true;
          dontFixup = true;

          installPhase = ''
            runHook preInstall
            mkdir -p "$out/Applications" "$out/bin"
            cp -R Silex.app "$out/Applications/Silex.app"
            ln -s Applications/Silex.app "$out/silex.app"
            printf '#!/bin/sh\nexec "%s/Applications/Silex.app/Contents/MacOS/silex" "$@"\n' "$out" > "$out/bin/silex"
            chmod 755 "$out/bin/silex"
            if [ -x "$out/Applications/Silex.app/Contents/MacOS/silex-syncd" ]; then
              printf '#!/bin/sh\nexec "%s/Applications/Silex.app/Contents/MacOS/silex-syncd" "$@"\n' "$out" > "$out/bin/silex-syncd"
              chmod 755 "$out/bin/silex-syncd"
            fi
            runHook postInstall
          '';

          meta = {
            description = "Pure-native Markdown knowledge base, daily notes, PDF viewer & physical handwriting notebook";
            homepage = "https://keithxc.github.io/silexnib/";
            license = pkgs.lib.licenses.unfreeRedistributable;
            platforms = [ "aarch64-darwin" ];
            mainProgram = "silex";
          };
        };
    in {
      packages = {
        x86_64-linux =
          let
            pkgs = import nixpkgs {
              system = "x86_64-linux";
              config.allowUnfree = true;
            };
            pkg = mkLinuxPkg pkgs;
          in {
            default = pkg;
            silex = pkg;
          };

        aarch64-darwin =
          let
            pkgs = import nixpkgs {
              system = "aarch64-darwin";
              config.allowUnfree = true;
            };
            pkg = mkDarwinPkg pkgs;
          in {
            default = pkg;
            silex = pkg;
          };
      };

      nixosModules = rec {
        silex = import ./nix/nixos-module.nix { inherit self; };
        default = silex;
      };

      darwinModules = rec {
        silex = import ./nix/darwin-module.nix { inherit self; };
        default = silex;
      };

      homeManagerModules = rec {
        silex = import ./nix/hm-module.nix { inherit self; };
        default = silex;
      };
    };
}
