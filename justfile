# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

# This file assumes 'nix develop' has already been run

native_binary_dir := "zig-out/" + if os() == "linux" {
  "x86-linux"
} else if os() == "macos" {
  "x86-macos"
} else if os() == "windows" {
  "x86-windows"
} else {
  "unknown"
}

all_src_files := 'fd . -e .mm -e .cpp -e .hpp -e .h src' 

build target_os='native':
  time analyzed-build zig build compile -Dtargets={{target_os}} -Dbuild-mode=development 

check-reuse:
  reuse lint

check-format:
  {{all_src_files}} | xargs clang-format --dry-run --Werror

clang-tidy:
  jq -r '.[].file' build_gen/compile_commands.json | xargs clang-tidy -p build_gen 

cppcheck:
  cppcheck --project=build_gen/compile_commands.json --cppcheck-build-dir=.zig-cache --enable=unusedFunction

format:
  {{all_src_files}} | xargs clang-format -i

test-clap-val:
  clap-validator validate --in-process {{native_binary_dir}}/Floe.clap

test-units:
  {{native_binary_dir}}/tests

test-pluginval:
  pluginval {{native_binary_dir}}/Floe.vst3

test-vst3-val:
  timeout 2 {{native_binary_dir}}/VST3-Validator {{native_binary_dir}}/Floe.vst3

test-wine-vst3-val:
  wine zig-out/x86-windows/VST3-Validator.exe zig-out/x86-windows/Floe.vst3

test-wine-pluginval:
  wine $PLUGINVAL_WINDOWS_PATH zig-out/x86-windows/Floe.vst3

test-wine-units:
  wine zig-out/x86-windows/tests.exe

test-wine-clap-val:
  wine $CLAPVAL_WINDOWS_PATH validate zig-out/x86-windows/Floe.clap

# level 0: fast tests
level_0_common_tests := replace("""
  check-reuse 
  check-format
  test-clap-val
  test-units
  test-pluginval
  test-vst3-val
""", "\n", " ")

level_0_linux_tests := replace("""
  test-wine-vst3-val
  test-wine-pluginval
  test-wine-clap-val
  test-wine-units
""", "\n", " ")

level_0_tests := level_0_common_tests + if os() == "linux" {
  level_0_linux_tests
} else {
  ""
}

# level 1: slower tests
level_1_tests := replace("""
  clang-tidy
  cppcheck
""", "\n", " ")

tests level: (parallel if level == "0" { level_0_tests } else { level_0_tests + level_1_tests })

parallel tasks:
  #!/usr/bin/env bash
  mkdir -p build_gen
  results_json=build_gen/results.json

  parallel --bar --results $results_json just ::: {{tasks}}

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
  summary=$(jq -r '["Command", "Time(s)", "Return Code"], (.[] | [.Command, .JobRuntime, .Exitval]) | @tsv' $results_json)
  failed=$(jq '. | map(select(.Exitval != 0)) | length' $results_json)
  num_tasks=$(jq '. | length' $results_json)

  # use miller to pretty-print the summary, along with a markdown version for GitHub Actions
  echo -e "\033[0;34m[Summary]\033[0m"
  [[ ! -z $GITHUB_ACTIONS ]] && echo "# Summary\n\n" >> $GITHUB_STEP_SUMMARY
  printf "%s\n" "$summary" | mlr --itsv --opprint sort -f "Return Code"
  [[ ! -z $GITHUB_ACTIONS ]] && printf "%s\n" "$summary" | mlr --itsv --omd sort -f "Return Code" >> $GITHUB_STEP_SUMMARY

  if [ $failed -eq 0 ]; then
    echo -e "\033[0;32mAll $num_tasks tasks passed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo ":white_check_mark: All $num_tasks tasks succeeded" >> $GITHUB_STEP_SUMMARY
  else
    echo -e "\033[0;31m$failed/$num_tasks tasks failed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo ":x: $failed/$num_tasks tasks failed" >> $GITHUB_STEP_SUMMARY
  fi

  exit 0

