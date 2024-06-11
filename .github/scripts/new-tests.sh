# Copyright 2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

jobs=(
  "check-format-all"
  "reuse lint"
  "clap-validator validate --in-process zig-out/x86-linux/Floe.clap"
  "zig-out/x86-linux/tests"
  "pluginval zig-out/x86-linux/Floe.vst3"
  "timeout 1 zig-out/x86-linux/VST3-Validator zig-out/x86-linux/Floe.vst3"
)

# parallel's json result is not valid json. It's a json object on each line without commas at the end. So we have to do some work to make it a valid array.
json="[ $(parallel --bar --results -.json ::: "${jobs[@]}" | sed 's/$/,/' | head -c -2) ]"

# Print stdout and stderr of failed jobs 
jq -r '.[] | select(.Exitval != 0) | "\n\u001b[34m[Stdout] \(.Command):\u001b[0m", .Stdout, "\n\u001b[34m[Stderr] \(.Command):\u001b[0m", .Stderr' <(echo $json)

echo -e "\033[0;34m[Summary]\033[0m"

echo $json | jq -r '["Command", "Time", "Return"], (.[] | [.Command, .JobRuntime, .Exitval]) | @tsv' | column -t -s $'\t'

failed=$(echo $json | jq '. | map(select(.Exitval != 0)) | length')
num_jobs=${#jobs[@]}
if [ $failed -eq 0 ]; then
  echo -e "\033[0;32mAll $num_jobs jobs passed\033[0m"
else
  # print a red failed message using ansi escape codes
  echo -e "\033[0;31m$failed out of $num_jobs jobs failed\033[0m"
  exit 1
fi
