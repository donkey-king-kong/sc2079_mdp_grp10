# Product

<!-- impeccable:product-schema 1 -->

## Platform

android

## Stack

The existing codebase is a multi-part robotics system:

- Android operator app in Kotlin and Java, built with Gradle Kotlin DSL, AndroidX, Material Components, ConstraintLayout, ViewBinding, Gson, and related UI/support libraries.
- Raspberry Pi controller in Python, responsible for Bluetooth communication with Android, HTTP communication with the algorithm service, UART communication with STM32, and image-capture orchestration.
- Algorithm service and simulator in Python Flask, including a browser-based simulator for arena setup, route planning, playback, and strategy comparison.
- STM32F407VET6 firmware using STM32 HAL and FreeRTOS/CMSIS-RTOS2 for movement execution, sensors, OLED telemetry, and command acknowledgements.

## Users

Primary users are mixed:

- MDP team operators who use the Android tablet to configure the arena, monitor robot state, send commands, and run task flows.
- Evaluators or instructors who assess whether the robot completes the SC2079 MDP task reliably.
- Developers who maintain and debug the Android, Raspberry Pi, algorithm, imaging, and STM32 parts of the system.

## Product Purpose

The product supports the SC2079 Multidisciplinary Design Project robot task. It enables a robot to operate in a 20x20 arena, navigate around obstacles, visit target-facing positions, capture images, run image-recognition workflows, and report target results back through the operator interface.

Success means the system can coordinate arena setup, route planning, physical movement, image capture, recognition, and final result reporting across the full robot stack with enough reliability for MDP task execution and evaluation.

## Positioning

The durable differentiator is the integrated robot stack: Android, Raspberry Pi, algorithm service, image-recognition flow, and STM32 firmware work together as one task-execution pipeline rather than as isolated demos.

## Operating Context

The product is used in an MDP robotics environment with a physical robot, Android tablet, Raspberry Pi, STM32 microcontroller, camera, Bluetooth link, HTTP algorithm service, UART command channel, and a 20x20 arena with obstacles and target faces.

Core workflows include:

- Configure the arena and obstacle target directions from the Android app.
- Send arena data from Android to Raspberry Pi over Bluetooth.
- Convert the arena into the algorithm-service format and request movement plans.
- Execute movement commands through STM32 and acknowledge progress.
- Trigger camera snapshots at planned positions.
- Run image-recognition and target-tracking logic.
- Send target results and stitched images back to Android.
- Use the Flask simulator to test arena layouts, planned paths, playback, and movement strategies.

## Capabilities and Constraints

Binding constraints:

- Preserve the existing stack boundaries: Android app, Raspberry Pi Python controller, Flask algorithm service, imaging pipeline, and STM32 firmware.
- Preserve the MDP protocol assumptions: 20x20 arena/grid model, obstacle and target-direction inputs, command flow, and image-target reporting.
- Preserve the communication architecture: Android to Raspberry Pi over Bluetooth, Raspberry Pi to Algo over HTTP, and Raspberry Pi to STM32 over UART.
- Preserve task command semantics such as movement commands, snapshot triggers, finish signals, STM acknowledgements, and target-result messages unless the protocol is intentionally revised.

Known capabilities:

- Android arena visualization, robot status, Bluetooth status, grid controls, obstacle placement, manual controls, save/load, and task messaging.
- Algorithmic route generation using obstacle pose generation, Dubins paths, Hybrid A* fallback, ordering strategies, and simulator playback.
- Raspberry Pi orchestration for command routing, image capture, inference integration, target tracking, and image stitching.
- STM32 movement execution, sensor integration, calibration commands, emergency stop handling, and OLED telemetry.

Open product decisions:

- Whether future UI work should prioritize the Android operator app, the browser simulator, or both as first-class product surfaces.
- The required accessibility standard for the Android app or simulator has not been explicitly confirmed.
- Deployment, demo, and evaluation requirements beyond the repository documentation are not yet confirmed.

## Brand Commitments

The project name and course context are binding product facts: SC2079 MDP Group 10 / Multidisciplinary Design Project robot system.

Existing UI assets, icons, Poppins font resources, Android themes, and repository terminology should be treated as incumbent product evidence, not as a confirmed long-term visual identity.

## Evidence on Hand

Repository evidence includes:

- Root system overview, message contracts, setup notes, and component descriptions in `README.md`.
- Algorithm planning, simulator, API, tests, and known gaps in `algo/README.md`.
- Raspberry Pi controller architecture, setup, runtime behavior, and integration status in `rpi_controller/README.md`.
- Imaging behavior and model expectations in `rpi_controller/imaging/README.md`.
- Android app implementation under `android/app/src/main`.
- Algorithm Flask service and simulator under `algo`.
- Raspberry Pi controller and imaging pipeline under `rpi_controller`.
- STM32 firmware under `STM/Task1`.

No testimonials, production benchmarks, customer claims, pricing, licensing claims, or external proof assets are confirmed in the repository and should not be fabricated in future work.

## Product Principles

- Treat the system as an integrated robot pipeline; changes to one layer should respect the downstream command, movement, image, and reporting flow.
- Prioritize operator clarity under live robotics conditions: current robot state, connection state, task progress, and recoverability should be easy to understand.
- Preserve protocol compatibility unless a change intentionally updates all affected Android, Raspberry Pi, Algo, and STM32 components.
- Support evaluation readiness by keeping task flows demonstrable, repeatable, and grounded in real MDP constraints.
- Keep simulator and physical-robot behavior aligned so test layouts and planned paths remain useful before hardware runs.
