#include <sonnet/core/Assert.h>

#include <catch2/catch_test_macros.hpp>

// A value that only an assertion reads must compile in Release, where the assertion is compiled out
// and warnings are errors; building this file in Release is half of the test.
TEST_CASE("a compiled-out assertion evaluates neither its expression nor its message", "[core][assert]") {
  int expressionEvaluations = 0;
  int messageEvaluations = 0;
  const int onlyAsserted = 1;
  SONNET_ASSERT((++expressionEvaluations, onlyAsserted == 1), "{}", ++messageEvaluations);
#if SONNET_ASSERTS_ENABLED
  REQUIRE(expressionEvaluations == 1);
#else
  REQUIRE(expressionEvaluations == 0);
#endif
  REQUIRE(messageEvaluations == 0);
}

TEST_CASE("a verification always evaluates its expression but never a passing message", "[core][assert]") {
  int expressionEvaluations = 0;
  int messageEvaluations = 0;
  SONNET_VERIFY(++expressionEvaluations == 1, "{}", ++messageEvaluations);
  REQUIRE(expressionEvaluations == 1);
  REQUIRE(messageEvaluations == 0);
}
