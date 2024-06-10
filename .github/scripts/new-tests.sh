jobs=(
  "test-clap-validator"
  "test-units"
  "test-pluginval"
)

# parallel's json outputs a json object on each line without a comma at the end. So we have to do some work to make
# it a valid json array.
json="[ $(parallel --results -.json ::: "${jobs[@]}" | sed 's/$/,/' | head -c -2) ]"

echo $json

jq -r '.[] | select(.Exitval != 0) | .Stdout, .Stderr' <(echo $json)

echo "========= Summary =========="

echo $json | jq -r '["Command", "Time", "Result"], (.[] | [.Command, .JobRuntime, .Exitval]) | @tsv' | column -t -s $'\t'
