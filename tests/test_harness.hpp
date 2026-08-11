#pragma once

#include <iostream>
#include <source_location>
#include <string_view>
#include <vector>

namespace ember::test {

class TestContext {
public:
    void beginTest(std::string_view name) noexcept {
        currentTest_ = name;
    }

    void expect(bool condition,
                std::string_view expression,
                std::source_location location = std::source_location::current()) {
        if (condition) {
            return;
        }

        ++failures_;
        std::cerr << location.file_name() << ':' << location.line() << ": [" << currentTest_
                  << "] expectation failed: " << expression << '\n';
    }

    [[nodiscard]] int exitCode() const noexcept {
        return failures_ == 0 ? 0 : 1;
    }

private:
    int failures_{};
    std::string_view currentTest_;
};

using TestFunction = void (*)(TestContext&);

struct TestCase {
    std::string_view name;
    TestFunction function;
};

class TestRegistry {
public:
    [[nodiscard]] static TestRegistry& instance() {
        static TestRegistry registry;
        return registry;
    }

    void add(TestCase testCase) {
        testCases_.push_back(testCase);
    }

    [[nodiscard]] int run() const {
        TestContext tests;
        for (const auto& testCase : testCases_) {
            tests.beginTest(testCase.name);
            testCase.function(tests);
        }
        return tests.exitCode();
    }

private:
    std::vector<TestCase> testCases_;
};

class TestRegistration {
public:
    TestRegistration(std::string_view name, TestFunction function) {
        TestRegistry::instance().add(TestCase{.name = name, .function = function});
    }
};

} // namespace ember::test

#define EMBER_TEST_JOIN_IMPL(left, right) left##right
#define EMBER_TEST_JOIN(left, right) EMBER_TEST_JOIN_IMPL(left, right)
#define EMBER_TEST(name) EMBER_TEST_IMPL(name, __LINE__)
#define EMBER_TEST_IMPL(name, line)                                                                \
    static void EMBER_TEST_JOIN(emberTest_, line)(::ember::test::TestContext & tests);             \
    [[maybe_unused]] static const ::ember::test::TestRegistration EMBER_TEST_JOIN(                 \
        emberRegistration_,                                                                        \
        line){name, EMBER_TEST_JOIN(emberTest_, line)};                                            \
    static void EMBER_TEST_JOIN(emberTest_, line)(::ember::test::TestContext & tests)
