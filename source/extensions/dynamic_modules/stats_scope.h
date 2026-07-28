#pragma once

#include <string>

#include "envoy/extensions/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// All dynamic module extension points use the same domain so that an explicitly shared scope can
// enforce one aggregate budget across them.
inline constexpr absl::string_view DynamicModulesStatsScopeDomain = "envoy.dynamic_modules";

struct DynamicModuleStatsScope {
  // The final scope that metric callbacks must use directly. Creating a child scope would move the
  // configured limits away from the metrics they are intended to bound.
  Stats::ScopeSharedPtr scope;

  // The selected prefix after applying Envoy's stat-name sanitization to a fixpoint. This is also
  // the prefix in the shared-scope cache key, so different spellings of the same rendered name
  // share a budget.
  std::string prefix;
};

/**
 * Builds the final stats scope for a dynamic module metric producer.
 *
 * Prefix precedence is stats_scope.prefix, metrics_namespace, then default_prefix. A non-empty
 * sharing_name creates a process-wide scope through ScopeProviderSingleton; otherwise a distinct
 * child of parent_scope is returned.
 */
absl::StatusOr<DynamicModuleStatsScope>
createStatsScope(const envoy::extensions::dynamic_modules::v3::DynamicModuleConfig& config,
                 absl::string_view default_prefix, absl::string_view consumer_domain,
                 Stats::Scope& parent_scope,
                 Server::Configuration::ServerFactoryContext& server_factory_context);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
