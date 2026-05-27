add_engine_executable(
    NAME dummy_module_smoke
    DEPS Engine::Core
)

# The smoke test must NOT link DummyModule directly - it must dlopen it at runtime.
# Establish an ordering dependency so DummyModule is built before this exe runs.
add_dependencies(dummy_module_smoke DummyModule)
