Added stream-owned child tracing spans to the dynamic modules HTTP ABI. The Rust SDK can retain
child spans across callbacks, select OpenTelemetry span kinds, and skip work when a span is not
recording. This updates the dynamic modules ABI version to ``v0.2.0``.
