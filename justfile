# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

# NOTE: for the most part, we assume that `nix develop` has been run before using this justfile

set dotenv-load

native_arch_os_pair := arch() + "-" + os()
native_binary_dir := join("zig-out", native_arch_os_pair)
native_binary_dir_abs := join(justfile_directory(), native_binary_dir)
all_src_files := 'fd . -e .mm -e .cpp -e .hpp -e .h src' 
cache_dir := ".floe-cache"
release_files_dir := join(justfile_directory(), "zig-out", "release") # for final release files
run_windows_program := if os() == 'windows' {
  ''
} else {
  'wine'
}

# Info about External Resources
#
# There are a few things that we want to keep external to this repo but still have easy access to when developing and
# preparing final deployments. To achieve this, we have a designated sub-folder that is not checked into git (it's a 
# sub-folder because it's convenient and quite often zig needs things relative to the build directory). We pass this
# path into our zig build script for it to lookup filenames within it. These resources are not requirements: if 
# anything is missing you will get a warning, not an error, and some aspect of the build might be missing.
#
# Logos: the logos represent Floe's quality-assurance and recognition and so we don't use a GPL licence for them. 
# Therefore they're kept separate and they're optional.

external_resources := join(cache_dir, "external_resources")

# IMPORTANT: these must be kept in sync with the build.zig file
logos_abs_dir := join(justfile_directory(), external_resources, "Logos")

default: 
  #!/usr/bin/env bash
  if [[ -z "${DEFAULT_CMD:-}" ]]; then
    just build native
  else
    just $DEFAULT_CMD
  fi

alias pre-debug := build

build target_os='native':
  zig build compile -Dtargets={{target_os}} -Dbuild-mode=development -Dexternal-resources="{{external_resources}}"
  just patch-rpath


patch-rpath:
  #!/usr/bin/env bash
  if [[ "{{os()}}" == "linux" && ! -f "/etc/NIXOS" ]]; then
    patch_file() {
      command=$1
      file=$2

      if [[ ! -f $file ]]; then
        echo "patch-rpath: file not found: $file"
        exit 1
      fi

      $command $file
    }

    patch_file patchrpath "{{native_binary_dir}}/Floe.clap"
    patch_file patchrpath "{{native_binary_dir}}/Floe.vst3/Contents/x86_64-linux/Floe.so"
    patch_file patchinterpreter "{{native_binary_dir}}/tests"
    patch_file patchinterpreter "{{native_binary_dir}}/docs_preprocessor"
    patch_file patchinterpreter "{{native_binary_dir}}/VST3-Validator"
  fi

build-tracy:
  zig build compile -Dtargets=native -Dbuild-mode=development -Dtracy

build-release target_os='native':
  zig build compile -Dtargets={{target_os}} -Dbuild-mode=production -Dexternal-resources="{{external_resources}}"

# build and report compile-time statistics
build-timed target_os='native':
  #!/usr/bin/env bash
  artifactDir={{cache_dir}}/clang-build-analyzer-artifacts
  reportFile={{cache_dir}}/clang-build-analyzer-report
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
# In vim, use :sort u to remove duplicates>
[unix]
check-spelling:
  #!/usr/bin/env bash
  output=$(fd . -e .md --exclude third_party_libs/ --exclude src/readme.md | xargs hunspell -l -d en_GB -p docs/ignored-spellings.dic)
  echo "$output"
  if [[ -n "$output" ]]; then
    exit 1
  fi

[unix]
check-links:
  lychee --exclude 'v%7B%7B#include' docs readme.md changelog.md

# install Compile DataBase (compile_commands.json)
install-cbd arch_os_pair=native_arch_os_pair:
  cp {{cache_dir}}/compile_commands_{{arch_os_pair}}.json {{cache_dir}}/compile_commands.json

clang-tidy arch_os_pair=native_arch_os_pair: (install-cbd arch_os_pair)
  #!/usr/bin/env bash
  # NOTE: we specify the config file because we don't want clang-tidy to go automatically looking for it and 
  # sometimes finding .clang-tidy files in third-party libraries that are incompatible with our version of clang-tidy
  jq -r '.[].file' {{cache_dir}}/compile_commands_{{arch_os_pair}}.json | xargs clang-tidy --config-file=.clang-tidy -p {{cache_dir}} 

clang-tidy-all: (clang-tidy "x86_64-linux") (clang-tidy "x86_64-windows") (clang-tidy "aarch64-macos")

upload-crashes:
  #!/usr/bin/env bash
  set -euxo pipefail
  
  case "$(uname -s)" in
    Linux*)   dir="$HOME/.local/state/Floe" ;;
    Darwin*)  dir="$HOME/Library/Logs/Floe" ;;
    MINGW*|CYGWIN*|MSYS*) dir="$LOCALAPPDATA/Floe" ;;
    *) echo "Unsupported OS" && exit 1 ;;
  esac

  if [ ! -d "$dir" ]; then
    exit 0
  fi
  
  cd "$dir" || exit 1
  for crash in *.floe-error; do
    if [ -f "$crash" ]; then
      sentry-cli send-envelope --raw "$crash"
    fi
  done

# IMPROVE: (June 2024) cppcheck v2.14.0 and v2.14.1 thinks there are syntax errors in valid code. It could be a cppcheck bug or it could be an incompatibility in how we are using it. Regardless, we should try again in the future and see if it's fixed. If it works it should run alongside clang-tidy in CI, etc.
# cppcheck arch_os_pair=native_arch_os_pair:
#   # IMPROVE: use --check-level=exhaustive?
#   # IMPROVE: investigate other flags such as --enable=constVariable
#   cppcheck --project={{justfile_directory()}}/{{cache_dir}}/compile_commands_{{arch_os_pair}}.json --cppcheck-build-dir={{justfile_directory()}}/.zig-cache --enable=unusedFunction --error-exitcode=2

_build_if_requested condition build-type:
  if [[ -n "{{condition}}" ]]; then just build {{build-type}}; fi

format:
  {{all_src_files}} | xargs clang-format -i

test-clap-val build="": (_build_if_requested build "native")
  clap-validator validate --in-process {{native_binary_dir}}/Floe.clap

test-units build="" +args="": (_build_if_requested build "native")
  {{native_binary_dir}}/tests {{args}} --log-level=debug

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
  curl -O -L {{url}} 
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
  exe="{{cache_dir}}/pluginval.exe"
  if [[ ! -f "$exe" ]]; then
    just _download-and-unzip-to-cache-dir "https://github.com/Tracktion/pluginval/releases/download/v1.0.3/pluginval_Windows.zip"
    chmod +x "$exe"
  fi
  {{run_windows_program}} "$exe" --verbose --validate zig-out/x86_64-windows/Floe.vst3

[linux, windows]
test-windows-clap-val:
  #!/usr/bin/env bash
  exe="{{cache_dir}}/clap-validator.exe"
  if [[ ! -f "$exe" ]]; then
    just _download-and-unzip-to-cache-dir  "https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-windows.zip"
    chmod +x "$exe"
  fi
  {{run_windows_program}} "$exe" validate zig-out/x86_64-windows/Floe.clap

[linux]
coverage build="": (_build_if_requested build "native")
  mkdir -p {{cache_dir}}
  # IMPROVE: run other tests with coverage and --merge the results
  kcov --include-pattern={{justfile_directory()}}/src {{cache_dir}}/coverage-out {{native_binary_dir}}/tests

# IMPROVE: also run validators through valgrind
[linux]
valgrind build="": (_build_if_requested build "native")
  valgrind \
    --leak-check=full \
    --fair-sched=yes \
    --num-callers=25 \
    --gen-suppressions=all \
    --suppressions=valgrind.supp \
    --error-exitcode=1 \
    --exit-on-first-error=no \
    {{native_binary_dir}}/tests

# TODO: add vst3-val, pluginval and plugival-au (and wine variants) when we re-enable wrappers
checks_level_0 := replace( 
  "
  check-reuse
  check-format
  check-links
  check-spelling
  test-units
  test-clap-val
  " + 
  if os() == "linux" {
    "
    test-windows-clap-val
    test-windows-units
    "
  } else {
    "
    "
  }, "\n", " ")

checks_level_1 := checks_level_0 + replace( 
  "
  clang-tidy
  ", "\n", " ")

# TODO: add vst3-val, pluginval and plugival-au when we re-enable wrappers
checks_ci := replace(
  "
    test-units
    test-clap-val
  " +
  if os() == "linux" {
    "
    check-reuse 
    check-format
    check-spelling
    check-links
    coverage
    clang-tidy-all
    valgrind
    "
  } else {
    "
    "
  }, "\n", " ")

[unix]
test level="0" build="": (_build_if_requested build "dev") (parallel if level == "0" { checks_level_0 } else { checks_level_1 })

[unix]
test-ci: (parallel checks_ci)

[unix]
install-pre-commit-hook:
  rm -f .git/hooks/pre-commit
  echo "#!/usr/bin/env bash" > .git/hooks/pre-commit
  echo "set -euo pipefail" >> .git/hooks/pre-commit
  echo "echo '[===] Running pre-commit checks...'" >> .git/hooks/pre-commit
  echo "just pre-commit-checks" >> .git/hooks/pre-commit
  echo "echo '[===] All pre-commit checks passed'" >> .git/hooks/pre-commit
  echo "echo ''" >> .git/hooks/pre-commit
  chmod +x .git/hooks/pre-commit

[unix]
pre-commit-checks: (parallel "check-format check-reuse check-spelling check-links") 

_print-ci-summary num_tasks num_failed:
  #!/usr/bin/env bash
  if [ "{{num_failed}}" -eq 0 ]; then
    echo -e "\033[0;32mAll {{num_tasks}} tasks passed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo "### :white_check_mark: All {{num_tasks}} tasks succeeded" >> $GITHUB_STEP_SUMMARY
  else
    echo -e "\033[0;31m{{num_failed}}/{{num_tasks}} tasks failed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo "### :x: {{num_failed}}/{{num_tasks}} tasks failed" >> $GITHUB_STEP_SUMMARY
  fi
  exit 0

[windows, linux]
test-ci-windows:
  #!/usr/bin/env bash

  num_tasks=0
  num_failed=0

  if [[ ! -v GITHUB_ACTIONS ]]; then
    mkdir -p {{cache_dir}}
    rm -f {{cache_dir}}/test_ci_windows_summary.md
    export GITHUB_STEP_SUMMARY={{cache_dir}}/test_ci_windows_summary.md
  fi

  echo "# Summary (Windows)" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY
  echo "| Command | Return-Code |" >> $GITHUB_STEP_SUMMARY
  echo "| --- | --- |" >> $GITHUB_STEP_SUMMARY

  test() {
    local name="$1"

    local result=0
    if [[ "{{os()}}" == "linux" ]]; then
      just "$name"
      result=$?
    else
      # on Windows the return code is lost for some reason, we have to parse the output
      local stderr_output=$(just "$name" 2>&1 1>/dev/null)
      echo "$stderr_output"
      local last_line=$(echo "$stderr_output" | tail -n 1)
      if [[ $last_line == *"failed on line"* ]]; then
        result=1
      fi
    fi

    echo "| $name | $result |" >> $GITHUB_STEP_SUMMARY
    num_tasks=$((num_tasks + 1))
    [[ $result -ne 0 ]] && num_failed=$((num_failed + 1))
  }
  
  # test test-windows-pluginval # TODO: re-enable when wrappers are supported
  test test-windows-units
  # test test-windows-vst3-val # TODO: re-enable when wrappers are supported
  test test-windows-clap-val

  if [[ ! -v GITHUB_ACTIONS ]]; then
    cat {{cache_dir}}/test_ci_windows_summary.md
  fi

  just _print-ci-summary $num_tasks $num_failed
  if [ $num_failed -ne 0 ]; then
    exit 1
  fi

[unix]
parallel tasks:
  #!/usr/bin/env bash
  mkdir -p {{cache_dir}}
  results_json={{cache_dir}}/parallel_cmd_results.json

  # use the --bar argument only if we are not on GITHUB_ACTIONS
  progress_bar=""
  [[ -z $GITHUB_ACTIONS ]] && progress_bar="--bar"

  parallel $progress_bar --results $results_json just ::: {{tasks}}

  # parallel's '--results x.json' flag does not produce valid JSON, so we need to fix it
  sed 's/$/,/' $results_json | head -c -2 > parallel_cmd_results.json.tmp
  { echo "["; cat parallel_cmd_results.json.tmp; echo "]"; } > $results_json
  rm parallel_cmd_results.json.tmp

  # remove any items where `Command == ""` (for some reason just adds these)
  jq "[ .[] | select(.Command != \"\") ]" $results_json > parallel_cmd_results.json.tmp
  mv parallel_cmd_results.json.tmp $results_json

  # print stdout and stderr for failed 
  jq -r '.[] | select(.Exitval != 0) | "\n\u001b[34m[Stdout] \(.Command):\u001b[0m", .Stdout, "\n\u001b[34m[Stderr] \(.Command):\u001b[0m", .Stderr' $results_json

  # prepare a TSV summary of the results
  summary=$(jq -r '["Command", "Time(s)", "Return-Code"], (.[] | [.Command, .JobRuntime, .Exitval]) | @tsv' $results_json)
  num_failed=$(jq '. | map(select(.Exitval != 0)) | length' $results_json)
  num_tasks=$(jq '. | length' $results_json)

  # use Miller to pretty-print the summary, along with a markdown version for GitHub Actions
  echo -e "\033[0;34m[Summary]\033[0m"
  [[ ! -z $GITHUB_ACTIONS ]] && echo "# Summary ({{os()}})" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY
  printf "%s\n" "$summary" | mlr --itsv --opprint sort -f "Return-Code"
  [[ ! -z $GITHUB_ACTIONS ]] && printf "%s\n" "$summary" | mlr --itsv --omd sort -f "Return-Code" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY

  just _print-ci-summary $num_tasks $num_failed
  if [ $num_failed -ne 0 ]; then
    exit 1
  fi

[unix]
echo-latest-changes:
  #!/usr/bin/env bash
  # we look for the heading with the version number and then print everything until the next heading
  changes=$(sed -n "/## $(cat version.txt)/,/## /{ /## /!p }" changelog.md)
  printf "%s" "$changes" # trim trailing newline

[unix]
_fetch-external-github-repo owner repo destination:
  #!/usr/bin/env bash
  dirname=$(dirname "{{destination}}")
  mkdir -p "$dirname"
  cd "$dirname"
  wget "https://github.com/{{owner}}/{{repo}}/archive/refs/heads/main.zip"
  unzip main.zip
  rm main.zip
  rm -rf "{{destination}}"
  mv "{{repo}}-main" "{{destination}}"

# NOTE: the logos probably have reserved copyright
[unix]
fetch-logos: (_fetch-external-github-repo "Floe-Project" "Floe-Logos" logos_abs_dir)

[unix, no-cd]
_create-manual-install-readme os_name:
  #!/usr/bin/env bash
  echo "These are the manual-install {{os_name}} plugin files for Floe version $(cat {{justfile_directory()}}/version.txt)." > readme.txt
  echo "" >> readme.txt
  echo "It's normally easier to use the installer instead of these manual-install files." >> readme.txt
  echo "The installer is a separate download to this." >> readme.txt
  echo "" >> readme.txt
  echo "Installation instructions: https://floe.audio/" >> readme.txt

[unix, no-cd]
windows-codesign-file file description:
  #!/usr/bin/env bash
  set -euo pipefail # don't use 'set -x' because it might print sensitive information

  cert_file={{justfile_directory()}}/{{cache_dir}}/windows-codesign-cert.pfx
  if [[ ! -f $cert_file ]]; then
    # decode the base64-encoded certificate string
    echo "$WINDOWS_CODESIGN_CERT_PFX" | base64 -d > $cert_file
  fi

  # signtool.exe alternative
  osslsigncode sign \
    -pkcs12 $cert_file \
    -pass "$WINDOWS_CODESIGN_CERT_PFX_PASSWORD" \
    -n "{{description}}" \
    -i https://github.com/Floe-Project/Floe \
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

  # just windows-codesign-file Floe.vst3 "Floe VST3" # TODO: re-enable when wrappers are supported
  just windows-codesign-file Floe.clap "Floe CLAP"
  just windows-codesign-file floe-packager.exe "Floe Packager"

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
  # TODO: add Floe.vst3 to zip when wrappers are supported
  zip -r $final_manual_zip_name Floe.clap readme.txt
  rm readme.txt
  mv $final_manual_zip_name {{release_files_dir}}

  # zip the packager
  final_packager_zip_name="Floe-Packager-v$version-Windows.zip"
  zip -r $final_packager_zip_name floe-packager.exe
  mv $final_packager_zip_name {{release_files_dir}}

[macos, no-cd]
macos-notarize file:
  #!/usr/bin/env bash
  set -euo pipefail # don't use 'set -x' because it might print sensitive information
  xcrun notarytool submit {{file}} --apple-id "$MACOS_NOTARIZATION_USERNAME" --password "$MACOS_NOTARIZATION_PASSWORD" --team-id $MACOS_TEAM_ID --wait

[macos]
macos-prepare-packager:
  #!/usr/bin/env bash
  set -euo pipefail # don't use 'set -x' because it might print sensitive information
  [[ ! -f version.txt ]] && echo "version.txt file not found" && exit 1
  [[ ! -d zig-out/universal-macos ]] && echo "universal-macos folder not found" && exit 1

  version=$(cat version.txt)
  mkdir -p {{release_files_dir}}

  cd zig-out/universal-macos

  codesign --sign "$MACOS_DEV_ID_APP_NAME" --timestamp --options=runtime --force floe-packager

  final_packager_zip_name="Floe-Packager-v$version-macOS.zip"
  zip $final_packager_zip_name floe-packager

  just macos-notarize "$final_packager_zip_name"
  # NOTE: we can't staple the packager because it's a unix binary 

  mv $final_packager_zip_name {{release_files_dir}}

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

  # TODO: add Floe.vst3 and Floe.component when wrappers are supported
  plugin_list="Floe.clap"

  # we can do it in parallel for speed, but we need to be careful there's no conflicting use of the filesystem
  export -f codesign_plugin
  SHELL=$(type -p bash) parallel --bar codesign_plugin ::: $plugin_list

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
  SHELL=$(type -p bash) parallel --bar notarize_plugin ::: $plugin_list

  # step 3: zip
  just _create-manual-install-readme "macOS"
  final_manual_zip_name="Floe-Manual-Install-v$version-macOS.zip"
  rm -f $final_manual_zip_name
  zip -r $final_manual_zip_name $plugin_list readme.txt
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

  # make packages for each plugin so they are selectable options in the final installer
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

  # TODO: re-enable when wrappers are supported
  # make_package vst3 VST3 "Floe VST3" "VST3 format of the Floe plugin"
  # make_package component Components "Floe AudioUnit (AUv2)" "AudioUnit (version 2) format of the Floe plugin"
  make_package clap CLAP "Floe CLAP" "CLAP format of the Floe plugin"

  # make the final installer combining all the packages
  mkdir -p productbuild_files
  echo "This application will install Floe on your computer. You will be able to select which types of audio plugin format you would like to install. Please note that sample libraries are separate: this installer just installs the Floe engine." > productbuild_files/welcome.txt

  # find the min macos version from one of the plugin's plists
  min_macos_version=$(grep -A 1 '<key>LSMinimumSystemVersion</key>' "$universal_macos_abs_path/Floe.clap/Contents/Info.plist" | grep '<string>' | sed 's/.*<string>\(.*\)<\/string>.*/\1/')

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
macos-prepare-release: (macos-prepare-packager) (macos-prepare-release-plugins) (macos-build-installer)
