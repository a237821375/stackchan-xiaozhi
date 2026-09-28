#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
out_dir=$(mktemp -d /tmp/stackchan-tests.XXXXXX)
trap 'rm -f "$out_dir/motion" "$out_dir/protocol" "$out_dir/speaking"; rmdir "$out_dir"' EXIT
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/stackchan_motion_test.cc -o "$out_dir/motion"
"$out_dir/motion"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/stackchan_protocol_test.cc -o "$out_dir/protocol"
"$out_dir/protocol"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/speaking_gate_test.cc -o "$out_dir/speaking"
"$out_dir/speaking"
