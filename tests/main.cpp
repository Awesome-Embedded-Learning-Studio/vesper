#include "cases.hpp"
#include "testing.hpp"

int main() {
    run_fake_host_tests();
    run_desktop_tests();
    run_window_frame_tests();
    run_icon_tests();
    run_pbm_tests();
    run_terminal_tests();
    return vesper::testing::failure_count() == 0 ? 0 : 1;
}
