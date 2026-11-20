#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "albor/core/env.h"

TEST(CoreEnv, ReturnsNoValueForAnUnsetVariable) {
    // arrange a name nothing in the process has ever set, which the environment cannot answer
    const char* name = "ALBOR_CORE_ENV_UNSET_SENTINEL_8F3A2C";
    // act
    const std::optional<std::string> value = albor::core::get_environment_variable(name);
    // assert the absent variable carries no value at all
    EXPECT_FALSE(value.has_value());
}

TEST(CoreEnv, ReturnsTheValueOfASetVariable) {
    // arrange a variable carrying a known value, which the wrapper must return verbatim
    const char* name = "ALBOR_CORE_ENV_TEST_VALUE";
#if defined(_WIN32)
    _putenv_s(name, "albor-env-fixture");
#else
    setenv(name, "albor-env-fixture", 1);
#endif
    // act
    const std::optional<std::string> value = albor::core::get_environment_variable(name);
    // assert the exact value comes back, not just a signal that the variable exists
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "albor-env-fixture");
    // the variable is cleared so a later suite cannot observe this fixture
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

TEST(CoreEnv, ReturnsAnOwnedCopyThatSurvivesAnEnvironmentChange) {
    // arrange a variable whose value is captured before the environment changes
    const char* name = "ALBOR_CORE_ENV_TEST_OWNED";
#if defined(_WIN32)
    _putenv_s(name, "first-value");
#else
    setenv(name, "first-value", 1);
#endif
    const std::optional<std::string> value = albor::core::get_environment_variable(name);
    // act remove the variable so a borrowed pointer would no longer read the original value
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
    // assert the earlier result still carries its own copy of the value
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "first-value");
}
