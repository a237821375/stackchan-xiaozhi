#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
out_dir=$(mktemp -d /tmp/stackchan-tests.XXXXXX)
trap 'rm -f "$out_dir/motion_result" "$out_dir/motion" "$out_dir/protocol" "$out_dir/speaking" "$out_dir/factory" "$out_dir/interaction" "$out_dir/recovery" "$out_dir/dance" "$out_dir/dance_rgb" "$out_dir/servo_diagnostics" "$out_dir/calibration"; rmdir "$out_dir"' EXIT
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/motion_result_test.cc -o "$out_dir/motion_result"
"$out_dir/motion_result"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/head_calibration_test.cc -o "$out_dir/calibration"
"$out_dir/calibration"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/servo_diagnostics_test.cc -o "$out_dir/servo_diagnostics"
"$out_dir/servo_diagnostics"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/stackchan_dance_test.cc -o "$out_dir/dance"
"$out_dir/dance"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/stackchan_dance_rgb_test.cc -o "$out_dir/dance_rgb"
"$out_dir/dance_rgb"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/feedback_recovery_test.cc -o "$out_dir/recovery"
"$out_dir/recovery"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/stackchan_motion_test.cc -o "$out_dir/motion"
"$out_dir/motion"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/stackchan_interaction_test.cc -o "$out_dir/interaction"
"$out_dir/interaction"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/stackchan_protocol_test.cc \
    main/boards/m5stack/stackchan-k151/factory_upstream/ftservo/SCS.cpp -o "$out_dir/protocol"
"$out_dir/protocol"
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/speaking_gate_test.cc -o "$out_dir/speaking"
"$out_dir/speaking"
factory_dir=main/boards/m5stack/stackchan-k151/factory_upstream/smooth_ui_toolkit/src
factory_sources=$(rg --files "$factory_dir" -g '*.cpp')
clang++ -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-lambda-capture -fsanitize=address,undefined \
    -DSMOOTH_UI_TOOLKIT_ENABLE_DEFAULT_HAL=0 -I"$factory_dir" \
    tests/factory_motion_test.cc main/boards/m5stack/stackchan-k151/factory_servo.cc \
    $factory_sources -o "$out_dir/factory"
"$out_dir/factory"
