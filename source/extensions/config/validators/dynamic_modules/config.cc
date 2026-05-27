#include "source/extensions/config/validators/dynamic_modules/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/config/validators/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/config/validators/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/config/validators/dynamic_modules/config_validator.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {
namespace DynamicModules {
namespace {

using DynamicModuleConfigValidatorProto =
    envoy::extensions::config::validators::dynamic_modules::v3::DynamicModuleConfigValidator;
using DynamicModuleConfigProto = envoy::extensions::dynamic_modules::v3::DynamicModuleConfig;

Envoy::Extensions::DynamicModules::DynamicModulePtr
loadDynamicModuleOrThrow(const DynamicModuleConfigProto& module_config) {
  absl::StatusOr<Envoy::Extensions::DynamicModules::DynamicModulePtr> dynamic_module;

  if (module_config.has_module()) {
    if (!module_config.module().has_local() || !module_config.module().local().has_filename()) {
      throw EnvoyException(
          "Only local file path module sources are supported for dynamic module config validators");
    }
    dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
        module_config.module().local().filename(), module_config.do_not_close(),
        module_config.load_globally());
  } else {
    if (module_config.name().empty()) {
      throw EnvoyException(
          "Either 'name' or 'module.local.filename' must be specified in dynamic_module_config");
    }
    dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
        module_config.name(), module_config.do_not_close(), module_config.load_globally());
  }

  if (!dynamic_module.ok()) {
    throw EnvoyException("Failed to load dynamic module: " +
                         std::string(dynamic_module.status().message()));
  }
  return std::move(dynamic_module.value());
}

std::string extensionConfigBytesOrThrow(const DynamicModuleConfigValidatorProto& proto_config) {
  if (!proto_config.has_extension_config()) {
    return "";
  }
  auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.extension_config());
  if (!config_or_error.ok()) {
    throw EnvoyException("Failed to parse dynamic module config validator extension_config: " +
                         std::string(config_or_error.status().message()));
  }
  return std::move(config_or_error.value());
}

} // namespace

Envoy::Config::ConfigValidatorPtr DynamicModuleConfigValidatorFactory::createConfigValidator(
    const Protobuf::Any& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& proto_config =
      MessageUtil::anyConvertAndValidate<DynamicModuleConfigValidatorProto>(config,
                                                                            validation_visitor);

  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module =
      loadDynamicModuleOrThrow(proto_config.dynamic_module_config());
  std::string extension_config = extensionConfigBytesOrThrow(proto_config);

  auto validator_config = newDynamicModuleConfigValidatorConfig(
      proto_config.extension_name(), extension_config, std::move(dynamic_module));
  if (!validator_config.ok()) {
    throw EnvoyException("Failed to create dynamic module config validator: " +
                         std::string(validator_config.status().message()));
  }

  return std::make_unique<DynamicModuleConfigValidator>(proto_config.type_url(),
                                                        std::move(validator_config.value()));
}

Envoy::ProtobufTypes::MessagePtr DynamicModuleConfigValidatorFactory::createEmptyConfigProto() {
  return std::make_unique<DynamicModuleConfigValidatorProto>();
}

std::string DynamicModuleConfigValidatorFactory::typeUrl() const {
  IS_ENVOY_BUG("dynamic module config validator type URL requires typed configuration");
  return "";
}

std::string DynamicModuleConfigValidatorFactory::typeUrl(
    const Protobuf::Any& config, ProtobufMessage::ValidationVisitor& validation_visitor) const {
  const auto& proto_config =
      MessageUtil::anyConvertAndValidate<DynamicModuleConfigValidatorProto>(config,
                                                                            validation_visitor);
  return proto_config.type_url();
}

REGISTER_FACTORY(DynamicModuleConfigValidatorFactory, Envoy::Config::ConfigValidatorFactory);

} // namespace DynamicModules
} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
