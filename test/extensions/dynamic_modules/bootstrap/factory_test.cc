#include <vector>

#include "envoy/extensions/bootstrap/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/allocator.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/bootstrap/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class FactoryTestBase : public testing::Test {
protected:
  std::string testDataDir() {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/c");
  }

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

// Pull the shared dynamic-modules test helper into scope.
using ::Envoy::Extensions::DynamicModules::failureCounter;

TEST(FactoryTest, Name) {
  DynamicModuleBootstrapExtensionFactory factory;
  EXPECT_EQ(factory.name(), "envoy.bootstrap.dynamic_modules");
}

TEST(FactoryTest, CreateEmptyConfigProto) {
  DynamicModuleBootstrapExtensionFactory factory;
  auto config = factory.createEmptyConfigProto();
  EXPECT_NE(config, nullptr);
}

TEST_F(FactoryTestBase, DynamicModuleLoadFail) {
  // Test that factory throws when dynamic module fails to load.
  DynamicModuleBootstrapExtensionFactory factory;
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir(), 1);

  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("nonexistent_module");
  proto_config.set_extension_name("test");

  EXPECT_THROW_WITH_REGEX(factory.createBootstrapExtension(proto_config, context_), EnvoyException,
                          "Failed to load dynamic module:.*");

  EXPECT_EQ(1U, failureCounter(context_.serverScope(), "module_load_error", "test"));

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(FactoryTestBase, ExtensionConfigCreateFail) {
  // Test that factory throws when extension config creation fails.
  DynamicModuleBootstrapExtensionFactory factory;
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir(), 1);

  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("bootstrap_no_config_new");
  proto_config.set_extension_name("test");

  EXPECT_THROW_WITH_REGEX(factory.createBootstrapExtension(proto_config, context_), EnvoyException,
                          "Failed to create extension config:.*");

  EXPECT_EQ(1U, failureCounter(context_.serverScope(), "config_init_error", "test"));

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(FactoryTestBase, InvalidExtensionConfig) {
  // Test that factory throws when extension_config Any message fails to parse.
  // This covers the config_or_error.ok() check in factory.cc.
  DynamicModuleBootstrapExtensionFactory factory;
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir(), 1);

  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("bootstrap_no_op");
  proto_config.set_extension_name("test");

  // Create an Any message that claims to be a StringValue but has invalid/corrupted data.
  // The type_url says it's a StringValue, but the value is not a valid protobuf encoding.
  auto* extension_config = proto_config.mutable_extension_config();
  extension_config->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  extension_config->set_value("invalid\xff\xfe protobuf data that cannot be parsed");

  EXPECT_THROW_WITH_REGEX(factory.createBootstrapExtension(proto_config, context_), EnvoyException,
                          "Failed to parse extension config:.*");

  EXPECT_EQ(1U, failureCounter(context_.serverScope(), "config_init_error", "test"));

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(FactoryTestBase, LocalFileLoading) {
  // Load the module via the ``module.local.filename`` data source instead of by name.
  DynamicModuleBootstrapExtensionFactory factory;

  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
      testDataDir() + "/libbootstrap_no_op.so");
  proto_config.set_extension_name("test");

  auto extension = factory.createBootstrapExtension(proto_config, context_);
  EXPECT_NE(extension, nullptr);
}

TEST_F(FactoryTestBase, RemoteSourceRejected) {
  // Remote module sources are not supported for bootstrap extensions (no init manager is wired up).
  DynamicModuleBootstrapExtensionFactory factory;

  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  auto* remote = proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_remote();
  remote->mutable_http_uri()->set_uri("https://example.com/module.so");
  remote->mutable_http_uri()->set_cluster("cluster_1");
  remote->mutable_http_uri()->mutable_timeout()->set_seconds(5);
  remote->set_sha256("abc123");
  proto_config.set_extension_name("test");

  EXPECT_THROW(factory.createBootstrapExtension(proto_config, context_), EnvoyException);
}

TEST_F(FactoryTestBase, StatsScopeValidationPrecedesModuleInitialization) {
  DynamicModuleBootstrapExtensionFactory factory;
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir(), 1);

  struct TestCase {
    std::string yaml;
    std::string expected_error;
  };
  const std::vector<TestCase> test_cases = {
      {R"EOF(
dynamic_module_config:
  name: program_init_fail
  stats_scope:
    enable_eviction: true
extension_name: test
)EOF",
       "Dynamic modules do not support stats_scope.enable_eviction"},
      {R"EOF(
dynamic_module_config:
  name: program_init_fail
  metrics_namespace: legacy
  stats_scope:
    prefix: scoped
extension_name: test
)EOF",
       "metrics_namespace and stats_scope.prefix cannot both be non-empty"},
  };

  for (const TestCase& test_case : test_cases) {
    testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
    envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
    TestUtility::loadFromYaml(test_case.yaml, proto_config);

    EXPECT_THROW_WITH_REGEX(factory.createBootstrapExtension(proto_config, context), EnvoyException,
                            test_case.expected_error);
    EXPECT_EQ(1U, failureCounter(context.serverScope(), "config_init_error", "test"));
    EXPECT_EQ(0U, failureCounter(context.serverScope(), "module_load_error", "test"));
  }

  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST_F(FactoryTestBase, StatsScopeIsFinalScopeForModuleMetrics) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/rust"), 1);
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});
  Stats::SymbolTableImpl symbol_table;
  Stats::Allocator allocator(symbol_table);
  Stats::ThreadLocalStoreImpl stats_store(allocator);
  Stats::ScopeSharedPtr root_scope = stats_store.rootScope();
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ON_CALL(context, scope()).WillByDefault(testing::ReturnRef(*root_scope));
  ON_CALL(context, serverScope()).WillByDefault(testing::ReturnRef(*root_scope));
  Stats::CustomStatNamespacesImpl custom_stat_namespaces;
  ON_CALL(context.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: bootstrap_stats_test
  stats_scope:
    prefix: bounded_bootstrap
    max_counters: 0
extension_name: test
)EOF";
  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleBootstrapExtensionFactory factory;
  auto extension = factory.createBootstrapExtension(proto_config, context);

  ASSERT_NE(extension, nullptr);
  EXPECT_EQ(TestUtility::findCounter(stats_store, "bounded_bootstrap.refresh_success_total"),
            nullptr);
  EXPECT_NE(TestUtility::findGauge(stats_store, "bounded_bootstrap.connection_state"), nullptr);
  ASSERT_NE(TestUtility::findCounter(stats_store, "server.stats_overflow.counter"), nullptr);
  EXPECT_EQ(TestUtility::findCounter(stats_store, "server.stats_overflow.counter")->value(), 2);
  EXPECT_FALSE(custom_stat_namespaces.registered("bounded_bootstrap"));
  EXPECT_FALSE(custom_stat_namespaces.registered("dynamicmodulescustom"));
}

TEST_F(FactoryTestBase, StatsScopeLimitsPreserveLegacyNamespaceRegistration) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/rust"), 1);
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});
  Stats::CustomStatNamespacesImpl custom_stat_namespaces;
  ON_CALL(context_.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: bootstrap_stats_test
  metrics_namespace: legacy_bootstrap
  stats_scope:
    max_counters: 2
extension_name: test
)EOF";
  envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DynamicModuleBootstrapExtensionFactory factory;
  auto extension = factory.createBootstrapExtension(proto_config, context_);

  ASSERT_NE(extension, nullptr);
  EXPECT_NE(TestUtility::findCounter(context_.serverScope().store(),
                                     "legacy_bootstrap.refresh_success_total"),
            nullptr);
  EXPECT_TRUE(custom_stat_namespaces.registered("legacy_bootstrap"));
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
