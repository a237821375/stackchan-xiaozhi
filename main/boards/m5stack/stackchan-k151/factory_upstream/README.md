# Original StackChan motion dependencies

Source: https://github.com/m5stack/StackChan/tree/1b5765599fba8aaad1811d9a79358ccc7051f5f3/firmware

- `ftservo/`: exact original FTServo_Arduino SCS / SCSCL / SCSerial sources; MIT license included.
- `smooth_ui_toolkit/`: unmodified animation dependencies from v2.12.0, the exact version pinned by factory firmware/repos.json; MIT license included.
- Adjacent `factory_servo.h/.cc` derives directly from firmware/main/stackchan/motion/servo.h/.cpp. Only include paths and the millisecond-clock bridge are adapted. The original 50 Hz spring progression, speed mapping and final target snap are retained.
- `factory_axis.h` connects that class to validated feedback, unit-specific calibration and the original `WritePos(id,raw,20,0)` operation. Automatic torque release is disabled so explicit poses hold; the worker owns arming, torque, limits and fault handling.
- No firmware calls to EEPROM, PWM mode, reset or zero-calibration APIs.

`provenance.json` records source identities and SHA-256 for copied files. Do not modify vendored sources to suppress warnings; the host compile ignores upstream-only unused lambda capture warnings.
