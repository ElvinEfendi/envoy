#include "source/extensions/filters/network/dynamic_modules/factory.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/stats_scope.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"
#include "source/extensions/filters/network/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

absl::StatusOr<Network::FilterFactoryCb>
DynamicModuleNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, FactoryContext& context) {

  Server::Configuration::ServerFactoryContext& server_context = context.serverFactoryContext();
  const auto& module_config = proto_config.dynamic_module_config();

  auto stats_scope_or_error = Extensions::DynamicModules::createStatsScope(
      module_config, Extensions::DynamicModules::NetworkFilters::DefaultMetricsNamespace,
      Extensions::DynamicModules::DynamicModulesStatsScopeDomain, server_context.scope(),
      server_context);
  if (!stats_scope_or_error.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        server_context, proto_config.filter_name(),
        Extensions::DynamicModules::ConfigInitErrorStat);
    return stats_scope_or_error.status();
  }
  Extensions::DynamicModules::DynamicModuleStatsScope stats_scope =
      std::move(stats_scope_or_error.value());

  // Network filters do not support remote module sources, so no init manager or async callback is
  // passed; only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      module_config, proto_config.filter_name(), server_context);
  RETURN_IF_NOT_OK_REF(load_result.status());
  auto dynamic_module = std::move(load_result->loaded);

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.filter_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          server_context, proto_config.filter_name(),
          Extensions::DynamicModules::ConfigInitErrorStat);
      return config_or_error.status();
    }
    config = std::move(config_or_error.value());
  }

  absl::StatusOr<
      Envoy::Extensions::DynamicModules::NetworkFilters::DynamicModuleNetworkFilterConfigSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::NetworkFilters::newDynamicModuleNetworkFilterConfig(
              proto_config.filter_name(), config, stats_scope.prefix, std::move(dynamic_module),
              server_context.clusterManager(), server_context.scope(),
              server_context.mainThreadDispatcher(), stats_scope.scope);

  if (!filter_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        server_context, proto_config.filter_name(),
        Extensions::DynamicModules::ConfigInitErrorStat);
    return absl::InvalidArgumentError("Failed to create filter config: " +
                                      std::string(filter_config.status().message()));
  }

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (module_config.stats_scope().prefix().empty() &&
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    const absl::string_view legacy_namespace =
        module_config.metrics_namespace().empty()
            ? Extensions::DynamicModules::NetworkFilters::DefaultMetricsNamespace
            : module_config.metrics_namespace();
    server_context.api().customStatNamespaces().registerStatNamespace(legacy_namespace);
  }

  return [config = filter_config.value()](Network::FilterManager& filter_manager) -> void {
    auto filter = std::make_shared<
        Envoy::Extensions::DynamicModules::NetworkFilters::DynamicModuleNetworkFilter>(config);
    filter_manager.addFilter(filter);
  };
}

/**
 * Static registration for the dynamic modules network filter.
 */
REGISTER_FACTORY(DynamicModuleNetworkFilterConfigFactory, NamedNetworkFilterConfigFactory);

} // namespace Configuration
} // namespace Server
} // namespace Envoy
