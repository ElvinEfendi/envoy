#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/stats/allocator.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/scope_provider_singleton.h"
#include "source/common/stats/thread_local_store.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class ScopeProviderSingletonTest : public testing::Test {
public:
  ScopeProviderSingletonTest()
      : server_scope_(server_store_.rootScope()->createScope("server")),
        parent_scope_(parent_store_.rootScope()->createScope("parent")) {
    ON_CALL(server_factory_context_, serverScope()).WillByDefault(ReturnRef(*server_scope_));
  }

  std::string prefix(const Scope& scope) {
    return scope.constSymbolTable().toString(scope.prefix());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  IsolatedStoreImpl server_store_;
  IsolatedStoreImpl parent_store_;
  ScopeSharedPtr server_scope_;
  ScopeSharedPtr parent_scope_;
};

TEST_F(ScopeProviderSingletonTest, SharedScopeReusesSameDomainAndConfigFromServerScope) {
  envoy::type::v3::Scope config;
  config.set_prefix("metrics");
  config.set_sharing_name("shared");
  config.mutable_max_counters()->set_value(10);

  ScopeSharedPtr first = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                          config, "consumer-a");
  ScopeSharedPtr second = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                           config, "consumer-a");

  EXPECT_EQ(first, second);
  EXPECT_EQ(&server_store_, &first->store());
  EXPECT_EQ("server.metrics", prefix(*first));
}

TEST_F(ScopeProviderSingletonTest, SharedScopesAreIsolatedByConsumerDomain) {
  envoy::type::v3::Scope config;
  config.set_prefix("metrics");
  config.set_sharing_name("shared");

  ScopeSharedPtr first = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                          config, "consumer-a");
  ScopeSharedPtr second = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                           config, "consumer-b");

  EXPECT_NE(first, second);
  EXPECT_EQ("server.metrics", prefix(*first));
  EXPECT_EQ("server.metrics", prefix(*second));
}

TEST_F(ScopeProviderSingletonTest, SharedScopesAreIsolatedByFullConfig) {
  envoy::type::v3::Scope config_one;
  config_one.set_prefix("metrics");
  config_one.set_sharing_name("shared");
  config_one.mutable_max_counters()->set_value(10);

  envoy::type::v3::Scope config_two = config_one;
  config_two.mutable_max_counters()->set_value(11);

  ScopeSharedPtr first = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                          config_one, "consumer-a");
  ScopeSharedPtr second = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                           config_two, "consumer-a");

  EXPECT_NE(first, second);
}

TEST_F(ScopeProviderSingletonTest, SharedLimitsAggregateWithinDomainAndIsolateAcrossDomains) {
  SymbolTableImpl symbol_table;
  Allocator allocator(symbol_table);
  ThreadLocalStoreImpl stats_store(allocator);
  ScopeSharedPtr root_scope = stats_store.rootScope();
  ON_CALL(server_factory_context_, serverScope()).WillByDefault(ReturnRef(*root_scope));

  envoy::type::v3::Scope config;
  config.set_prefix("metrics");
  config.set_sharing_name("shared");
  config.mutable_max_counters()->set_value(1);

  ScopeSharedPtr first = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                          config, "consumer-a");
  ScopeSharedPtr second = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                           config, "consumer-a");
  ScopeSharedPtr other_domain = ScopeProviderSingleton::getScope(
      server_factory_context_, *parent_scope_, config, "consumer-b");

  EXPECT_EQ(first, second);
  EXPECT_NE(first, other_domain);

  Counter& admitted = first->counterFromString("consumer_a_first");
  Counter& rejected = second->counterFromString("consumer_a_second");
  Counter& other_domain_admitted = other_domain->counterFromString("consumer_b_first");

  EXPECT_NE(&admitted, &stats_store.nullCounter());
  EXPECT_EQ(&rejected, &stats_store.nullCounter());
  EXPECT_NE(&other_domain_admitted, &stats_store.nullCounter());
  EXPECT_EQ(1, root_scope->counterFromString("server.stats_overflow.counter").value());
}

TEST_F(ScopeProviderSingletonTest, UnsharedScopesAreDistinctAndUseExplicitParent) {
  envoy::type::v3::Scope config;
  config.set_prefix("metrics");

  EXPECT_CALL(server_factory_context_, serverScope()).Times(0);
  ScopeSharedPtr first = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                          config, "consumer-a");
  ScopeSharedPtr second = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                           config, "consumer-a");

  EXPECT_NE(first, second);
  EXPECT_EQ(&parent_store_, &first->store());
  EXPECT_EQ(&parent_store_, &second->store());
  EXPECT_EQ("parent.metrics", prefix(*first));
  EXPECT_EQ("parent.metrics", prefix(*second));
}

TEST_F(ScopeProviderSingletonTest, ForwardsLimitsAndEviction) {
  NiceMock<MockStore> parent_store;
  ScopeSharedPtr parent_scope = parent_store.rootScope();

  envoy::type::v3::Scope config;
  config.set_prefix("metrics");
  config.set_enable_eviction(true);
  config.mutable_max_counters()->set_value(10);
  config.mutable_max_gauges()->set_value(20);
  config.mutable_max_histograms()->set_value(30);

  EXPECT_CALL(parent_store.mockScope(), checkCreateScopeArgs(true, _))
      .WillOnce(Invoke([](bool, const ScopeStatsLimitSettings& limits) {
        ASSERT_TRUE(limits.max_counters.has_value());
        ASSERT_TRUE(limits.max_gauges.has_value());
        ASSERT_TRUE(limits.max_histograms.has_value());
        EXPECT_EQ(10, limits.max_counters.value());
        EXPECT_EQ(20, limits.max_gauges.value());
        EXPECT_EQ(30, limits.max_histograms.value());
      }));
  EXPECT_CALL(parent_store.mockScope(), createScope_("metrics")).WillOnce(Return(parent_scope));

  EXPECT_EQ(parent_scope,
            ScopeProviderSingleton::getScope(server_factory_context_, parent_store.mockScope(),
                                             config, "consumer-a"));
}

TEST_F(ScopeProviderSingletonTest, CleanupErasesExpiredCurrentEntry) {
  NiceMock<MockStore> server_store;
  ON_CALL(server_factory_context_, serverScope())
      .WillByDefault(ReturnRef(server_store.mockScope()));

  std::vector<std::unique_ptr<StatNameDynamicStorage>> name_storages;
  std::vector<Event::PostCb> cleanup_callbacks;
  ON_CALL(server_factory_context_.dispatcher_, post(_))
      .WillByDefault(Invoke([&cleanup_callbacks](Event::PostCb callback) {
        cleanup_callbacks.push_back(std::move(callback));
      }));
  EXPECT_CALL(server_store.mockScope(), createScope_(_))
      .Times(3)
      .WillRepeatedly(Invoke([&name_storages, &server_store](const std::string& name) {
        name_storages.emplace_back(
            std::make_unique<StatNameDynamicStorage>(name, server_store.symbolTable()));
        return std::make_shared<NiceMock<MockScope>>(name_storages.back()->statName(),
                                                     server_store);
      }));

  envoy::type::v3::Scope config;
  config.set_prefix("metrics");
  config.set_sharing_name("shared");

  envoy::type::v3::Scope keeper_config;
  keeper_config.set_prefix("keeper");
  keeper_config.set_sharing_name("keeper");

  ScopeSharedPtr scope = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                          config, "consumer-a");
  ScopeSharedPtr keeper = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                           keeper_config, "consumer-a");
  scope.reset();

  ASSERT_EQ(1, cleanup_callbacks.size());
  Event::PostCb cleanup = std::move(cleanup_callbacks.front());
  cleanup_callbacks.clear();
  cleanup();
  cleanup = nullptr;

  ScopeSharedPtr replacement = ScopeProviderSingleton::getScope(
      server_factory_context_, *parent_scope_, config, "consumer-a");
  EXPECT_NE(replacement, keeper);
}

TEST_F(ScopeProviderSingletonTest, CleanupPreservesNewerLiveScopeAndGetSharedLifetime) {
  NiceMock<MockStore> server_store;
  ON_CALL(server_factory_context_, serverScope())
      .WillByDefault(ReturnRef(server_store.mockScope()));

  std::vector<std::unique_ptr<StatNameDynamicStorage>> name_storages;
  EXPECT_CALL(server_store.mockScope(), createScope_("metrics"))
      .Times(2)
      .WillRepeatedly(Invoke([&name_storages, &server_store](const std::string& name) {
        name_storages.emplace_back(
            std::make_unique<StatNameDynamicStorage>(name, server_store.symbolTable()));
        return std::make_shared<NiceMock<MockScope>>(name_storages.back()->statName(),
                                                     server_store);
      }));

  envoy::type::v3::Scope config;
  config.set_prefix("metrics");
  config.set_sharing_name("shared");

  Event::PostCb old_cleanup;
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_))
      .WillOnce(
          Invoke([&old_cleanup](Event::PostCb callback) { old_cleanup = std::move(callback); }));

  ScopeSharedPtr first = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                          config, "consumer-a");
  std::weak_ptr<Scope> first_weak = first;
  ScopeSharedPtr shared_copy = first->getShared();
  first.reset();

  ScopeSharedPtr cached = ScopeProviderSingleton::getScope(server_factory_context_, *parent_scope_,
                                                           config, "consumer-a");
  EXPECT_EQ(cached, shared_copy);
  EXPECT_FALSE(first_weak.expired());

  cached.reset();
  shared_copy.reset();
  EXPECT_TRUE(first_weak.expired());
  ASSERT_TRUE(old_cleanup);
  testing::Mock::VerifyAndClearExpectations(&server_factory_context_.dispatcher_);

  ScopeSharedPtr replacement = ScopeProviderSingleton::getScope(
      server_factory_context_, *parent_scope_, config, "consumer-a");
  old_cleanup();
  ScopeSharedPtr replacement_cached = ScopeProviderSingleton::getScope(
      server_factory_context_, *parent_scope_, config, "consumer-a");

  EXPECT_EQ(replacement, replacement_cached);
}

} // namespace
} // namespace Stats
} // namespace Envoy
