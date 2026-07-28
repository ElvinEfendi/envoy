#include "source/common/stats/scope_provider_singleton.h"

#include "envoy/singleton/instance.h"
#include "envoy/type/v3/scope.pb.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Stats {

namespace {

struct ScopeConfiguration {
  Stats::ScopeStatsLimitSettings limits;
  bool evictable_;
};

ScopeConfiguration convertProtoToScopeStatsLimitSettings(const envoy::type::v3::Scope& config) {
  Stats::ScopeStatsLimitSettings limits;
  if (config.has_max_counters()) {
    limits.max_counters = config.max_counters().value();
  }
  if (config.has_max_gauges()) {
    limits.max_gauges = config.max_gauges().value();
  }
  if (config.has_max_histograms()) {
    limits.max_histograms = config.max_histograms().value();
  }
  return {limits, config.enable_eviction()};
}

} // namespace

SINGLETON_MANAGER_REGISTRATION(stats_scope_provider);

ScopeProviderSingleton::ScopeCacheKey::ScopeCacheKey(absl::string_view consumer_domain,
                                                     const envoy::type::v3::Scope& config)
    : consumer_domain_(consumer_domain) {
  RELEASE_ASSERT(config.SerializeToString(&serialized_config_),
                 "stats scope configuration must be serializable");
}

Stats::ScopeSharedPtr ScopeProviderSingleton::getScope(
    Server::Configuration::ServerFactoryContext& server_factory_context, Stats::Scope& parent_scope,
    const envoy::type::v3::Scope& config, absl::string_view consumer_domain) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (config.sharing_name().empty()) {
    ScopeConfiguration scope_cfg = convertProtoToScopeStatsLimitSettings(config);
    return parent_scope.createScope(config.prefix(), scope_cfg.evictable_, scope_cfg.limits);
  }

  ScopeProviderSingletonSharedPtr provider =
      server_factory_context.singletonManager().getTyped<ScopeProviderSingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(stats_scope_provider),
          [] { return std::make_shared<ScopeProviderSingleton>(); });

  ScopeCacheKey key(consumer_domain, config);
  auto it = provider->scopes_.find(key);
  if (it != provider->scopes_.end()) {
    Stats::ScopeSharedPtr scope = it->second.lock();
    if (scope != nullptr) {
      return scope;
    }
  }

  ScopeConfiguration scope_cfg = convertProtoToScopeStatsLimitSettings(config);
  Stats::ScopeSharedPtr scope = server_factory_context.serverScope().createScope(
      config.prefix(), scope_cfg.evictable_, scope_cfg.limits);
  std::weak_ptr<Stats::Scope> weak_scope = scope;

  Event::Dispatcher& dispatcher = server_factory_context.mainThreadDispatcher();

  // The returned scope captures the provider (the shared_ptr) by value in its cleanup callback.
  // This keeps the provider (and singleton cache) alive as long as the scope exists.
  scope->setCleanupCallback([key, provider, weak_scope, &dispatcher]() {
    dispatcher.post([key, provider, weak_scope]() {
      auto map_it = provider->scopes_.find(key);
      const bool same_scope = map_it != provider->scopes_.end() &&
                              !map_it->second.owner_before(weak_scope) &&
                              !weak_scope.owner_before(map_it->second);
      if (same_scope && map_it->second.expired()) {
        provider->scopes_.erase(map_it);
      }
    });
  });

  provider->scopes_[key] = scope;

  return scope;
}

} // namespace Stats
} // namespace Envoy
