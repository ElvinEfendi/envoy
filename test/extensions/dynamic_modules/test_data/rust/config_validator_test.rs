//! Test dynamic module implementing an xDS config validator.

use envoy_proxy_dynamic_modules_rust_sdk::config_validator::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_all_init_functions!(init, config_validator: new_config_validator_config);

fn init() -> bool {
  true
}

fn new_config_validator_config(
  name: &str,
  config: &[u8],
) -> Option<Box<dyn ConfigValidatorConfig>> {
  match name {
    "accept_all" => Some(Box::new(AcceptAllValidator)),
    "required_clusters" => Some(Box::new(RequiredClustersValidator {
      required_cluster: String::from_utf8(config.to_vec()).ok()?,
    })),
    "empty_rejection_message" => Some(Box::new(EmptyRejectionMessageValidator)),
    "panic" => Some(Box::new(PanicValidator)),
    _ => None,
  }
}

struct AcceptAllValidator;

impl ConfigValidatorConfig for AcceptAllValidator {
  fn validate(
    &self,
    _type_url: &str,
    _resources: &[ConfigValidatorResource],
  ) -> Result<(), String> {
    Ok(())
  }

  fn validate_delta(
    &self,
    _type_url: &str,
    _added_resources: &[ConfigValidatorResource],
    _removed_resources: &[&str],
  ) -> Result<(), String> {
    Ok(())
  }
}

struct RequiredClustersValidator {
  required_cluster: String,
}

impl ConfigValidatorConfig for RequiredClustersValidator {
  fn validate(&self, type_url: &str, resources: &[ConfigValidatorResource]) -> Result<(), String> {
    if !type_url.ends_with("envoy.config.cluster.v3.Cluster") {
      return Err(format!("unexpected type URL: {type_url}"));
    }
    if resources
      .iter()
      .any(|resource| resource.name.as_ref() == self.required_cluster)
    {
      return Ok(());
    }
    Err(format!(
      "required cluster '{}' is absent",
      self.required_cluster
    ))
  }

  fn validate_delta(
    &self,
    type_url: &str,
    _added_resources: &[ConfigValidatorResource],
    removed_resources: &[&str],
  ) -> Result<(), String> {
    if !type_url.ends_with("envoy.config.cluster.v3.Cluster") {
      return Err(format!("unexpected type URL: {type_url}"));
    }
    if removed_resources
      .iter()
      .any(|removed_resource| *removed_resource == self.required_cluster)
    {
      return Err(format!(
        "required cluster '{}' was removed",
        self.required_cluster
      ));
    }
    Ok(())
  }
}

struct EmptyRejectionMessageValidator;

impl ConfigValidatorConfig for EmptyRejectionMessageValidator {
  fn validate(
    &self,
    _type_url: &str,
    _resources: &[ConfigValidatorResource],
  ) -> Result<(), String> {
    Err(String::new())
  }

  fn validate_delta(
    &self,
    _type_url: &str,
    _added_resources: &[ConfigValidatorResource],
    _removed_resources: &[&str],
  ) -> Result<(), String> {
    Err(String::new())
  }
}

struct PanicValidator;

impl ConfigValidatorConfig for PanicValidator {
  fn validate(
    &self,
    _type_url: &str,
    _resources: &[ConfigValidatorResource],
  ) -> Result<(), String> {
    panic!("validate panic");
  }

  fn validate_delta(
    &self,
    _type_url: &str,
    _added_resources: &[ConfigValidatorResource],
    _removed_resources: &[&str],
  ) -> Result<(), String> {
    panic!("validate_delta panic");
  }
}
