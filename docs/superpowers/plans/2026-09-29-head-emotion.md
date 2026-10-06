# StackChan Head Emotion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Add bounded AI head positioning, speaking micro-movements, and state-linked mouth animation to the working Xiaozhi device.
**Architecture:** A single board-local worker owns the feedback servo UART. A portable controller enforces limits and command precedence; MCP and display hooks submit commands only. Existing cloud, audio, assets and partition behavior remain intact.
**Tech Stack:** C++/ESP-IDF 6.0.1, Xiaozhi v2.5.0, SCSCL, LVGL, native clang tests.
**Spec:** ../specs/2026-09-29-head-emotion-design.md

## Global Constraints
- Preserve NVS, current xiaozhi.me binding, ILI9342C, Quad PSRAM and partition offsets.
- Default idle and sleepy mouths closed; subtitles hidden.
- Relative micro-motions 2–4 degrees, at least 4 seconds apart, at most two per speaking turn.
- Motion disabled until calibration and feedback have been validated; no EEPROM or servo zero writes.
- Autonomous motion yields to commands; stop holds current position and disables automatic motion.

## Review Focus
- Corrupt/absent feedback must never become an angle or a write target (Task 1/2).
- Near-limit gestures must not cross a hardware limit (Task 1).
- New commands or speech end must cancel stale queued gestures (Task 1/3).
- Reconnect and restart must not force an unverified center position (Task 2/4).
- Neutral during speech must animate while neutral idle and sleepy stay closed (Task 3).

### Task 1: Portable motion policy and tests
Files: firmware/main/boards/m5stack/stackchan-k151/motion_policy.h; firmware/tests/stackchan_motion_test.cc.
Interface: Pose {yaw,pitch}; Policy feedback, target, move/gesture/stop/speaking/step; no IDF dependencies.
- [x] Write assertions for limits, startup disabled, invalid feedback, cancellation, return to base, stop and speech cadence. Run clang test and observe failure before implementation.
- [x] Implement fixed-capacity, bounded policy and rerun host tests with sanitizers.

### Task 2: Board driver and AI tools
Files: stackchan_head.h/.cc in the board directory; m5stack_core_s3.cc; tests for SCS packet codec.
Interfaces: Start(), SetSpeaking(bool), SetEmotion(string), MCP get_position/pose/adjust/gesture/stop/resume; UART worker owns all bus I/O.
- [x] Verify official SCS register format and board pin/power usage; test checksum, word endian and truncated replies before implementation.
- [x] Add worker, current-position feedback, conservative speed/limits, calibration gate, fault handling and MCP tool registration.
- [x] Native policy/codec tests pass; no startup write without explicit enabling.

### Task 3: Speaking display integration
Files: display/lcd_display.h/.cc; display/display.h; application.cc; assets generator helper.
Interfaces: display SetSpeaking(bool); select emotion GIF suffix only while speaking; board hook avoids holding display lock during UART.
- [x] Test emotion-to-asset selection and sleepy special case before implementation.
- [x] Generate idle and speaking assets, register speech/emotion events, preserve hide_subtitle.
- [x] Verify GIF decoding and full asset pack size before device writes.

### Task 4: Build and device validation
- [x] Install only IDF 6.0.1 ESP32-S3 toolchain and build version-pinned CoreS3 configuration.
- [x] Review diff and run native suite plus full firmware build. Resolve important review findings.
- [x] Back up current Flash and validate partition table; write only intended compatible application/assets ranges.
- [ ] Read servo feedback with motion gated. Verify calibration from original backup; ask user only for physical observation/positioning that tools cannot perform.
- [ ] Validate 2-degree probe, command directions, graceful stop, cloud tool discovery, audio and mouth behavior, and record results.

## Execution ledger
- 2026-09-29: Design approved; user explicitly instructed implementation. Native execution in this session; no repeated design/plan permission request.
- New isolated source checkout at firmware/ from v2.5.0; no existing repository or user code to displace. Create feature branch before edits.

- Ruling: upstream AGENTS.md requires a unique board identity; new board m5stack/stackchan-k151 reuses CoreS3 pins without modifying the existing CoreS3 board. Generic display speaking hook is a no-op elsewhere.
- Source sparse checkout initially omitted board fixtures; upstream suite reported 2 failures/22 errors from missing fixtures. Expanding board checkout before rerunning.
- Original factory NVS active calibration values: yaw 461, pitch 610. Startup remains unarmed; operator USB console verifies motion before persisting head_ctl/verified.

- 01:18: IDF 6.0.1 full K151 build passed (2,795,376 bytes); host 81/81 and three native sanitized suites passed. Review findings fixed: local-only arming/approval, PCM-driven speaking, independent-axis stall, torque release after recovery failures. Source commit daef0c6. Current Flash backup in progress before deployment.

- 01:22: Full 16 MiB backup verified; active OTA sequence 1/CRC valid selects ota_0. Partition table identical. Wrote app 0x20000 and assets 0x800000 only, both hashes verified. Read back 0..0x20000 and compared byte-identical (bootloader, partitions, NVS, otadata, PHY). Awaiting physical short RST; head remains gated.

- Device boot: cloud activation, wake and speaking/listening transitions, GIF assets/hide_subtitle verified. Missing K151-specific PY32 VM_EN discovered; added official pin-0 read/modify/readback power initialization. Servo responses confirmed raw yaw=545 (26.25°), pitch=690 (25°), EEPROM limits 20..1003. Initial cold-power range reads need bounded retries. First +2° yaw probe saw no progress and faulted after 1.2s; original feedback-relative .4° command did not accumulate through deadband. Added bounded trajectory (max 2° ahead), tests, diagnostic deadband/current logging. Rebuilt and 81 host/3 native suites passed; awaiting review and physical retest. No head approval persisted.

- Bounded trajectory enabled successful +2° probes: yaw 26.25→27.50°, pitch 25→26.25° (within 1° target tolerance). No head approval persisted. Later transient invalid feedback triggered fault; diagnostic build did not reproduce over 1 minute idle/hold. Slow center attempt stopped at yaw≈15°, pitch≈10° with yaw no-progress (load105/current0), not overload; user confirms no obstruction/noise. Read-only stored-goal diagnostic added. Latest diagnostic flash hash verified, but watchdog reset is looping in ROM/bootloader; requested physical short RST. Resolve boot state before further work.

## Approved implementation correction: reuse factory motion chain
- User explicitly requested using the original open-source App/firmware movement implementation. Replace custom packet transport and feedback-relative slew with pinned factory FTServo driver and original Servo class + smooth_ui_toolkit v2.12.0. Keep current xiaozhi.me integration, policy limits, stop/fault gate, unit calibration and subtitles/face rules.
- BLE motion data passes json_helper.cpp -> moveWithSpeed / moveWithSpringParams -> Servo.update at 50 Hz -> hal_servo.cpp WritePos(Time=20). Original MCP uses speed150 and negative yaw as robot own left.
- Ruling: adapt only Servo include paths/clock to board boundary; disable factory automatic torque release because user-selected poses should hold. Never call factory EEPROM/PWM/zero-calibration APIs; retain validated feedback and local approval gate. New test uses quantized feedback/deadband and asserts exact final target snap and stop cancellation.

- Factory-chain port committed a718f77. Full ESP-IDF build passed, 81 upstream tests and 4 native sanitized suites passed. Fresh review found no blockers. Application-only flash hash verified; before/after 0..0x20000 protected ranges byte-identical. Physical RST requested; automatic USB reset previously caused ROM loops that cleared on physical RST, so avoided it for this deployment.

- Factory controller physically returned center (user confirmed), feedback settled yaw=1.25°, pitch=10.31°, load75/current0. Our 1° no-progress guard falsely faulted and approval remained false, causing AI local-verification response. Added hardware-value regression (RED), adopted factory 8-tick/2.5° stall exclusion (GREEN); retained far-stall/overload guards and animation final snap. Approved-but-faulted responses now distinguish fault from missing local verification. Four native sanitized suites + incremental IDF build passed. Source c210c2e; application-only xiaozhi-factory-settle.bin flashed/hash verified. Waiting physical RST before local arm/center/approve.

- After physical reboot, feedback was valid but approved=false. A transient read timeout during cloud connection had faulted the unarmed policy. Local arm cleared the fault; center remained completed without fault for >10 s. Sent head approve: runtime confirms approved=true, armed=true, automatic=true, fault=false; head_ctl/verified persisted by handler. Requested AI voice up/center test. Actual cloud tool dispatch and persistence across a later reboot remain to verify.

- User confirms voice commands moved head. Logs show AI resume action5 at165376ms, AI up action0 pitch20 at169876ms (actual18.75°, completed), AI center action0 pitch10 at177216ms (actual10.31°, completed), later AI shake action3. Current approved/armed=true, fault=false. A single UART timeout at155036ms during cloud connection caused a fault; AI resume recovered before successful motions. Do not claim long-duration UART stability or visible shake/micro-motion amplitude verified; 3-second logging cannot resolve small gestures. Saved boot-ai-pose-verified.log.

- User reports only up/center visibly work. AI nod/shake accepted in logs, but 3° waypoints reversed after 300–400ms, too short for factory speed150 spring. Added combined Policy+FactoryAxis test: RED old nod excursion1.5625°; fix explicit gestures6° and phase1400ms (micro remains3°, phase1000ms); GREEN nod5.625°, shake±5.625°, return-to-base verified in simulation. Four native sanitized suites and IDF build passed. Added local nod/shake/left/right diagnostics and 200ms moving feedback logging for hardware test. Commit1d04e72; app-only xiaozhi-factory-gesture.bin flash/hash verified. Waiting short physical RST, then test individual gestures and static yaw presets; do not claim physical fix yet.

- User requests App-like speed, larger visible and repeated gestures; reports slow/stiff one-cycle motion. Retrieved pinned official App source: app/lib/model/expression_data.dart MotionDataItem constructor and JSON fallback both speed500 (lines113/121), motion.dart speed slider0..1000. Prior speed150 was official MCP default, not App default. Now FactoryAxis uses500; explicit nod8° then6° (two excursions), shake±12° then±10° (two cycles), 450ms waypoints, clamp existing bounds; automatic gestures remain3°. Worker uses deadline-based20ms cadence with no catch-up burst after >40ms delay. RED combined test failed old 500ms settling; GREEN 4 sanitized suites incl two visible excursions/return within3s, simulated nod7.5°, shake-11.25..11.56°. IDF build passed, commit291e26d. App-only deployment in progress; physical smoothness and cycle visibility still require verification.

- App-speed version booted with approved=true (persistence verified). Local nod interrupted at26606ms by one yaw UART read timeout while audio state was speaking; pitch moved24.69→30.94° before fault/torque release. Confirms an additional abrupt-stop cause independent of trajectory amplitude. Added exactly one fresh read retry for transport failure, no retry if servo alarm, no cached feedback; continuous failure still faults. Native failure/recovery/alarm cases + four suites pass; IDF build pass. Commitbd30deb, deploying xiaozhi-appspeed-retry.bin; still awaiting physical completion of both gesture cycles.

- User requests larger/faster gestures before restarting retry build. Adjusted speed500→650 (explicitly a user preference, App default remains500), nod12°/10° twice, shake±18°/±15° twice, phase400ms; micro stays3°/450ms. Existing yaw±30/pitch5..60 bounds unchanged and near upper pitch nod inward. RED old amplitude failed; GREEN 4 native suites, simulated nod11.5625°, shake±17.5°, return and two excursions pass. IDF build passed; commite270a01. Deploying xiaozhi-expressive.bin app-only from bootloader; physical test pending.

- Expressive build flashed/hash verified and physically reset. Persisted approval true. Local stop suspended automatic gestures, center succeeded. Nod at28616ms feedback pitch9.69→20.62→10.31→17.50→10.31 (two excursions, fault false). Shake at44186ms feedback crossed both directions twice and returned yaw-1.56° within tolerance, fault false. AI left at48736ms moved yaw-1.56→-8.44 for target-10 and completed. Restored automatic mode via local approve (existing flag retained). Saved boot-expressive-verified.log. Physical feedback/cycles verified; perceived smoothness remains user judgment, long-duration UART reliability not established.

## Approved idle glance and head pet addition
- User approved bounded design: idle random looks every4–8s, pause on conversation/stop; top pet loving+raise, restore3s after release, new commands win. Read pinned original idle_motion.h, head_pet.h and Si12T driver/hal. Implemented board-only: ConfigureHeadTouch mirrors factory0x68/LOW LEVEL3 with readback, polling50ms in separate task, two-sample debounce and300ms stale release; policy owns transient pet/idle targets, display override scheduled on application loop preserving newest emotion. Explicit650 profile retained, idle400. Repeated-stroke regression RED then GREEN, no upward ratchet. Five native sanitized suites pass; initial/full incremental builds pass; review and final incremental build in progress before app-only deployment.

- Commit362f35e: final IDF build and five native sanitized suites pass, independent review found no blockers. App-only xiaozhi-idle-pet.bin flashed and hash verified. Monitor /tmp/stackchan-idle-pet-monitor.log active; requested physical short RST, idle10s then top pet/release. No hardware success claimed yet.

- Physical boot: Si12T configured/readback verified at1146ms. Multiple real touches detected; initial pet6516ms pitch18.44→35.62 (target36.44), release9796ms→restore12806ms, actual18.44 by13456ms. Repeated strokes held target without ratchet. Idle targets14/24 then16/21 generated and physically followed; idle exited28356ms for wake/listening. Later pet works during speech; transient UART misses recovered via retry, fault remains false. Saved boot-idle-pet-verified.log. User visual loving-GIF confirmation still pending; no claim of visually verified face.

- User requests softer pet lift followed by gentle repeating nods until3s after release. Commitf5075f2: pet speed350, initial raised center held1200ms then alternating±3° every700ms, continuous through release grace; soft return speed350, manual commands cancel and retain650. Repeated touches preserve center and phase. RED fixed-pose regression then GREEN five sanitized suites; real FactoryAxis simulation pitch25..30.625 around28 and restoration10 verifies movement and grace timing. IDF build passed; app-only xiaozhi-pet-nod.bin deployment in progress, physical verification pending.

- Pet-nod physical reboot observed pitch4.6875° (raw625 vszero610) following a minimum-pitch position. Strict command-range validation was incorrectly reused for actual feedback, making approved head fault permanently and AI report stuck. Added FeedbackSafe using existing2.5° factory settling tolerance, retained strict command bounds and clamped adopted/restored poses in Arm/Stop/idle-exit/pet. RED actual-value boundary test reproduced failure; GREEN five suites including below-limit command rejection and truly out-of-band pitch2° fault. Final build/review pending before deployment.

- Commit8f14386 boundary fix: five suites/build/review pass. App-only xiaozhi-pet-boundary.bin flash hash verified; monitor /tmp/stackchan-pet-boundary-monitor.log. Waiting shortRST to verify boot at4.69° now arms and pet nods work.

- Boundary fix physically confirmed boot at4.69° armed/valid and idle moved normally. Pet at23096ms lifted, but24256ms read ID1 CRC mismatch recovered and subsequent ID2 position ACK length error caused immediate fault; pet ended and no restore, matching user. Original factory hal_servo ignores WritePos return; current strict driver latched on first write ACK failure. Added WithBusRecovery: idempotent feedback/position/torque transactions retry once after20ms quiet+RX flush, no retry on servo alarm, repeated failure still faults. RED missing-helper test then GREEN five suites; build/review in progress. This is bounded resynchronization, not proof of physical bus fault elimination.

- Commitc913b28 bus recovery: review identified retained SCS u8Status on no-header; fixed predicates to require transporterror0 plus nonzeroalarm, with test. Five suites and IDF build pass. App-only xiaozhi-bus-recovery.bin flashing. Need physical multiple pet cycles and post-pet command; do not claim resolved from unit tests.

- c913b28 still fails: from1166ms startup, ID/CRC/length/no-reply errors; before any pet, fault2836ms. Retry did not resolve transport instability. PM disabled, UART6/7 not shared with camera/audio/display pins. Source inspection: factory writeBuf sends header/payload/checksum via separateuart_write_bytes; SCSerial receive reads onebyte+vTaskDelay(pdMS_TO_TICKS(1)) which is0 at100Hz. Hypothesis scheduling gaps/receive polling under current task load. Added BufferedServoBus adapter: stage bounded64-byte request untilwFlush, one UART write; blocking buffered20ms RX, original SCS codec retained. Five suites andbuild pass. Review/hardware confirmation pending; not claiming root cause proven.

- a194f2a buffered transport: review no issues, app-only flash hash verified. Monitor /tmp/stackchan-buffered-monitor.log active, awaiting physical RST first without touch. Keep unresolved until actual stable boot+pet cycles+post-pet motion.

- a194f2a physical boot still fails ID/no-reply from1046ms, before pet. AI resume at20346ms briefly arms, fails feedback again20656ms. Buffered transport hypothesis not supported as sufficient fix. Stop further code/flashing attempts; requested full base OFF+USB unplug10s thenON/reconnect to reset servo/PY32 supply state (RST only MCU). Saved boot-buffered-still-fault.log, explicitly unresolved. Next inspect whether full power cycle restores valid/stable feedback before any motion.

- User now confirms lift+gentle nods works. No no-nod comparison edits/flashes occurred (clean tree); keep a194f2a and pet nods. Logs confirm repeated pitch25.31..29.38 around raised center, restore10.31 and idle glances. Later172126ms UART invalid yaw caused fault during speech; do not claim reliability fixed or blame nods exclusively. Saved boot-pet-nods-observed.log and marked observation vs remaining fault separately.
