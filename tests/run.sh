#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
cc -std=c11 -Wall -Wextra -Werror -pedantic \
    -I "$repo_dir/main" "$repo_dir/tests/test_button_debounce.c" \
    "$repo_dir/main/button_debounce.c" -o "$test_dir/test_button_debounce"
"$test_dir/test_button_debounce"
idf_dir=${IDF_PATH:-/workspace/tools/esp-idf}
cjson_dir="$idf_dir/components/json/cJSON"
if [[ ! -f "$cjson_dir/cJSON.c" ]]; then
    echo "Set IDF_PATH to an ESP-IDF checkout with its cJSON submodule initialized." >&2
    exit 1
fi
cc -std=c11 -Wall -Wextra -Werror -pedantic \
    -I "$repo_dir/main" -I "$cjson_dir" \
    "$repo_dir/tests/test_ha_discovery.c" "$repo_dir/main/ha_discovery.c" \
    "$cjson_dir/cJSON.c" -lm -o "$test_dir/test_ha_discovery"
"$test_dir/test_ha_discovery"
