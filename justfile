# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

# This file assumes 'nix develop' has already been run

native_arch_os_pair := arch() + "-" + os()
native_binary_dir := "zig-out/" + native_arch_os_pair
all_src_files := 'fd . -e .mm -e .cpp -e .hpp -e .h src' 

build target_os='native':
  time analyzed-build zig build compile -Dtargets={{target_os}} -Dbuild-mode=development 

check-reuse:
  reuse lint

check-format:
  {{all_src_files}} | xargs clang-format --dry-run --Werror

clang-tidy arch_os_pair=native_arch_os_pair:
  jq -r '.[].file' build_gen/compile_commands_{{arch_os_pair}}.json | xargs clang-tidy -p build_gen 

cppcheck arch_os_pair=native_arch_os_pair:
  # IMPROVE: use --check-level=exhaustive?
  cppcheck --project={{justfile_directory()}}/build_gen/compile_commands_{{arch_os_pair}}.json --cppcheck-build-dir={{justfile_directory()}}/.zig-cache --enable=unusedFunction --error-exitcode=2

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
  wine zig-out/x86_64-windows/VST3-Validator.exe zig-out/x86_64-windows/Floe.vst3

test-wine-pluginval:
  wine $PLUGINVAL_WINDOWS_PATH zig-out/x86_64-windows/Floe.vst3

test-wine-units:
  wine zig-out/x86_64-windows/tests.exe

test-wine-clap-val:
  wine $CLAPVAL_WINDOWS_PATH validate zig-out/x86_64-windows/Floe.clap

[linux]
coverage:
  mkdir -p build_gen
  # TODO: run other tests with coverage and --merge the results
  kcov --include-pattern={{justfile_directory()}}/src build_gen/coverage-out {{native_binary_dir}}/tests

# Local linux
quick_checks_linux := replace("""
  check-reuse 
  check-format
  test-units
  test-clap-val
  test-pluginval
  test-vst3-val
  test-wine-vst3-val
  test-wine-pluginval
  test-wine-clap-val
  test-wine-units
""", "\n", " ")

# NOTE: we use different checks for linux CI at the moment because of a couple of things we have yet to solve:
# 1. We haven't set up wine on CI yet
# 2. Linux plugin tests (pluginval, vst3-val, etc) do not work. Something out running the plugin always crashes
quick_checks_linux_ci := replace("""
  check-reuse 
  check-format
  test-units
""", "\n", " ")

quick_checks_non_linux := replace("""
  check-reuse 
  check-format
  test-units
  test-clap-val
  test-pluginval
  test-vst3-val
""", "\n", " ")

static_analyisers := replace("""
  clang-tidy
  cppcheck
""", "\n", " ")

checks_local_level_0 := if os() == "linux" { quick_checks_linux } else { quick_checks_non_linux }
checks_local_level_1 := checks_local_level_0 + static_analyisers 

checks_ci := static_analyisers + if os() == "linux" { quick_checks_linux_ci + " coverage" } else { quick_checks_non_linux } 

test level: (parallel if level == "0" { checks_local_level_0 } else { checks_local_level_1 } )

test-ci: (parallel checks_ci)

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

