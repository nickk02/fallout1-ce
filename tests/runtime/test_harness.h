#ifndef FALLOUT_TESTS_RUNTIME_TEST_HARNESS_H_
#define FALLOUT_TESTS_RUNTIME_TEST_HARNESS_H_

#include <stdio.h>

namespace fallout_test {

typedef void (*TestFunction)();

struct TestCase {
    const char* name;
    TestFunction function;
};

// Registers a test at static initialization time. Returns true so it can
// initialize a namespace-scope variable.
bool register_test(const char* name, TestFunction function);

// Called by CHECK on failure. Counts the failure for the running test.
void report_failure(const char* file, int line, const char* expression);

} // namespace fallout_test

#define TEST(name)                                                              \
    static void name();                                                         \
    static const bool name##_registered = fallout_test::register_test(#name, name); \
    static void name()

#define CHECK(expression)                                                 \
    do {                                                                  \
        if (!(expression)) {                                              \
            fallout_test::report_failure(__FILE__, __LINE__, #expression); \
        }                                                                 \
    } while (0)

#define CHECK_EQ(actual, expected) CHECK((actual) == (expected))

#endif /* FALLOUT_TESTS_RUNTIME_TEST_HARNESS_H_ */
