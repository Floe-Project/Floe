# Copyright 2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

test_level=$1

if ! [[ $test_level =~ ^[0-2]$ ]]; then
  echo "Invalid test level. Must be an integer between 0 and 2."
  exit 1
fi

binary_dir="zig-out/x86-linux"
if [ $(uname -s) == "Darwin" ]; then
  binary_dir="zig-out/x86-macos"
fi

# level 0: fast tests
jobs=(
  "check-format-all"
  "reuse lint"
  "clap-validator validate --in-process $binary_dir/Floe.clap"
  "$binary_dir/tests"
  "pluginval $binary_dir/Floe.vst3"
  "timeout 2 $binary_dir/VST3-Validator $binary_dir/Floe.vst3"
)

if [ $(uname -s) != "Darwin" ] && [ -z $GITHUB_ACTIONS ]; then
  jobs+=(
    "wine zig-out/x86-windows/tests.exe"
  )
fi

# level 1: slow tests, only run if level 1 or higher
if [ $test_level -ge 1 ]; then
  jobs+=(
    "sh build_gen/clang-tidy-cmd.sh"
    "cppcheck --project=$PWD/build_gen/compile_commands.json --cppcheck-build-dir=$PWD/build_gen --enable=unusedFunction
"
  )
fi

# parallel's json result is not valid json. It's a json object on each line without commas at the end. So we have to do some work to make it a valid array.
json="[ $(parallel --bar --results -.json ::: "${jobs[@]}" | sed 's/$/,/' | head -c -2) ]"

# Print stdout and stderr of failed jobs 
jq -r '.[] | select(.Exitval != 0) | "\n\u001b[34m[Stdout] \(.Command):\u001b[0m", .Stdout, "\n\u001b[34m[Stderr] \(.Command):\u001b[0m", .Stderr' <(echo $json)

echo -e "\033[0;34m[Summary]\033[0m"

summary=$(echo $json | jq -r '["Command", "Time", "Return Code"], (.[] | [.Command, .JobRuntime, .Exitval]) | @tsv')

printf "%s\n" "$summary" | mlr --itsv --opprint sort -f "Return Code"

failed=$(echo $json | jq '. | map(select(.Exitval != 0)) | length')
num_jobs=${#jobs[@]}

if [ ! -z $GITHUB_ACTIONS ]; then
  echo "# Test Summary\n\n" >> $GITHUB_STEP_SUMMARY
  printf "%s\n" "$summary" | mlr --itsv --omd sort -f "Return Code" >> $GITHUB_STEP_SUMMARY
  if [ $failed -eq 0 ]; then
    echo ":white_check_mark: All $num_jobs tests passed" >> $GITHUB_STEP_SUMMARY
  else
    echo ":x: $failed/$num_jobs tests failed" >> $GITHUB_STEP_SUMMARY
  fi
fi

if [ $failed -eq 0 ]; then
  echo -e "\033[0;32mAll $num_jobs jobs passed\033[0m"
else
  echo -e "\033[0;31m$failed/$num_jobs jobs failed\033[0m"
  exit 1
fi
