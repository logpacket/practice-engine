# app_smoke - PAL window-lifecycle test.
# Links the PAL interface + the configure-time-selected backend (ADR-0018).
add_engine_executable(
    NAME app_smoke
    DEPS Engine::Core Engine::ApplicationCore Engine::Platform
)
