#include "source/extensions/dynamic_modules/stats_scope.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

using ::Envoy::StatusHelpers::HasStatus;
using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

class DynamicModuleStatsScopeTest : public testing::Test {
protected:
  void expectScopeCreation(absl::string_view prefix) {
    EXPECT_CALL(parent_scope_, checkCreateScopeArgs(false, _))
        .WillOnce(Invoke([](bool, const Stats::ScopeStatsLimitSettings& limits) {
          EXPECT_EQ(limits.max_counters, std::nullopt);
          EXPECT_EQ(limits.max_gauges, std::nullopt);
          EXPECT_EQ(limits.max_histograms, std::nullopt);
        }));
    EXPECT_CALL(parent_scope_, createScope_(std::string(prefix)))
        .WillOnce(Return(stats_store_.rootScope()));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<Stats::MockStore> stats_store_;
  Stats::MockScope& parent_scope_{stats_store_.mockScope()};
};

TEST_F(DynamicModuleStatsScopeTest, UsesProducerDefaultPrefix) {
  envoy::extensions::dynamic_modules::v3::DynamicModuleConfig config;
  expectScopeCreation("producer_default");

  auto result = createStatsScope(config, "producer_default", DynamicModulesStatsScopeDomain,
                                 parent_scope_, server_context_);

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->prefix, "producer_default");
  EXPECT_EQ(result->scope, stats_store_.rootScope());
}

TEST_F(DynamicModuleStatsScopeTest, UsesMetricsNamespaceWithLimitsOnlyScope) {
  envoy::extensions::dynamic_modules::v3::DynamicModuleConfig config;
  config.set_metrics_namespace("legacy_namespace");
  config.mutable_stats_scope()->mutable_max_counters()->set_value(11);
  config.mutable_stats_scope()->mutable_max_gauges()->set_value(12);
  config.mutable_stats_scope()->mutable_max_histograms()->set_value(13);

  EXPECT_CALL(parent_scope_, checkCreateScopeArgs(false, _))
      .WillOnce(Invoke([](bool, const Stats::ScopeStatsLimitSettings& limits) {
        EXPECT_EQ(limits.max_counters, 11);
        EXPECT_EQ(limits.max_gauges, 12);
        EXPECT_EQ(limits.max_histograms, 13);
      }));
  EXPECT_CALL(parent_scope_, createScope_("legacy_namespace"))
      .WillOnce(Return(stats_store_.rootScope()));

  auto result = createStatsScope(config, "producer_default", DynamicModulesStatsScopeDomain,
                                 parent_scope_, server_context_);

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->prefix, "legacy_namespace");
}

TEST_F(DynamicModuleStatsScopeTest, UsesStatsScopePrefix) {
  envoy::extensions::dynamic_modules::v3::DynamicModuleConfig config;
  config.mutable_stats_scope()->set_prefix("scope_prefix");
  expectScopeCreation("scope_prefix");

  auto result = createStatsScope(config, "producer_default", DynamicModulesStatsScopeDomain,
                                 parent_scope_, server_context_);

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->prefix, "scope_prefix");
}

TEST_F(DynamicModuleStatsScopeTest, SanitizesPrefixBeforeScopeCreation) {
  envoy::extensions::dynamic_modules::v3::DynamicModuleConfig config;
  config.mutable_stats_scope()->set_prefix("..scope_prefix..");
  expectScopeCreation("scope_prefix");

  auto result = createStatsScope(config, "producer_default", DynamicModulesStatsScopeDomain,
                                 parent_scope_, server_context_);

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->prefix, "scope_prefix");
}

TEST_F(DynamicModuleStatsScopeTest, RejectsMetricsNamespaceAndStatsScopePrefix) {
  envoy::extensions::dynamic_modules::v3::DynamicModuleConfig config;
  config.set_metrics_namespace("legacy_namespace");
  config.mutable_stats_scope()->set_prefix("scope_prefix");
  EXPECT_CALL(parent_scope_, checkCreateScopeArgs(_, _)).Times(0);
  EXPECT_CALL(parent_scope_, createScope_(_)).Times(0);

  auto result = createStatsScope(config, "producer_default", DynamicModulesStatsScopeDomain,
                                 parent_scope_, server_context_);

  EXPECT_THAT(result,
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "metrics_namespace and stats_scope.prefix cannot both be non-empty"));
}

TEST_F(DynamicModuleStatsScopeTest, RejectsEviction) {
  envoy::extensions::dynamic_modules::v3::DynamicModuleConfig config;
  config.mutable_stats_scope()->set_enable_eviction(true);
  EXPECT_CALL(parent_scope_, checkCreateScopeArgs(_, _)).Times(0);
  EXPECT_CALL(parent_scope_, createScope_(_)).Times(0);

  auto result = createStatsScope(config, "producer_default", DynamicModulesStatsScopeDomain,
                                 parent_scope_, server_context_);

  EXPECT_THAT(result, HasStatus(absl::StatusCode::kInvalidArgument,
                                "Dynamic modules do not support stats_scope.enable_eviction"));
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
