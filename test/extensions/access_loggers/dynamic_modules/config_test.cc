#include <vector>

#include "envoy/registry/registry.h"

#include "source/common/stats/allocator.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/access_loggers/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/server/options.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {
namespace {

class DynamicModuleAccessLogFactoryTest : public testing::Test {
public:
  DynamicModuleAccessLogFactoryTest() {
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("access_log_no_op", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  DynamicModuleAccessLogFactory factory_;
};

// Pull the shared dynamic-modules test helper into scope.
using ::Envoy::Extensions::DynamicModules::failureCounter;

TEST_F(DynamicModuleAccessLogFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.access_loggers.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleAccessLogFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
  EXPECT_NE(
      nullptr,
      dynamic_cast<envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog*>(
          proto.get()));
}

TEST_F(DynamicModuleAccessLogFactoryTest, ValidConfig) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  NiceMock<Server::MockOptions> options;
  ON_CALL(options, concurrency()).WillByDefault(testing::Return(1));
  ON_CALL(context.server_context_, options()).WillByDefault(testing::ReturnRef(options));
  ScopedThreadLocalServerContextSetter setter(context.server_context_);

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_no_op
  do_not_close: true
logger_name: test_logger
logger_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: test_config
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  auto access_log = factory_.createAccessLogInstance(proto_config, std::move(filter), context, {});
  EXPECT_NE(nullptr, access_log);

  // The happy path emits no load-failure counters.
  EXPECT_EQ(0U, failureCounter(context.server_context_.serverScope(), "module_load_error",
                               "test_logger"));
  EXPECT_EQ(0U, failureCounter(context.server_context_.serverScope(), "config_init_error",
                               "test_logger"));
}

// Load the module via the ``module.local.filename`` data source instead of by name.
TEST_F(DynamicModuleAccessLogFactoryTest, ValidConfigWithLocalFile) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  NiceMock<Server::MockOptions> options;
  ON_CALL(options, concurrency()).WillByDefault(testing::Return(1));
  ON_CALL(context.server_context_, options()).WillByDefault(testing::ReturnRef(options));
  ScopedThreadLocalServerContextSetter setter(context.server_context_);

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
      Extensions::DynamicModules::testSharedObjectPath("access_log_no_op", "c"));
  proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
  proto_config.set_logger_name("test_logger");

  AccessLog::FilterPtr filter;
  auto access_log = factory_.createAccessLogInstance(proto_config, std::move(filter), context, {});
  EXPECT_NE(nullptr, access_log);
}

// Remote module sources are not supported for access loggers (no init manager is wired up).
TEST_F(DynamicModuleAccessLogFactoryTest, RemoteSourceRejected) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  ScopedThreadLocalServerContextSetter setter(context.server_context_);

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  auto* remote = proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_remote();
  remote->mutable_http_uri()->set_uri("https://example.com/module.so");
  remote->mutable_http_uri()->set_cluster("cluster_1");
  remote->mutable_http_uri()->mutable_timeout()->set_seconds(5);
  remote->set_sha256("abc123");
  proto_config.set_logger_name("test_logger");

  AccessLog::FilterPtr filter;
  EXPECT_THROW(factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
               EnvoyException);
}

TEST_F(DynamicModuleAccessLogFactoryTest, StatsScopeValidationPrecedesModuleInitialization) {
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
logger_name: test_logger
)EOF",
       "Dynamic modules do not support stats_scope.enable_eviction"},
      {R"EOF(
dynamic_module_config:
  name: program_init_fail
  metrics_namespace: legacy
  stats_scope:
    prefix: scoped
logger_name: test_logger
)EOF",
       "metrics_namespace and stats_scope.prefix cannot both be non-empty"},
  };

  for (const TestCase& test_case : test_cases) {
    NiceMock<Server::Configuration::MockGenericFactoryContext> context;
    envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
    TestUtility::loadFromYaml(test_case.yaml, proto_config);

    AccessLog::FilterPtr filter;
    EXPECT_THROW_WITH_REGEX(
        factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
        EnvoyException, test_case.expected_error);
    EXPECT_EQ(1U, failureCounter(context.server_context_.serverScope(), "config_init_error",
                                 "test_logger"));
    EXPECT_EQ(0U, failureCounter(context.server_context_.serverScope(), "module_load_error",
                                 "test_logger"));
  }
}

TEST_F(DynamicModuleAccessLogFactoryTest, ValidConfigWithFilter) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  NiceMock<Server::MockOptions> options;
  ON_CALL(options, concurrency()).WillByDefault(testing::Return(1));
  ON_CALL(context.server_context_, options()).WillByDefault(testing::ReturnRef(options));
  ScopedThreadLocalServerContextSetter setter(context.server_context_);

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_no_op
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto filter = std::make_unique<NiceMock<AccessLog::MockFilter>>();
  auto access_log = factory_.createAccessLogInstance(proto_config, std::move(filter), context, {});
  EXPECT_NE(nullptr, access_log);
}

TEST_F(DynamicModuleAccessLogFactoryTest, InvalidModule) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: nonexistent_module
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to load.*");

  EXPECT_EQ(1U, failureCounter(context.server_context_.serverScope(), "module_load_error",
                               "test_logger"));
}

TEST_F(DynamicModuleAccessLogFactoryTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<AccessLog::AccessLogInstanceFactory>::getFactory(
      "envoy.access_loggers.dynamic_modules");
  EXPECT_NE(nullptr, factory);
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingConfigNew) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_config_new
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*config_new");

  // The module loads fine but its config creation fails resolving a symbol, so this is counted as
  // config_init_error.
  EXPECT_EQ(1U, failureCounter(context.server_context_.serverScope(), "config_init_error",
                               "test_logger"));
  EXPECT_EQ(0U, failureCounter(context.server_context_.serverScope(), "module_load_error",
                               "test_logger"));
}

TEST_F(DynamicModuleAccessLogFactoryTest, MalformedLoggerConfig) {
  // The module loads fine but the logger_config Any cannot be unpacked, counted as
  // config_init_error. A malformed Any must be built programmatically.
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("access_log_no_op");
  proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
  proto_config.set_logger_name("test_logger");
  auto* any = proto_config.mutable_logger_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("invalid_binary_data_that_cannot_be_unpacked_as_string_value");

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to parse logger config");

  EXPECT_EQ(1U, failureCounter(context.server_context_.serverScope(), "config_init_error",
                               "test_logger"));
  EXPECT_EQ(0U, failureCounter(context.server_context_.serverScope(), "module_load_error",
                               "test_logger"));
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingConfigDestroy) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_config_destroy
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*config_destroy");
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingLoggerNew) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_logger_new
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*logger_new");
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingLoggerLog) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_logger_log
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*logger_log");
}

TEST_F(DynamicModuleAccessLogFactoryTest, MissingLoggerDestroy) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_missing_logger_destroy
  do_not_close: true
logger_name: test_logger
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to resolve symbol.*logger_destroy");
}

// Verifies that the unified Rust SDK factory correctly rejects unknown logger names by
// returning `None`, which the C++ factory translates into an `InvalidArgumentError` with the
// standard "Failed to initialize" message. This exercises the dispatch-by-name code path
// installed via the `access_logger:` arm of `declare_all_init_functions!`.
class DynamicModuleAccessLogFactoryRustTest : public testing::Test {
public:
  DynamicModuleAccessLogFactoryRustTest() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  DynamicModuleAccessLogFactory factory_;
};

TEST_F(DynamicModuleAccessLogFactoryRustTest, UnknownLoggerNameRejectedAtConfigLoad) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  NiceMock<Server::MockOptions> options;
  ON_CALL(options, concurrency()).WillByDefault(testing::Return(1));
  ON_CALL(context.server_context_, options()).WillByDefault(testing::ReturnRef(options));
  ScopedThreadLocalServerContextSetter setter(context.server_context_);

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_integration_test
  do_not_close: true
logger_name: unknown_logger
logger_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: test_config
)EOF";

  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  EXPECT_THROW_WITH_REGEX(
      factory_.createAccessLogInstance(proto_config, std::move(filter), context, {}),
      EnvoyException, "Failed to initialize dynamic module access logger config");
}

TEST_F(DynamicModuleAccessLogFactoryRustTest, StatsScopeIsFinalScopeForModuleMetrics) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});

  Stats::SymbolTableImpl symbol_table;
  Stats::Allocator allocator(symbol_table);
  Stats::ThreadLocalStoreImpl stats_store(allocator);
  Stats::ScopeSharedPtr root_scope = stats_store.rootScope();
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  ON_CALL(context.server_context_, scope()).WillByDefault(testing::ReturnRef(*root_scope));
  ON_CALL(context.server_context_, serverScope()).WillByDefault(testing::ReturnRef(*root_scope));
  NiceMock<Server::MockOptions> options;
  ON_CALL(options, concurrency()).WillByDefault(testing::Return(1));
  ON_CALL(context.server_context_, options()).WillByDefault(testing::ReturnRef(options));
  ScopedThreadLocalServerContextSetter setter(context.server_context_);
  Stats::CustomStatNamespacesImpl custom_stat_namespaces;
  ON_CALL(context.server_context_.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_integration_test
  do_not_close: true
  stats_scope:
    prefix: bounded_access
    max_counters: 0
logger_name: test_logger
)EOF";
  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  auto access_log = factory_.createAccessLogInstance(proto_config, std::move(filter), context, {});

  ASSERT_NE(access_log, nullptr);
  EXPECT_EQ(TestUtility::findCounter(stats_store, "bounded_access.test_log_count"), nullptr);
  EXPECT_EQ(TestUtility::findCounter(stats_store, "bounded_access.config_total"), nullptr);
  ASSERT_NE(TestUtility::findCounter(stats_store, "server.stats_overflow.counter"), nullptr);
  EXPECT_EQ(TestUtility::findCounter(stats_store, "server.stats_overflow.counter")->value(), 2);
  EXPECT_FALSE(custom_stat_namespaces.registered("bounded_access"));
  EXPECT_FALSE(custom_stat_namespaces.registered("dynamicmodulescustom"));
}

TEST_F(DynamicModuleAccessLogFactoryRustTest, StatsScopeLimitsPreserveLegacyNamespaceRegistration) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});

  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  NiceMock<Server::MockOptions> options;
  ON_CALL(options, concurrency()).WillByDefault(testing::Return(1));
  ON_CALL(context.server_context_, options()).WillByDefault(testing::ReturnRef(options));
  ScopedThreadLocalServerContextSetter setter(context.server_context_);
  Stats::CustomStatNamespacesImpl custom_stat_namespaces;
  ON_CALL(context.server_context_.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));

  const std::string yaml = R"EOF(
dynamic_module_config:
  name: access_log_integration_test
  do_not_close: true
  metrics_namespace: legacy_access
  stats_scope:
    max_counters: 2
logger_name: test_logger
)EOF";
  envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  AccessLog::FilterPtr filter;
  auto access_log = factory_.createAccessLogInstance(proto_config, std::move(filter), context, {});

  ASSERT_NE(access_log, nullptr);
  Stats::Store& store = context.server_context_.serverScope().store();
  EXPECT_NE(TestUtility::findCounter(store, "legacy_access.test_log_count"), nullptr);
  EXPECT_NE(TestUtility::findCounter(store, "legacy_access.config_total"), nullptr);
  EXPECT_TRUE(custom_stat_namespaces.registered("legacy_access"));
}

} // namespace
} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
