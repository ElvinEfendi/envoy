#include "source/extensions/access_loggers/dynamic_modules/config.h"

#include "envoy/extensions/access_loggers/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/stats_scope.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

AccessLog::InstanceSharedPtr DynamicModuleAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
    Server::Configuration::GenericFactoryContext& context,
    std::vector<Formatter::CommandParserPtr>&&) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog&>(
      config, context.messageValidationVisitor());

  Envoy::Server::Configuration::ServerFactoryContext& server_context =
      context.serverFactoryContext();
  const auto& module_config = proto_config.dynamic_module_config();

  auto stats_scope_or_error = Extensions::DynamicModules::createStatsScope(
      module_config, DefaultMetricsNamespace,
      Extensions::DynamicModules::DynamicModulesStatsScopeDomain, server_context.scope(),
      server_context);
  if (!stats_scope_or_error.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        server_context, proto_config.logger_name(),
        Extensions::DynamicModules::ConfigInitErrorStat);
    throw EnvoyException(std::string(stats_scope_or_error.status().message()));
  }
  Extensions::DynamicModules::DynamicModuleStatsScope stats_scope =
      std::move(stats_scope_or_error.value());

  // Access loggers do not support remote module sources, so no init manager or async callback is
  // passed; only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      module_config, proto_config.logger_name(), server_context);
  if (!load_result.ok()) {
    throw EnvoyException(std::string(load_result.status().message()));
  }
  auto dynamic_module = std::move(load_result->loaded);

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string logger_config_str;
  if (proto_config.has_logger_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.logger_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          server_context, proto_config.logger_name(),
          Extensions::DynamicModules::ConfigInitErrorStat);
      throw EnvoyException("Failed to parse logger config: " +
                           std::string(config_or_error.status().message()));
    }
    logger_config_str = std::move(config_or_error.value());
  }

  auto access_log_config = newDynamicModuleAccessLogConfig(
      proto_config.logger_name(), logger_config_str, stats_scope.prefix, std::move(dynamic_module),
      server_context.scope(), stats_scope.scope);

  if (!access_log_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        server_context, proto_config.logger_name(),
        Extensions::DynamicModules::ConfigInitErrorStat);
    throw EnvoyException("Failed to create access logger config: " +
                         std::string(access_log_config.status().message()));
  }

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (module_config.stats_scope().prefix().empty() &&
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    const absl::string_view legacy_namespace = module_config.metrics_namespace().empty()
                                                   ? DefaultMetricsNamespace
                                                   : module_config.metrics_namespace();
    server_context.api().customStatNamespaces().registerStatNamespace(legacy_namespace);
  }

  return std::make_shared<DynamicModuleAccessLog>(
      std::move(filter), std::move(access_log_config.value()), server_context.threadLocal());
}

ProtobufTypes::MessagePtr DynamicModuleAccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog>();
}

REGISTER_FACTORY(DynamicModuleAccessLogFactory, AccessLog::AccessLogInstanceFactory);

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
