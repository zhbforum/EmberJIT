#include "test_harness.hpp"

int main() {
    return ember::test::TestRegistry::instance().run();
}
