# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
    zig.url = "github:mitchellh/zig-overlay";
  };

  outputs = { self, nixpkgs, flake-utils, zig }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        zigpkgs = zig.packages.${system};

        macosx-sdks = pkgs.stdenv.mkDerivation {
          pname = "macosx-sdks";
          version = "12.0";

          src = builtins.fetchurl {
            url = "https://github.com/joseluisq/macosx-sdks/releases/download/12.0/MacOSX12.0.sdk.tar.xz";
            sha256 = "sha256:1z8ga5m9624g3hc0kpdfpqbq1ghswxg57w813jdb18z6166g41xc";
          };

          buildPhase = ''
            mkdir -p $out
          '';

          installPhase = ''
            mkdir -p $out
            cp -R . $out
          '';
        };

        clang-build-analyzer = pkgs.stdenv.mkDerivation rec {
          pname = "clang-build-analyzer";
          version = "1.5.0";

          src = pkgs.fetchFromGitHub {
            owner = "aras-p";
            repo = "ClangBuildAnalyzer";
            rev = "v${version}";
            hash = "sha256-kmgdk634zM0W0OoRoP/RzepArSipa5bNqdVgdZO9gxo=";
          };

          nativeBuildInputs = [
            pkgs.cmake
          ];
        };

        # created using nix-init
        clap-val = pkgs.rustPlatform.buildRustPackage rec {
          pname = "clap-validator";
          version = "0.3.2";
          buildType = "debug";
          dontStrip = true;
          src = pkgs.fetchFromGitHub {
            owner = "free-audio";
            repo = "clap-validator";
            rev = version;
            hash = "sha256-Dtn6bVBFvUBLam7ZagflnNsCuQBCeTGfSVH/7G/QiBE=";
          };
          cargoLock = {
            lockFile = ./build_resources/clap-val-Cargo.lock;
            outputHashes = {
              "clap-sys-0.3.0" = "sha256-O+x9PCRxU/e4nvHa4Lu/MMWTzqCt6fLRZUe4/5HlvJM=";
            };
          };
        };

        # created using nix-init
        pluginval = pkgs.stdenv.mkDerivation rec {
          pname = "pluginval";
          version = "1.0.3";

          src = pkgs.fetchFromGitHub {
            owner = "Tracktion";
            repo = "pluginval";
            rev = "v${version}";
            hash = "sha256-o253DBl3jHumaKzxHDZXK/MpFMrq06MmBia9HEYLtXs=";
            fetchSubmodules = true;
          };

          cmakeBuildType = "Debug";
          dontStrip = true;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.pkg-config
          ];

          buildInputs = pkgs.lib.optionals pkgs.stdenv.isLinux [
            pkgs.xorg.libX11
            pkgs.xorg.libXrandr
            pkgs.xorg.libXcursor
            pkgs.xorg.libXinerama
            pkgs.xorg.libXext
            pkgs.freetype
            pkgs.alsa-lib
            pkgs.curl
            pkgs.gtk3-x11
            pkgs.ladspa-sdk
          ] ++ pkgs.lib.optionals pkgs.stdenv.isDarwin [
            pkgs.darwin.apple_sdk.frameworks.CoreAudio
            pkgs.darwin.apple_sdk.frameworks.CoreMIDI
            pkgs.darwin.apple_sdk.frameworks.CoreFoundation
            pkgs.darwin.apple_sdk.frameworks.Cocoa
            pkgs.darwin.apple_sdk.frameworks.WebKit
            pkgs.darwin.apple_sdk.frameworks.MetalKit
            pkgs.darwin.apple_sdk.frameworks.Accelerate
            pkgs.darwin.apple_sdk.frameworks.CoreAudioKit
            pkgs.darwin.apple_sdk.frameworks.DiscRecording
          ];

          installPhase = ''
            mkdir -p $out/bin
          '' + pkgs.lib.optionalString pkgs.stdenv.isLinux ''
            cp $(find . -name "pluginval" -executable) $out/bin
          '' + pkgs.lib.optionalString pkgs.stdenv.isDarwin ''
            mkdir -p $out/Applications
            cp -R $(find . -type d -name "pluginval.app") $out/Applications
            ln -s $out/Applications/pluginval.app/Contents/MacOS/pluginval $out/bin
          '';
        };
      in
      {
        devShells.default = pkgs.mkShell {
          packages = [
            # If you change the zig version you probably also want to change the ZLS version. 
            # For me, that's done my home-manager setup at the moment.
            zigpkgs."0.13.0"
            clap-val
            pluginval
            clang-build-analyzer
            pkgs.zip
            pkgs.unzip
            pkgs.llvmPackages_18.bintools-unwrapped # llvm-lipo, llvm-addr2line, dsymutil
            pkgs.llvmPackages_18.clang-unwrapped # clangd, clang-tidy, clang-format
            pkgs.cppcheck
            pkgs.codespell
            pkgs.parallel
            pkgs.miller
            pkgs.gnused
            pkgs.coreutils
            pkgs.jq
            pkgs.just
            pkgs.reuse
            pkgs.mdbook
            pkgs.osslsigncode
            pkgs.wget
            pkgs.hunspell
            pkgs.hunspellDicts.en_GB-ise
            pkgs.lychee # link checker

            # dsymutil internally calls "lipo", so we have to make sure it's available under that name
            (pkgs.writeShellScriptBin "lipo" "llvm-lipo $@")
          ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            pkgs.pkg-config
            pkgs.alsa-lib
            pkgs.xorg.libX11
            pkgs.xorg.libXext
            pkgs.xorg.libXcursor
            pkgs.gnome.zenity
            pkgs.libGL
            pkgs.libGLU
            pkgs.kcov
            pkgs.patchelf
            pkgs.glibc

            (pkgs.writeShellScriptBin "patchrpath" ''
              patchelf --add-rpath "${pkgs.libGL}/lib" $@
            '')
          ];
          shellHook = ''
            export MACOSX_SDK_SYSROOT="${macosx-sdks}"
          '';
        };
      });
}
