#pragma once

// Registry of test entry points. Add one declaration per new test file;
// main.cpp calls each in turn.
void run_fake_host_tests();
void run_desktop_tests();
void run_window_frame_tests();
void run_icon_tests();
void run_pbm_tests();
void run_terminal_tests();
void run_pty_tests();
void run_wire_tests();
void run_remote_canvas_tests();
void run_font_tests();
void run_keymap_tests();
void run_evdev_input_tests();
void run_fbdev_device_tests();
