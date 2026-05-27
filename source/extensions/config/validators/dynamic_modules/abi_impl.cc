#include "source/extensions/config/validators/dynamic_modules/config_validator.h"

#include "source/common/common/assert.h"

#include "absl/strings/string_view.h"

extern "C" {

void envoy_dynamic_module_callback_config_validator_set_rejection_message(
    envoy_dynamic_module_type_config_validator_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer rejection_message) {
  if (config_envoy_ptr == nullptr) {
    IS_ENVOY_BUG(
        "envoy_dynamic_module_callback_config_validator_set_rejection_message: null config");
    return;
  }
  auto* config = static_cast<Envoy::Extensions::Config::Validators::DynamicModules::
                                DynamicModuleConfigValidatorConfig*>(config_envoy_ptr);
  if (rejection_message.ptr == nullptr && rejection_message.length != 0) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_config_validator_set_rejection_message: null "
                 "message with non-zero length");
    return;
  }
  config->setRejectionMessage(
      rejection_message.ptr == nullptr
          ? absl::string_view()
          : absl::string_view(rejection_message.ptr, rejection_message.length));
}

} // extern "C"
