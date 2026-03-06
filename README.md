# Surface Level Project with Waveshare ESP32-S3 1.43inch AMOLED Touch Display
<a href="https://www.buymeacoffee.com/thelastoutpostworkshop" target="_blank">
<img src="https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png" alt="Buy Me A Coffee">
</a>

## Youtube Tutorial
[<img src="https://github.com/thelastoutpostworkshop/images/blob/main/Surface%20Level-1.png" width="500">](https://youtu.be/HT6sG39-vrk)

## Project Description
This project is a ready-to-run digital surface level example for the Waveshare ESP32-S3 1.43 inch AMOLED touch display using LVGL 9. It combines the onboard AMOLED screen, capacitive touch controller, and QMI8658 6-axis IMU to create a responsive level interface with a moving bubble, angle readouts, and a target indicator when the device is close to level.

The repository also includes the generated SquareLine Studio UI source, making it a useful starting point both for flashing the included demo and for building your own touch-based LVGL interface on this board.

## Features
- LVGL 9 based project structure configured for the Waveshare ESP32-S3 Touch AMOLED 1.43 board
- Built-in surface level demo that reads accelerometer data from the QMI8658 IMU
- Real-time bubble movement based on board orientation
- On-screen angle labels for the current level position
- Visual target indicator that changes state when the board is close to level
- Capacitive touch support integrated with LVGL input handling
- Included SquareLine Studio project and generated UI source files for further editing
- Display driver setup, LVGL buffer allocation, and rendering callbacks already wired up

## What You Can Customize
- Replace the included `ui_init()` interface with your own SquareLine Studio or hand-built LVGL UI
- Change the bubble behavior by tuning smoothing, movement scaling, and update timing in the sketch
- Adjust the level detection threshold with `TARGET_THRESHOLD_PX`
- Change IMU sampling and UI refresh timing with `READ_SAMPLE_INTERVAL_MS` and `MOVE_BUBBLE_INTERVAL_MS`
- Swap or invert axes to match your preferred board orientation using `swap_axes`, `inv_pitch`, and `inv_roll`
- Modify labels, graphics, and screens using the included SquareLine Studio project in `Squareline_studio_project`
- Use the project as a hardware/software base for other AMOLED touch applications on the same board


