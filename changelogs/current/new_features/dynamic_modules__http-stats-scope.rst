Added optional :ref:`stats_scope
<envoy_v3_api_field_extensions.dynamic_modules.v3.DynamicModuleConfig.stats_scope>` configuration
for HTTP dynamic-module custom metrics. It can set per-type metric cardinality limits and share one
process-wide budget across configurations; no finite limit is applied by default.
