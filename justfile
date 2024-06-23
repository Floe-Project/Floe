# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

# This file assumes 'nix develop' has already been run

set dotenv-load

native_arch_os_pair := arch() + "-" + os()
native_binary_dir := "zig-out/" + native_arch_os_pair
all_src_files := 'fd . -e .mm -e .cpp -e .hpp -e .h src' 
gen_files_dir := "build_gen"
release_files_dir := justfile_directory() + "/zig-out/release"

build target_os='native':
  zig build compile -Dtargets={{target_os}} -Dbuild-mode=development

build-tracy:
  zig build compile -Dtargets=native -Dbuild-mode=development -Dtracy

build-release target_os='native':
  zig build compile -Dtargets={{target_os}} -Dbuild-mode=production

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

# install compile database (compile_commands.json)
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

[linux]
test-wine-vst3-val build="": (_build_if_requested build "windows")
  wine zig-out/x86_64-windows/VST3-Validator.exe zig-out/x86_64-windows/Floe.vst3

[linux]
test-wine-pluginval build="": (_build_if_requested build "windows")
  wine $PLUGINVAL_WINDOWS_PATH zig-out/x86_64-windows/Floe.vst3

[linux]
test-wine-units build="": (_build_if_requested build "windows")
  wine zig-out/x86_64-windows/tests.exe

[linux]
test-wine-clap-val build="": (_build_if_requested build "windows")
  wine $CLAPVAL_WINDOWS_PATH validate zig-out/x86_64-windows/Floe.clap

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
    test-wine-vst3-val
    test-wine-pluginval
    test-wine-clap-val
    test-wine-units
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
    coverage
    clang-tidy-all
    "
  } else {
    "
    test-pluginval-au
    "
  }, "\n", " ")

test level="0" build="": (_build_if_requested build "dev") (parallel if level == "0" { checks_level_0 } else { checks_level_1 })

test-ci: (parallel checks_ci)

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

latest-changes:
  #!/usr/bin/env bash
  changes=$(sed -n "/## $(cat version.txt)/,/## /{ /## /!p }" changelog.md)
  printf "%s" "$changes" # trim trailing newline

[no-cd]
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
  just windows-codesign-file $installer_file "Installer for Floe audio plugin formats"

  # zip the installer
  final_installer_name=$(echo $installer_file | sed 's/.exe//')
  final_installer_zip_name="Floe-Installer-v$version-Windows.zip"
  zip -r $final_installer_zip_name $installer_file
  mv $final_installer_zip_name {{release_files_dir}}

  # zip the manual-install files
  echo "These are the manual-install Windows plugin files for Floe version $version." > readme.txt
  echo "" >> readme.txt
  echo "It's normally recommended to use the installer instead of these manual-install files." >> readme.txt
  echo "The installer is a separate download to this." >> readme.txt
  echo "" >> readme.txt
  echo "For for manual installation, you can copy these files to the appropriate folders:" >> readme.txt
  echo "  Floe.vst3: C:/Program Files/Common Files/VST3" >> readme.txt
  echo "  Floe.clap: C:/Program Files/Common Files/CLAP" >> readme.txt

  final_manual_zip_name="Floe-Manual-Install-v$version-Windows.zip"
  zip -r $final_manual_zip_name Floe.vst3 Floe.clap readme.txt
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

  export -f notarize_plugin
  SHELL=$(type -p bash) parallel --bar notarize_plugin ::: Floe.vst3 Floe.component Floe.clap

  # step 3: zip
  echo "These are the manual-install macOS plugin files for Floe version $version." > readme.txt
  echo "" >> readme.txt
  echo "It's normally recommended to use the installer instead of these manual-install files." >> readme.txt
  echo "The installer is a separate download to this." >> readme.txt
  echo "" >> readme.txt
  echo "For for manual installation, you can copy these files to the appropriate folders:" >> readme.txt
  echo "  Floe.vst3: MachintoshHD -> /Library/Audio/Plug-Ins/VST3" >> readme.txt
  echo "  Floe.component: MachintoshHD -> /Library/Audio/Plug-Ins/Components" >> readme.txt
  echo "  Floe.clap: MachintoshHD -> /Library/Audio/Plug-Ins/CLAP" >> readme.txt

  final_manual_zip_name="Floe-Manual-Install-v$version-macOS.zip"
  rm -f $final_manual_zip_name
  zip -r $final_manual_zip_name Floe.vst3 Floe.component Floe.clap readme.txt
  mv $final_manual_zip_name {{release_files_dir}}

  rm readme.txt

[macos]
macos-build-installer:
  #!/usr/bin/env bash
  set -euo pipefail # don't use 'set -x' because it might print sensitive information
  [[ ! -f version.txt ]] && echo "version.txt file not found" && exit 1
  [[ ! -d zig-out/universal-macos ]] && echo "universal-macos folder not found" && exit 1

  mkdir -p {{release_files_dir}}

  version=$(cat version.txt)
  universal_macos_abs_path="{{justfile_directory()}}/zig-out/universal-macos"
  final_installer_name="Floe-Installer-v$version"

  cd $universal_macos_abs_path

  temp_working_subdir="temp_installer_working_subdir"
  rm -rf $temp_working_subdir
  mkdir -p $temp_working_subdir
  cd $temp_working_subdir

  distribution_xml_choices=""
  distribution_xml_choice_outlines=""

  # step 1: make packages for each plugin so they are selectable options in the final installer
  make_package() {
    file_extension=$1
    destination_plugin_folder=$2
    title=$3
    description=$4

    package_root=package_$file_extension
    install_folder=Library/Audio/Plug-Ins/$destination_plugin_folder
    identifier=com.Floe.$file_extension
    plugin_path=$universal_macos_abs_path/Floe.$file_extension

    codesign --verify "$plugin_path" || { echo "ERROR: the plugin file isn't codesigned, do that before this command"; exit 1; }

    mkdir -p $package_root/$install_folder
    cp -r "$plugin_path" $package_root/$install_folder
    pkgbuild --analyze --root $package_root $package_root.plist
    pkgbuild --root $package_root --component-plist $package_root.plist --identifier $identifier --install-location / --version $version $package_root.pkg

    choice=$(cat <<EOF
    <choice id="$identifier" title="$title" description="$description">
        <pkg-ref id="$identifier" version="$version">$package_root.pkg</pkg-ref>
    </choice>
  EOF)
    distribution_xml_choices=$(printf "%s%s\n" "$distribution_xml_choices" "$choice")
    distribution_xml_choice_outlines=$(printf "%s%s\n" "$distribution_xml_choice_outlines" "<line choice=\"$identifier\" />")
  }

  make_package vst3 VST3 "Floe VST3" "VST3 format of the Floe plugin"
  make_package component Components "Floe AudioUnit (AUv2)" "AudioUnit (version 2) format of the Floe plugin"
  make_package clap CLAP "Floe CLAP" "CLAP format of the Floe plugin"

  # step 2: make a package to create empty folders that Floe might use
  mkdir -p floe_dirs
  cd floe_dirs
  mkdir -p Library/Application\ Support/Floe/Presets
  mkdir -p Library/Application\ Support/Floe/Libraries
  cd ../
  pkgbuild --root floe_dirs --identifier com.Floe.dirs --install-location / --version $version floe_dirs.pkg

  # step 3: make the final installer combining all the packages
  mkdir -p productbuild_files
  echo "This application will install Floe on your computer. You will be able to select which types of audio plugin format you would like to install. Please note that sample libraries are separate: this installer just installs the Floe engine." > productbuild_files/welcome.txt

  min_macos_version=$(grep -A 1 '<key>LSMinimumSystemVersion</key>' "$universal_macos_abs_path/Floe.vst3/Contents/Info.plist" | grep '<string>' | sed 's/.*<string>\(.*\)<\/string>.*/\1/')

  cat >distribution.xml <<EOF
  <installer-gui-script minSpecVersion="1">
      <title>Floe v$version</title>
      <welcome file="welcome.txt" mime-type="text/plain"/>
      <options customize="always" require-scripts="false"/>
      <os-version min="$min_macos_version" /> 
      <choice id="com.Floe.dirs" title="Floe Folders" description="Create empty folders ready for Floe to use to look for libraries and presets" enabled="false">
          <pkg-ref id="com.Floe.dirs" version="$version">floe_dirs.pkg</pkg-ref>
      </choice>
      $distribution_xml_choices
      <choices-outline>
          <line choice="com.Floe.dirs" />
          $distribution_xml_choice_outlines
      </choices-outline>
  </installer-gui-script>
  EOF

  productbuild --distribution distribution.xml --resources productbuild_files --package-path . unsigned.pkg
  productsign --timestamp --sign "$MACOS_DEV_ID_INSTALLER_NAME" unsigned.pkg $universal_macos_abs_path/$final_installer_name.pkg

  cd ../
  rm -rf $temp_working_subdir

  # step 4: notarize the installer
  just macos-notarize $final_installer_name.pkg
  xcrun stapler staple $final_installer_name.pkg

  # step 5: zip the installer
  final_zip_name=$final_installer_name-macOS.zip
  rm -f $final_zip_name
  zip -r $final_zip_name $final_installer_name.pkg
  mv $final_zip_name {{release_files_dir}}

[macos]
macos-prepare-release: (macos-prepare-release-plugins) (macos-build-installer)
