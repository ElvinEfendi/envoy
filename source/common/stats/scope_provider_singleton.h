#pragma once

#include <memory>
#include <string>
#include <utility>

#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/type/v3/scope.pb.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

// ScopeProviderSingleton is a process-wide singleton responsible for managing and vending scopes
// with inline limits configuration.
class ScopeProviderSingleton;
using ScopeProviderSingletonSharedPtr = std::shared_ptr<ScopeProviderSingleton>;

class ScopeProviderSingleton : public Singleton::Instance {
public:
  static Stats::ScopeSharedPtr
  getScope(Server::Configuration::ServerFactoryContext& server_factory_context,
           Stats::Scope& parent_scope, const envoy::type::v3::Scope& config,
           absl::string_view consumer_domain);

  ScopeProviderSingleton() = default;

private:
  struct ScopeCacheKey {
    ScopeCacheKey(absl::string_view consumer_domain, const envoy::type::v3::Scope& config);

    bool operator==(const ScopeCacheKey& rhs) const {
      return consumer_domain_ == rhs.consumer_domain_ &&
             serialized_config_ == rhs.serialized_config_;
    }

    template <typename H> friend H AbslHashValue(H h, const ScopeCacheKey& key) {
      return H::combine(std::move(h), key.consumer_domain_, key.serialized_config_);
    }

    std::string consumer_domain_;
    std::string serialized_config_;
  };

  absl::flat_hash_map<ScopeCacheKey, std::weak_ptr<Stats::Scope>> scopes_;
};

} // namespace Stats
} // namespace Envoy
