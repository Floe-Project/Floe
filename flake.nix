# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-23.11";
    flake-utils.url = "github:numtide/flake-utils";
    zig.url = "github:mitchellh/zig-overlay";
  };

  outputs = { self, nixpkgs, flake-utils, zig }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        zig-out-folder = "zig-out/${builtins.replaceStrings [ "_64" "darwin" ] [ "" "macos" ] system}";
        pkgs = import nixpkgs { inherit system; };
        zigpkgs = zig.packages.${system};

        macosx-sdks = pkgs.stdenv.mkDerivation {
          pname = "macosx-sdks";
          version = "11.3";

          src = builtins.fetchurl {
            url = "https://github.com/joseluisq/macosx-sdks/releases/download/11.3/MacOSX11.3.sdk.tar.xz";
            sha256 = "sha256:1c9crsg9ha4196ic4gjacwqjkc2ij1yg3ncas9rik7l7sdri7p4s";
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
            # cp $(find . -name "pluginval" -executable) $out/bin
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
            zigpkgs.master-2024-04-25
            clap-val
            pluginval
            clang-build-analyzer
            pkgs.zip
            pkgs.kcov
            pkgs.llvmPackages_17.bintools-unwrapped # llvm-lipo, llvm-addr2line, dsymutil
            pkgs.llvmPackages_17.clang-unwrapped # clangd, clang-tidy, clang-format
            pkgs.cppcheck

            (pkgs.writeShellScriptBin "format-all" ''
              ${pkgs.fd}/bin/fd . -e .mm -e .cpp -e .hpp -e .h src | xargs ${pkgs.llvmPackages_17.clang-unwrapped}/bin/clang-format -i
            '')

            (pkgs.writeShellScriptBin "check-format-all" ''
              ${pkgs.fd}/bin/fd . -e .mm -e .cpp -e .hpp -e .h src | xargs ${pkgs.llvmPackages_17.clang-unwrapped}/bin/clang-format clang-format --dry-run --Werror
            '')

            (pkgs.writeShellScriptBin "clang-tidy-all" ''
              echo "NOTE: Our clang-tidy command is generated after a successful compilation using our Zig build system"
              sh zig-cache/clang-tidy-cmd.sh
            '')

            # --enable=constVariable ?
            (pkgs.writeShellScriptBin "cppcheck-all" ''
              echo "NOTE: Our cppcheck command depends on a successful compilation using our Zig build system"
              ${pkgs.cppcheck}/bin/cppcheck --project=$PWD/zig-cache/compile_commands.json --cppcheck-build-dir=$PWD/zig-cache --enable=unusedFunction
            '')

            # IMPROVE: Add a test for running auval on our AUv2

            (pkgs.writeShellScriptBin "test-pluginval" ''
              ${pluginval}/bin/pluginval ${zig-out-folder}/Floe.vst3
            '')

            (pkgs.writeShellScriptBin "test-clap-validator" ''
              ${clap-val}/bin/clap-validator validate ${zig-out-folder}/Floe.clap
            '')

            (pkgs.writeShellScriptBin "test-vst3-validator" ''
              ${zig-out-folder}/VST3-Validator ${zig-out-folder}/Floe.vst3
            '')

            (pkgs.writeShellScriptBin "test-units" ''
              ${zig-out-folder}/tests
            '')

            (pkgs.writeShellScriptBin "analyzed-build" ''
              artifactDir=zig-cache/tmp
              reportFile=zig-cache/build-report
              mkdir -p ''${artifactDir}
              ${clang-build-analyzer}/bin/ClangBuildAnalyzer --start ''${artifactDir}
              "$@"
              returnCode=$?
              ${clang-build-analyzer}/bin/ClangBuildAnalyzer --stop ''${artifactDir} ''${reportFile}
              ${clang-build-analyzer}/bin/ClangBuildAnalyzer --analyze ''${reportFile}
              exit ''${returnCode}
            '')

            # dsymutil internally calls "lipo", so we have to make sure it's available under that name
            (pkgs.writeShellScriptBin "lipo" "llvm-lipo $@")
          ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            pkgs.pkg-config
            pkgs.alsa-lib
            pkgs.xorg.libX11
            pkgs.xorg.libXext
            pkgs.xorg.libXcursor
            pkgs.gtk4
            pkgs.gnome.zenity
            pkgs.libGL
            pkgs.libGLU
          ];
          shellHook = ''
            export MACOSX_SDK_SYSROOT="${macosx-sdks}"

            echo "Setting up git hooks..."
            mkdir -p ".git/hooks"
            echo "a"

            preCommit=".git/hooks/pre-commit"
            echo "b"
            echo "#!/bin/sh" > ''${preCommit}
            echo "c"
            echo "echo 'Running format-all...'" >> ''${preCommit}
            echo "d"
            echo "format-all" >> ''${preCommit}
            echo "e"
            chmod +x ''${preCommit}
            echo "f"
          '';
        };
      });
}
