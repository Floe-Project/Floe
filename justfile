# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

# This file assumes 'nix develop' has already been run

set dotenv-load

native_arch_os_pair := arch() + "-" + os()
native_binary_dir := "zig-out/" + native_arch_os_pair
all_src_files := 'fd . -e .mm -e .cpp -e .hpp -e .h src' 
gen_files_dir := "build_gen"
release_files_dir := justfile_directory() + "/zig-out/release"
build_resources_core := justfile_directory() + "/build_resources/Core"
cache_dir := ".zig-cache"
run_windows_program := if os() == 'windows' {
  ''
} else {
  'wine'
}

build target_os='native':
  zig build compile -Dtargets={{target_os}} -Dbuild-mode=development

build-tracy:
  zig build compile -Dtargets=native -Dbuild-mode=development -Dtracy

build-release target_os='native':
  zig build compile -Dtargets={{target_os}} -Dbuild-mode=production -Dcore-lib-path="{{build_resources_core}}"

# build and report compile-time statistics
build-timed target_os='native':
  #!/usr/bin/env bash
  artifactDir={{gen_files_dir}}/clang-build-analyzer-artifacts
  reportFile={{gen_files_dir}}/clang-build-analyzer-report
  mkdir -p ''${artifactDir}
  ClangBuildAnalyzer --start ${artifactDir}
  time just build {{target_os}}
  returnCode=$?
  ClangBuildAnalyzer --stop ${artifactDir} ${reportFile}
  ClangBuildAnalyzer --analyze ${reportFile}
  exit ${returnCode}

check-reuse:
  reuse lint

check-format:
  {{all_src_files}} | xargs clang-format --dry-run --Werror

# hunspell doesn't do anything fancy at all, it just checks each word for spelling. It means we get lots of
# false positives, but I think it's still worth it. We can just add words to ignored-spellings.dic.
[unix]
check-spelling:
  #!/usr/bin/env bash
  output=$(fd . -e .md --exclude third_party_libs/ --exclude src/readme.md | xargs hunspell -l -d en_GB -p docs/ignored-spellings.dic)
  echo "$output"
  if [[ -n "$output" ]]; then
    exit 1
  fi

# install Compile DataBase (compile_commands.json)
install-cbd arch_os_pair=native_arch_os_pair:
  cp {{gen_files_dir}}/compile_commands_{{arch_os_pair}}.json {{gen_files_dir}}/compile_commands.json

clang-tidy arch_os_pair=native_arch_os_pair: (install-cbd arch_os_pair)
  #!/usr/bin/env bash
  jq -r '.[].file' {{gen_files_dir}}/compile_commands_{{arch_os_pair}}.json | xargs clang-tidy -p {{gen_files_dir}} 

clang-tidy-all: (clang-tidy "x86_64-linux") (clang-tidy "x86_64-windows") (clang-tidy "aarch64-macos")

# IMPROVE: (June 2024) cppcheck v2.14.0 and v2.14.1 thinks there are syntax errors in valid code. It could be a cppcheck bug or it could be an incompatibility in how we are using it. Regardless, we should try again in the future and see if it's fixed. If it works it should run alongside clang-tidy in CI, etc.
# cppcheck arch_os_pair=native_arch_os_pair:
#   # IMPROVE: use --check-level=exhaustive?
#   # IMPROVE: investigate other flags such as --enable=constVariable
#   cppcheck --project={{justfile_directory()}}/{{gen_files_dir}}/compile_commands_{{arch_os_pair}}.json --cppcheck-build-dir={{justfile_directory()}}/.zig-cache --enable=unusedFunction --error-exitcode=2

_build_if_requested condition build-type:
  if [[ -n "{{condition}}" ]]; then just build {{build-type}}; fi

format:
  {{all_src_files}} | xargs clang-format -i

test-clap-val build="": (_build_if_requested build "native")
  clap-validator validate --in-process {{native_binary_dir}}/Floe.clap

test-units build="" +args="": (_build_if_requested build "native")
  {{native_binary_dir}}/tests {{args}}

test-pluginval build="": (_build_if_requested build "native")
  pluginval {{native_binary_dir}}/Floe.vst3

test-pluginval-au build="": (_build_if_requested build "native")
  pluginval {{native_binary_dir}}/Floe.component

test-vst3-val build="": (_build_if_requested build "native")
  timeout 2 {{native_binary_dir}}/VST3-Validator {{native_binary_dir}}/Floe.vst3

_download-and-unzip-to-cache-dir url:
  #!/usr/bin/env bash
  mkdir -p {{cache_dir}}
  pushd {{cache_dir}}
  wget {{url}} 
  basename=$(basename {{url}})
  unzip $basename
  rm $basename
  popd

[linux, windows]
test-windows-units:
  {{run_windows_program}} zig-out/x86_64-windows/tests.exe --log-level=debug

[linux, windows]
test-windows-vst3-val:
  {{run_windows_program}} zig-out/x86_64-windows/VST3-Validator.exe zig-out/x86_64-windows/Floe.vst3

[linux, windows]
test-windows-pluginval:
  #!/usr/bin/env bash
  # if pluginval is not available, download it
  if [[ ! -f {{cache_dir}}/pluginval.exe ]]; then
    just _download-and-unzip-to-cache-dir "https://github.com/Tracktion/pluginval/releases/download/v1.0.3/pluginval_Windows.zip"
  fi
  {{run_windows_program}} {{cache_dir}}/pluginval.exe --verbose --validate zig-out/x86_64-windows/Floe.vst3

[linux, windows]
test-windows-clap-val:
  #!/usr/bin/env bash
  if [[ ! -f {{cache_dir}}/clap-validator.exe ]]; then
    just _download-and-unzip-to-cache-dir  "https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-windows.zip"
  fi
  {{run_windows_program}} {{cache_dir}}/clap-validator.exe validate zig-out/x86_64-windows/Floe.clap

[linux]
coverage build="": (_build_if_requested build "native")
  mkdir -p {{gen_files_dir}}
  # IMPROVE: run other tests with coverage and --merge the results
  kcov --include-pattern={{justfile_directory()}}/src {{gen_files_dir}}/coverage-out {{native_binary_dir}}/tests

[linux]
valgrind build="": (_build_if_requested build "native")
  valgrind --fair-sched=yes {{native_binary_dir}}/tests

# IMPROVE: add auval tests on macos
checks_level_0 := replace( 
  "
  check-reuse
  check-format
  test-units
  test-clap-val
  test-pluginval
  test-vst3-val
  " + 
  if os() == "linux" {
    "
    test-windows-vst3-val
    test-windows-pluginval
    test-windows-clap-val
    test-windows-units
    "
  } else {
    "test-pluginval-au"
  }, "\n", " ")

checks_level_1 := checks_level_0 + replace( 
  "
  clang-tidy
  ", "\n", " ")

# IMPROVE: Linux CI: enable plugin tests when we have a solution to the crashes
# IMPROVE: Linux CI: enable wine tests when we have a way to install wine on CI
checks_ci := replace(
  "
    test-units
    test-clap-val
    test-pluginval
    test-vst3-val
  " +
  if os() == "linux" {
    "
    check-reuse 
    check-format
    check-spelling
    coverage
    clang-tidy-all
    "
  } else {
    "
    test-pluginval-au
    "
  }, "\n", " ")

[unix]
test level="0" build="": (_build_if_requested build "dev") (parallel if level == "0" { checks_level_0 } else { checks_level_1 })

[unix]
test-ci: (parallel checks_ci)

[windows, linux]
test-ci-windows:
  #!/usr/bin/env bash

  failed=0

  if [[ -z $GITHUB_ACTIONS ]]; then
    mkdir -p {{gen_files_dir}}
    rm -f {{gen_files_dir}}/test_ci_windows_summary.md
    export GITHUB_STEP_SUMMARY={{gen_files_dir}}/test_ci_windows_summary.md
  fi

  echo "# Summary (Windows)" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY
  echo "| Command | Return-Code |" >> $GITHUB_STEP_SUMMARY
  echo "| --- | --- |" >> $GITHUB_STEP_SUMMARY

  test() {
    local name="$1"

    just $name
    local result=$?
    echo "| $name | $result |" >> $GITHUB_STEP_SUMMARY
    [[ $result -ne 0 ]] && failed=1
  }
  
  test test-windows-pluginval
  test test-windows-units
  test test-windows-vst3-val
  test test-windows-clap-val

  exit $failed

[unix]
parallel tasks:
  #!/usr/bin/env bash
  mkdir -p {{gen_files_dir}}
  results_json={{gen_files_dir}}/results.json

  # use the --bar argument only if we are not on GITHUB_ACTIONS
  progress_bar=""
  [[ -z $GITHUB_ACTIONS ]] && progress_bar="--bar"

  parallel $progress_bar --results $results_json just ::: {{tasks}}

  # parallel's '--results x.json' flag does not produce valid JSON, so we need to fix it
  sed 's/$/,/' $results_json | head -c -2 > results.json.tmp
  { echo "["; cat results.json.tmp; echo "]"; } > $results_json
  rm results.json.tmp

  # remove any items where `Command == ""` (for some reason just adds these)
  jq "[ .[] | select(.Command != \"\") ]" $results_json > results.json.tmp
  mv results.json.tmp $results_json

  # print stdout and stderr for failed 
  jq -r '.[] | select(.Exitval != 0) | "\n\u001b[34m[Stdout] \(.Command):\u001b[0m", .Stdout, "\n\u001b[34m[Stderr] \(.Command):\u001b[0m", .Stderr' $results_json

  # prepare a TSV summary of the results
  summary=$(jq -r '["Command", "Time(s)", "Return-Code"], (.[] | [.Command, .JobRuntime, .Exitval]) | @tsv' $results_json)
  failed=$(jq '. | map(select(.Exitval != 0)) | length' $results_json)
  num_tasks=$(jq '. | length' $results_json)

  # use Miller to pretty-print the summary, along with a markdown version for GitHub Actions
  echo -e "\033[0;34m[Summary]\033[0m"
  [[ ! -z $GITHUB_ACTIONS ]] && echo "# Summary ({{os()}})" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY
  printf "%s\n" "$summary" | mlr --itsv --opprint sort -f "Return-Code"
  [[ ! -z $GITHUB_ACTIONS ]] && printf "%s\n" "$summary" | mlr --itsv --omd sort -f "Return-Code" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY

  if [ $failed -eq 0 ]; then
    echo -e "\033[0;32mAll $num_tasks tasks passed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo "### :white_check_mark: All $num_tasks tasks succeeded" >> $GITHUB_STEP_SUMMARY
    exit 0
  else
    echo -e "\033[0;31m$failed/$num_tasks tasks failed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo "### :x: $failed/$num_tasks tasks failed" >> $GITHUB_STEP_SUMMARY
    exit 1
  fi

[unix]
latest-changes:
  #!/usr/bin/env bash
  changes=$(sed -n "/## $(cat version.txt)/,/## /{ /## /!p }" changelog.md)
  printf "%s" "$changes" # trim trailing newline

[unix]
fetch-core-library:
  #!/usr/bin/env bash
  mkdir -p build_resources
  cd build_resources
  wget https://github.com/Floe-Synth/Core-Library/archive/refs/heads/main.zip
  unzip main.zip
  rm main.zip
  mv Core-Library-main "{{build_resources_core}}"

[unix, no-cd]
_try-add-core-library-to-zip zip-path:
  #!/usr/bin/env bash
  if [[ -d "{{build_resources_core}}" ]]; then
    # need to do some faffing with folders so that we only zip the library and none of the parent folders
    full_zip_path=$(realpath "{{zip-path}}")
    core_dirname=$(dirname "{{build_resources_core}}")
    core_filename=$(basename "{{build_resources_core}}")
    pushd "$core_dirname"
    zip -r "$full_zip_path" "$core_filename"
    popd
  fi

[unix, no-cd]
_create-manual-install-readme os_name:
  #!/usr/bin/env bash
  echo "These are the manual-install {{os_name}} plugin files and Core library for Floe version $version." > readme.txt
  echo "" >> readme.txt
  echo "It's normally recommended to use the installer instead of these manual-install files." >> readme.txt
  echo "The installer is a separate download to this." >> readme.txt
  echo "" >> readme.txt
  echo "Installation instructions: https://floe-synth.github.io/Floe/" >> readme.txt

[unix, no-cd]
windows-codesign-file file description:
  #!/usr/bin/env bash
  set -euo pipefail # don't use 'set -x' because it might print sensitive information

  cert_file={{justfile_directory()}}/{{gen_files_dir}}/windows-codesign-cert.pfx
  if [[ ! -f $cert_file ]]; then
    # decode the base64-encoded certificate string
    echo "$WINDOWS_CODESIGN_CERT_PFX" | base64 -d > $cert_file
  fi

  # signtool.exe alternative
  osslsigncode sign \
    -pkcs12 $cert_file \
    -pass "$WINDOWS_CODESIGN_CERT_PFX_PASSWORD" \
    -n "{{description}}" \
    -i https://github.com/Floe-Synth/Floe \
    -t http://timestamp.sectigo.com \
    -in "{{file}}" \
    -out "{{file}}.signed"

  mv {{file}}.signed {{file}}

[unix]
windows-prepare-release:
  #!/usr/bin/env bash
  set -euxo pipefail

  version=$(cat version.txt)

  mkdir -p {{release_files_dir}}

  [[ ! -d zig-out/x86_64-windows ]] && echo "x86_64-windows folder not found" && exit 1
  cd zig-out/x86_64-windows

  just windows-codesign-file Floe.vst3 "Floe VST3"
  just windows-codesign-file Floe.clap "Floe CLAP"

  installer_file=$(find . -type f -name "*Installer*.exe")
  just windows-codesign-file $installer_file "Installer for Floe"

  # zip the installer
  final_installer_name=$(echo $installer_file | sed 's/.exe//')
  final_installer_zip_name="Floe-Installer-v$version-Windows.zip"
  zip -r $final_installer_zip_name $installer_file
  mv $final_installer_zip_name {{release_files_dir}}

  # zip the manual-install files
  just _create-manual-install-readme "Windows"
  final_manual_zip_name="Floe-Manual-Install-v$version-Windows.zip"
  zip -r $final_manual_zip_name Floe.vst3 Floe.clap readme.txt
  just _try-add-core-library-to-zip $final_manual_zip_name
  rm readme.txt
  mv $final_manual_zip_name {{release_files_dir}}

[macos, no-cd]
macos-notarize file:
  #!/usr/bin/env bash
  set -euo pipefail # don't use 'set -x' because it might print sensitive information
  xcrun notarytool submit {{file}} --apple-id "$MACOS_NOTARIZATION_USERNAME" --password "$MACOS_NOTARIZATION_PASSWORD" --team-id $MACOS_TEAM_ID --wait

[macos]
macos-prepare-release-plugins:
  #!/usr/bin/env bash
  set -euo pipefail # don't use 'set -x' because it might print sensitive information
  [[ ! -f version.txt ]] && echo "version.txt file not found" && exit 1
  [[ ! -d zig-out/universal-macos ]] && echo "universal-macos folder not found" && exit 1

  version=$(cat version.txt)
  mkdir -p {{release_files_dir}}

  cd zig-out/universal-macos

  # step 1: codesign
  cat >plugin.entitlements <<EOF
  <?xml version="1.0" encoding="UTF-8"?>
  <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
  <plist version="1.0">
  <dict>
      <key>com.apple.security.app-sandbox</key>
      <true/>
      <key>com.apple.security.files.user-selected.read-write</key>
      <true/>
      <key>com.apple.security.assets.music.read-write</key>
      <true/>
      <key>com.apple.security.files.bookmarks.app-scope</key>
      <true/>
  </dict>
  </plist>
  EOF

  codesign_plugin() {
    codesign --sign "$MACOS_DEV_ID_APP_NAME" --timestamp --options=runtime --deep --force --entitlements plugin.entitlements $1
  }

  # we can do it in parallel for speed, but we need to be careful there's no conflicting use of the filesystem
  export -f codesign_plugin
  SHELL=$(type -p bash) parallel --bar codesign_plugin ::: Floe.vst3 Floe.component Floe.clap

  rm plugin.entitlements

  # step 2: notarize
  notarize_plugin() {
    plugin=$1
    temp_subdir=notarizing_$plugin

    rm -rf $temp_subdir
    mkdir -p $temp_subdir
    zip -r $temp_subdir/$plugin.zip $plugin

    just macos-notarize $temp_subdir/$plugin.zip

    unzip $temp_subdir/$plugin.zip -d $temp_subdir
    xcrun stapler staple $temp_subdir/$plugin
    # replace the original bundle with the stapled one
    rm -rf $plugin
    mv $temp_subdir/$plugin $plugin
    rm -rf $temp_subdir
  }

  # we can do it in parallel for speed, but we need to be careful there's no conflicting use of the filesystem
  export -f notarize_plugin
  SHELL=$(type -p bash) parallel --bar notarize_plugin ::: Floe.vst3 Floe.component Floe.clap

  # step 3: zip
  just _create-manual-install-readme "macOS"
  final_manual_zip_name="Floe-Manual-Install-v$version-macOS.zip"
  rm -f $final_manual_zip_name
  zip -r $final_manual_zip_name Floe.vst3 Floe.component Floe.clap readme.txt
  just _try-add-core-library-to-zip $final_manual_zip_name
  mv $final_manual_zip_name {{release_files_dir}}
  rm readme.txt

[macos]
macos-build-installer:
  #!/usr/bin/env bash
  set -euo pipefail # don't use 'set -x' because it might print sensitive information
  [[ ! -f version.txt ]] && echo "version.txt file not found" && exit 1
  [[ ! -d zig-out/universal-macos ]] && echo "universal-macos folder not found" && exit 1

  mkdir -p "{{release_files_dir}}"

  version=$(cat version.txt)
  universal_macos_abs_path="{{justfile_directory()}}/zig-out/universal-macos"
  final_installer_name="Floe-Installer-v$version"

  cd $universal_macos_abs_path

  temp_working_subdir="temp_installer_working_subdir"
  rm -rf "$temp_working_subdir"
  mkdir -p "$temp_working_subdir"
  pushd "$temp_working_subdir"

  distribution_xml_choices=""
  distribution_xml_choice_outlines=""

  add_package_to_distribution_xml() {
    local identifier="$1"
    local title="$2"
    local description="$3"
    local pkg_name="$4"

    local choice=$(cat <<EOF
    <choice id="$identifier" title="$title" description="$description">
        <pkg-ref id="$identifier" version="$version">$pkg_name</pkg-ref>
    </choice>
  EOF)
    # add the choices with correct newlines
    distribution_xml_choices=$(printf "%s%s\n" "$distribution_xml_choices" "$choice")
    distribution_xml_choice_outlines=$(printf "%s%s\n" "$distribution_xml_choice_outlines" "<line choice=\"$identifier\" />")
  }

  # step 1: make packages for each plugin so they are selectable options in the final installer
  make_package() {
    local file_extension="$1"
    local destination_plugin_folder="$2"
    local title="$3"
    local description="$4"

    local package_root="package_$file_extension"
    local install_folder="Library/Audio/Plug-Ins/$destination_plugin_folder"
    local identifier="com.Floe.$file_extension"
    local plugin_path="$universal_macos_abs_path/Floe.$file_extension"

    codesign --verify "$plugin_path" || { echo "ERROR: the plugin file isn't codesigned, do that before this command"; exit 1; }

    mkdir -p "$package_root/$install_folder"
    cp -r "$plugin_path" "$package_root/$install_folder"
    pkgbuild --analyze --root "$package_root" "$package_root.plist"
    pkgbuild --root "$package_root" --component-plist "$package_root.plist" --identifier "$identifier" --install-location / --version "$version" "$package_root.pkg"

    add_package_to_distribution_xml "$identifier" "$title" "$description" "$package_root.pkg"
  }

  make_package vst3 VST3 "Floe VST3" "VST3 format of the Floe plugin"
  make_package component Components "Floe AudioUnit (AUv2)" "AudioUnit (version 2) format of the Floe plugin"
  make_package clap CLAP "Floe CLAP" "CLAP format of the Floe plugin"

  # step 2: make a package to create empty folders that Floe might use
  mkdir -p floe_dirs
  pushd floe_dirs
  mkdir -p "Library/Application Support/Floe/Presets"
  mkdir -p "Library/Application Support/Floe/Libraries"
  popd
  pkgbuild --root floe_dirs --identifier com.Floe.dirs --install-location / --version "$version" floe_dirs.pkg
  add_package_to_distribution_xml \
    com.Floe.dirs \
    "Floe Folders" \
    "Create empty folders ready for Floe to use to look for libraries and presets" \
    floe_dirs.pkg
  
  # step 3: make a package for the core library
  if [[ -d "{{build_resources_core}}" ]]; then
    mkdir -p core_library
    pushd core_library
    install_folder="Library/Application Support/Floe/Libraries"
    mkdir -p "$install_folder"
    cp -r "{{build_resources_core}}" "$install_folder"
    popd
    pkgbuild --root core_library --identifier com.Floe.Core --install-location / --version "$version" core_library.pkg

    identifier=com.Floe.core

    add_package_to_distribution_xml \
      "com.Floe.core" \
      "Core Library" \
      "Core Floe library containing a few reverb impulses responses" \
      core_library.pkg
  fi

  # step 4: make the final installer combining all the packages
  mkdir -p productbuild_files
  echo "This application will install Floe on your computer. You will be able to select which types of audio plugin format you would like to install. Please note that sample libraries are separate: this installer just installs the Floe engine." > productbuild_files/welcome.txt

  # find the min macos version from one of the plugin's plists
  min_macos_version=$(grep -A 1 '<key>LSMinimumSystemVersion</key>' "$universal_macos_abs_path/Floe.vst3/Contents/Info.plist" | grep '<string>' | sed 's/.*<string>\(.*\)<\/string>.*/\1/')

  cat >distribution.xml <<EOF
  <installer-gui-script minSpecVersion="1">
      <title>Floe v$version</title>
      <welcome file="welcome.txt" mime-type="text/plain"/>
      <options customize="always" require-scripts="false"/>
      <os-version min="$min_macos_version" /> 
      $distribution_xml_choices
      <choices-outline>
          $distribution_xml_choice_outlines
      </choices-outline>
  </installer-gui-script>
  EOF

  productbuild --distribution distribution.xml --resources productbuild_files --package-path . unsigned.pkg
  productsign --timestamp --sign "$MACOS_DEV_ID_INSTALLER_NAME" unsigned.pkg "$universal_macos_abs_path/$final_installer_name.pkg"

  popd
  rm -rf "$temp_working_subdir"

  # step 5: notarize the installer
  just macos-notarize $final_installer_name.pkg
  xcrun stapler staple $final_installer_name.pkg

  # step 6: zip the installer
  final_zip_name="$final_installer_name-macOS.zip"
  rm -f "$final_zip_name"
  zip -r "$final_zip_name" "$final_installer_name.pkg"
  mv "$final_zip_name" "{{release_files_dir}}"

[macos]
macos-prepare-release: (macos-prepare-release-plugins) (macos-build-installer)
