#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Minimal stats sink module that implements all four required hooks. The stats-scope factory tests
// use a reserved sink name to define one gauge during config initialization.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_stat_sink_config_module_ptr
envoy_dynamic_module_on_stat_sink_config_new(
    envoy_dynamic_module_type_stat_sink_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer sink_name,
    envoy_dynamic_module_type_envoy_buffer sink_config) {
  (void)sink_config;
  if (sink_name.length == 16 && memcmp(sink_name.ptr, "stats_scope_test", 16) == 0) {
    size_t gauge_id = 0;
    envoy_dynamic_module_type_module_buffer gauge_name = {.ptr = "defined_gauge", .length = 13};
    (void)envoy_dynamic_module_callback_stat_sink_config_define_gauge(
        config_envoy_ptr, gauge_name, &gauge_id);
  }
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_stat_sink_config_destroy(
    envoy_dynamic_module_type_stat_sink_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

void envoy_dynamic_module_on_stat_sink_flush(
    envoy_dynamic_module_type_stat_sink_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  (void)config_module_ptr;
  (void)snapshot_envoy_ptr;
}

void envoy_dynamic_module_on_stat_sink_on_histogram_complete(
    envoy_dynamic_module_type_stat_sink_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer histogram_name, uint64_t value) {
  (void)config_module_ptr;
  (void)histogram_name;
  (void)value;
}
