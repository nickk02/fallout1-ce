#include "test_harness.h"

#include <string.h>

#include <vector>

namespace fallout_test {

static std::vector<TestCase>& test_cases()
{
    static std::vector<TestCase> cases;
    return cases;
}

static int currentFailures = 0;

bool register_test(const char* name, TestFunction function)
{
    test_cases().push_back({ name, function });
    return true;
}

void report_failure(const char* file, int line, const char* expression)
{
    currentFailures++;
    fprintf(stderr, "  %s:%d: CHECK failed: %s\n", file, line, expression);
}

} // namespace fallout_test

// Runs every registered test, or only those whose name contains argv[1].
int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;

    int run = 0;
    int failed = 0;
    for (const fallout_test::TestCase& testCase : fallout_test::test_cases()) {
        if (filter != nullptr && strstr(testCase.name, filter) == nullptr) {
            continue;
        }

        fallout_test::currentFailures = 0;
        testCase.function();
        run++;

        if (fallout_test::currentFailures != 0) {
            failed++;
            printf("FAIL %s\n", testCase.name);
        } else {
            printf("ok   %s\n", testCase.name);
        }
    }

    printf("%d run, %d failed\n", run, failed);

    // A filter that matches nothing is an error, not a pass.
    return (run == 0 || failed != 0) ? 1 : 0;
}
