Fixed a safe-Rust use-after-free in the dynamic modules SDK by making ``EnvoyChildSpan::finish``
consume the child-span handle.
