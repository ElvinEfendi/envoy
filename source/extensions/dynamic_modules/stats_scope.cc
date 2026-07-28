#include "source/extensions/dynamic_modules/stats_scope.h"

#include <utility>

#include "source/common/stats/scope_provider_singleton.h"
#include "source/common/stats/utility.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

absl::StatusOr<DynamicModuleStatsScope>
createStatsScope(const envoy::extensions::dynamic_modules::v3::DynamicModuleConfig& config,
                 absl::string_view default_prefix, absl::string_view consumer_domain,
                 Stats::Scope& parent_scope,
                 Server::Configuration::ServerFactoryContext& server_factory_context) {
  const envoy::type::v3::Scope& configured_scope = config.stats_scope();
  if (configured_scope.enable_eviction()) {
    return absl::InvalidArgumentError("Dynamic modules do not support stats_scope.enable_eviction");
  }

  if (!configured_scope.prefix().empty() && !config.metrics_namespace().empty()) {
    return absl::InvalidArgumentError(
        "metrics_namespace and stats_scope.prefix cannot both be non-empty");
  }

  std::string prefix;
  if (!configured_scope.prefix().empty()) {
    prefix = configured_scope.prefix();
  } else if (!config.metrics_namespace().empty()) {
    prefix = config.metrics_namespace();
  } else {
    prefix = default_prefix;
  }

  // Scope creation strips one leading and trailing dot. Strip all boundary dots here so the
  // effective cache key is already at that sanitization fixpoint and cannot fork one rendered
  // namespace into multiple shared budgets.
  absl::string_view prefix_view = prefix;
  while (!prefix_view.empty() && prefix_view.front() == '.') {
    prefix_view.remove_prefix(1);
  }
  while (!prefix_view.empty() && prefix_view.back() == '.') {
    prefix_view.remove_suffix(1);
  }
  prefix = Stats::Utility::sanitizeStatsName(prefix_view);

  envoy::type::v3::Scope effective_scope = configured_scope;
  effective_scope.set_prefix(prefix);
  return DynamicModuleStatsScope{
      Stats::ScopeProviderSingleton::getScope(server_factory_context, parent_scope, effective_scope,
                                              consumer_domain),
      std::move(prefix)};
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
