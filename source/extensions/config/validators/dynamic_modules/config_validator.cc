#include "source/extensions/config/validators/dynamic_modules/config_validator.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {
namespace DynamicModules {
namespace {

envoy_dynamic_module_type_envoy_buffer makeEnvoyBuffer(absl::string_view value) {
  return {value.data(), value.size()};
}

struct SerializedResource {
  std::string name;
  std::string version;
  std::string serialized_resource;
};

std::vector<envoy_dynamic_module_type_config_validator_resource>
serializeResources(const std::vector<Envoy::Config::DecodedResourcePtr>& resources,
                   std::vector<SerializedResource>& serialized_resources) {
  serialized_resources.clear();
  serialized_resources.resize(resources.size());

  std::vector<envoy_dynamic_module_type_config_validator_resource> abi_resources;
  abi_resources.resize(resources.size());

  for (size_t i = 0; i < resources.size(); ++i) {
    const auto& resource = resources[i];
    SerializedResource& serialized = serialized_resources[i];
    serialized.name = resource->name();
    serialized.version = resource->version();
    if (!resource->resource().SerializeToString(&serialized.serialized_resource)) {
      throw EnvoyException(
          absl::StrCat("Failed to serialize xDS resource for dynamic module config validator: ",
                       resource->name()));
    }
    abi_resources[i] = {makeEnvoyBuffer(serialized.name), makeEnvoyBuffer(serialized.version),
                        makeEnvoyBuffer(serialized.serialized_resource)};
  }

  return abi_resources;
}

std::vector<envoy_dynamic_module_type_envoy_buffer>
removedResourceBuffers(const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  std::vector<envoy_dynamic_module_type_envoy_buffer> buffers;
  buffers.reserve(removed_resources.size());
  for (const auto& removed_resource : removed_resources) {
    buffers.push_back(makeEnvoyBuffer(removed_resource));
  }
  return buffers;
}

void maybeThrowRejection(DynamicModuleConfigValidatorConfig& config, absl::string_view type_url,
                         bool accepted) {
  if (accepted) {
    return;
  }

  absl::optional<std::string> rejection_message = config.takeRejectionMessage();
  if (rejection_message.has_value() && !rejection_message->empty()) {
    throw EnvoyException(absl::StrCat("dynamic module config validator rejected ", type_url,
                                      " update: ", rejection_message.value()));
  }
  throw EnvoyException(
      absl::StrCat("dynamic module config validator rejected ", type_url, " update"));
}

} // namespace

DynamicModuleConfigValidatorConfig::DynamicModuleConfigValidatorConfig(
    absl::string_view extension_name, absl::string_view extension_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : extension_name_(extension_name), extension_config_(extension_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleConfigValidatorConfig::~DynamicModuleConfigValidatorConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

void DynamicModuleConfigValidatorConfig::setRejectionMessage(absl::string_view message) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  rejection_message_ = std::string(message);
}

absl::optional<std::string> DynamicModuleConfigValidatorConfig::takeRejectionMessage() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  absl::optional<std::string> message = std::move(rejection_message_);
  rejection_message_.reset();
  return message;
}

absl::StatusOr<DynamicModuleConfigValidatorConfigSharedPtr>
newDynamicModuleConfigValidatorConfig(
    absl::string_view extension_name, absl::string_view extension_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnConfigValidatorConfigNewType>(
      "envoy_dynamic_module_on_config_validator_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnConfigValidatorConfigDestroyType>(
      "envoy_dynamic_module_on_config_validator_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_validate = dynamic_module->getFunctionPointer<OnConfigValidatorValidateType>(
      "envoy_dynamic_module_on_config_validator_validate");
  RETURN_IF_NOT_OK_REF(on_validate.status());

  auto on_validate_delta = dynamic_module->getFunctionPointer<OnConfigValidatorValidateDeltaType>(
      "envoy_dynamic_module_on_config_validator_validate_delta");
  RETURN_IF_NOT_OK_REF(on_validate_delta.status());

  auto config = std::make_shared<DynamicModuleConfigValidatorConfig>(
      extension_name, extension_config, std::move(dynamic_module));
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_validate_ = on_validate.value();
  config->on_validate_delta_ = on_validate_delta.value();

  // The ABI buffers below borrow from strings owned by config and are valid only for the
  // on_config_new call. Modules must copy anything they retain.
  envoy_dynamic_module_type_envoy_buffer name_buffer = makeEnvoyBuffer(config->extension_name_);
  envoy_dynamic_module_type_envoy_buffer config_buffer =
      makeEnvoyBuffer(config->extension_config_);
  config->in_module_config_ =
      on_config_new.value()(static_cast<void*>(config.get()), name_buffer, config_buffer);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module config validator");
  }

  return config;
}

DynamicModuleConfigValidator::DynamicModuleConfigValidator(
    absl::string_view type_url, DynamicModuleConfigValidatorConfigSharedPtr config)
    : type_url_(type_url), config_(std::move(config)) {}

void DynamicModuleConfigValidator::validate(
    const Server::Instance&, const std::vector<Envoy::Config::DecodedResourcePtr>& resources) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  config_->takeRejectionMessage();

  std::vector<SerializedResource> serialized_resources;
  std::vector<envoy_dynamic_module_type_config_validator_resource> abi_resources =
      serializeResources(resources, serialized_resources);

  const bool accepted = config_->on_validate_(
      static_cast<void*>(config_.get()), config_->in_module_config_, makeEnvoyBuffer(type_url_),
      abi_resources.data(), abi_resources.size());
  maybeThrowRejection(*config_, type_url_, accepted);
}

void DynamicModuleConfigValidator::validate(
    const Server::Instance&, const std::vector<Envoy::Config::DecodedResourcePtr>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  config_->takeRejectionMessage();

  std::vector<SerializedResource> serialized_resources;
  std::vector<envoy_dynamic_module_type_config_validator_resource> abi_added_resources =
      serializeResources(added_resources, serialized_resources);
  std::vector<envoy_dynamic_module_type_envoy_buffer> abi_removed_resources =
      removedResourceBuffers(removed_resources);

  const bool accepted = config_->on_validate_delta_(
      static_cast<void*>(config_.get()), config_->in_module_config_, makeEnvoyBuffer(type_url_),
      abi_added_resources.data(), abi_added_resources.size(), abi_removed_resources.data(),
      abi_removed_resources.size());
  maybeThrowRejection(*config_, type_url_, accepted);
}

} // namespace DynamicModules
} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
