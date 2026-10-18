#pragma once

// Registry of test entry points. Add one declaration per new test file;
// main.cpp calls each in turn.
void run_fake_host_tests();
void run_desktop_tests();
void run_window_frame_tests();
void run_icon_tests();
