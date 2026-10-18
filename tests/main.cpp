#include "cases.hpp"
#include "testing.hpp"

int main() {
    run_fake_host_tests();
    run_desktop_tests();
    return vesper::testing::failure_count() == 0 ? 0 : 1;
}
