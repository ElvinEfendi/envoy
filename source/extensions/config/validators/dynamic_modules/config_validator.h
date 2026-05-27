#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/config_validator.h"

#include "source/common/common/statusor.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {
namespace DynamicModules {

using OnConfigValidatorConfigNewType =
    decltype(&envoy_dynamic_module_on_config_validator_config_new);
using OnConfigValidatorConfigDestroyType =
    decltype(&envoy_dynamic_module_on_config_validator_config_destroy);
using OnConfigValidatorValidateType = decltype(&envoy_dynamic_module_on_config_validator_validate);
using OnConfigValidatorValidateDeltaType =
    decltype(&envoy_dynamic_module_on_config_validator_validate_delta);

class DynamicModuleConfigValidatorConfig {
public:
  DynamicModuleConfigValidatorConfig(
      absl::string_view extension_name, absl::string_view extension_config,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);
  ~DynamicModuleConfigValidatorConfig();

  void setRejectionMessage(absl::string_view message);
  absl::optional<std::string> takeRejectionMessage();

  envoy_dynamic_module_type_config_validator_config_module_ptr in_module_config_{nullptr};
  OnConfigValidatorConfigDestroyType on_config_destroy_{nullptr};
  OnConfigValidatorValidateType on_validate_{nullptr};
  OnConfigValidatorValidateDeltaType on_validate_delta_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleConfigValidatorConfig>>
  newDynamicModuleConfigValidatorConfig(
      absl::string_view extension_name, absl::string_view extension_config,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string extension_name_;
  const std::string extension_config_;
  // Keep the module owned by the validator config so its destroy callback is still callable from
  // ~DynamicModuleConfigValidatorConfig before the shared object can be closed.
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  absl::optional<std::string> rejection_message_;
};

using DynamicModuleConfigValidatorConfigSharedPtr =
    std::shared_ptr<DynamicModuleConfigValidatorConfig>;

absl::StatusOr<DynamicModuleConfigValidatorConfigSharedPtr>
newDynamicModuleConfigValidatorConfig(
    absl::string_view extension_name, absl::string_view extension_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

class DynamicModuleConfigValidator : public Envoy::Config::ConfigValidator {
public:
  DynamicModuleConfigValidator(absl::string_view type_url,
                               DynamicModuleConfigValidatorConfigSharedPtr config);

  void validate(const Server::Instance& server,
                const std::vector<Envoy::Config::DecodedResourcePtr>& resources) override;

  void validate(const Server::Instance& server,
                const std::vector<Envoy::Config::DecodedResourcePtr>& added_resources,
                const Protobuf::RepeatedPtrField<std::string>& removed_resources) override;

private:
  const std::string type_url_;
  DynamicModuleConfigValidatorConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
