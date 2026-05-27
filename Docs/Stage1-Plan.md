# practice-engine — Vulkan 기반 모듈식 C++ 게임 엔진 설계

## Context

`/home/sungyoon/Workspace/practice-engine`는 빈 디렉토리이며, 그린필드로 게임 엔진을 설계한다. 동기는 다음과 같다.

- **목표**: Vulkan 백엔드 기반 C++ 게임 엔진. Unreal 스타일의 완전한 모듈식 구조, RAII 엄수, 백엔드(RHI) 교체 가능.
- **확정된 제약**:
  - C++20, CMake (≥3.26), 1단계 타깃 Win64 + Linux (이후 macOS/MoltenVK, 모바일/콘솔 확장)
  - 모듈/플러그인은 동적 라이브러리(.dll/.so)로 로드 — UE의 `IModuleInterface` 패턴
  - RHI는 인터페이스로 분리, Vulkan만 1차 구현, D3D12/Metal은 슬롯만 확보
  - 오브젝트 모델: Unreal식 Actor/Component **공개 API** + 내부 **ECS** 스토리지 (Unity DOTS 하이브리드)
  - C++ 단일 언어, 게임플레이 모듈은 **핫 리로드** 가능
  - 에디터는 설계 범위 포함, 1단계 구현은 제외
- **산출물 범위**: 엔진 전체 아키텍처 + 1단계(부트스트랩) 구현 체크리스트 — `practice-engine` 실행 시 동적 로드된 VulkanRHI 모듈로 1280×720 창에 삼각형이 렌더링되는 것이 1단계 종료 조건.

---

## 1. 전체 아키텍처 개요

```
+----------------------------------------------------------+
|  Game / Gameplay Modules  (hot-reloadable)               |
+----------------------------------------------------------+
|  Editor (post-Stage-1, ImGui-based)                      |
+----------------------------------------------------------+
|  Renderer / Render Graph                                  |
+----------------------------------------------------------+
|  RHI Interface  (handle-based, explicit sync)            |
+----------------------------------------------------------+
|  VulkanRHI module  |  D3D12RHI (TBD)  |  MetalRHI (TBD)  |
+----------------------------------------------------------+
|  ApplicationCore (window/input) | ShaderCompiler          |
+----------------------------------------------------------+
|  Core  (logging, assert, modules, ECS, math, paths)      |
+----------------------------------------------------------+
|  Launch  (thin bootstrap, static-linked)                 |
+----------------------------------------------------------+
```

핵심 분리 원칙:
- **Renderer는 Vulkan 헤더를 참조하지 않는다** (CI에서 `ldd`/`dumpbin`으로 강제).
- **VulkanRHI는 Launch/exe의 링크-타임 의존성이 아니다** (런타임 `dlopen`).
- **에디터는 모든 런타임 모듈 위의 선택적 레이어**로만 존재한다.

---

## 2. 모듈 시스템 설계

### 2.1 ABI 경계 규칙 (DLL/공유라이브러리 간)

핫 리로드 시 vtable이 안전하게 교체되어야 하고, Windows에서 CRT 인스턴스가 모듈마다 다를 수 있다는 점을 전제로 다음을 **강제**한다.

1. 모듈당 단 두 개의 `extern "C"` export: `CreateModule_<Name>`, `DestroyModule_<Name>`.
2. 경계를 넘는 타입은 **순수 추상 인터페이스**(데이터 멤버/인라인/템플릿 금지)와 엔진 정의 POD value 타입(`EngineStringView`, `EngineSpan<T>`, `EngineResult`, `EngineGuid`)뿐.
3. 경계 너머로 STL 컨테이너/`std::string` 사용 금지, 예외 전파 금지, RTTI/`dynamic_cast` 금지.
4. 메모리는 할당자 인터페이스(`IEngineAllocator*`)를 명시적으로 전달; 호스트 할당 → 호스트 해제 원칙.
5. 인터페이스 식별은 엔진 정의 `EngineInterfaceId`(qualified name의 FNV-1a 64-bit 해시) — RTTI 의존 없음.
6. 모든 public 모듈 헤더 최상단에 `<EngineAbi.hpp>` 포함 — STL/예외/RTTI 노출 감지 시 `#error`.

### 2.2 핵심 인터페이스 (C++ 의사 코드)

```cpp
// EngineAbi.hpp
struct EngineResult        { int32_t code; uint32_t facility; constexpr bool ok() const { return code == 0; } };
struct EngineStringView    { const char* data; uint64_t size; };
template<class T> struct EngineSpan { T* data; uint64_t size; };
struct IEngineAllocator {
    virtual void* Allocate(uint64_t size, uint32_t align) = 0;
    virtual void  Free(void* p, uint64_t size, uint32_t align) = 0;
protected: ~IEngineAllocator() = default;
};

// IModule.hpp
enum class ELoadingPhase : uint16_t {
    EarliestPossible, PreDefault, Default, PostDefault,
    PreLoadingScreen, Game, PostEngineInit
};
struct ModuleDependency { EngineStringView name; uint32_t min_version; bool optional; };
struct ModuleDescriptor {
    EngineStringView name; uint32_t version; ELoadingPhase phase;
    EngineSpan<const ModuleDependency> dependencies;
    bool hot_reloadable;  // gameplay = true; RHI/Core = false
};

class IModule {
public:
    virtual const ModuleDescriptor& GetDescriptor() const noexcept = 0;
    virtual EngineResult StartupModule(class IEngineContext*) = 0;
    virtual EngineResult ShutdownModule() = 0;
    virtual EngineResult PreReload()  { return {}; }
    virtual EngineResult PostReload() { return {}; }
    virtual void* QueryInterface(EngineInterfaceId) noexcept = 0;
protected: ~IModule() = default;
};

extern "C" {
    using PFN_CreateModule  = IModule* (*)(IEngineAllocator*, uint32_t abi);
    using PFN_DestroyModule = void     (*)(IModule*);
}

#define DECLARE_ENGINE_MODULE(ModuleClass, ModuleName) /* C export factory pair */
```

### 2.3 모듈 레지스트리/로더 (Core 내부)

- 플러그인 매니페스트(.uplugin 유사) 스캔 → 디스크립터 수집.
- 의존성 그래프 빌드 → Tarjan SCC로 **사이클 검출 실패 시 즉시 로드 거부**.
- `ELoadingPhase` 별로 topo-sorted 순서 로드.
- 핫 리로드 시 DLL을 **shadow-copy** (`Game.<gen>.dll`)하여 빌드 산출물을 잠그지 않음.
- 언로드 안전 계약: 모듈은 `Shutdown/PreReload` 반환 전 모든 델리게이트, 스케줄러 시스템 함수 포인터, RHI 핸들, 스폰 스레드를 회수해야 한다. 레지스트리는 콜백 테이블의 주소 범위가 모듈에 남아 있으면 리로드를 중단.

### 2.4 핫 리로드 전략 — 핸들 인다이렉션 (ECS 백킹)

```
RELOAD_REQUESTED
  → DRAIN_FRAME            (현재 프레임 fence 대기)
  → PRE_RELOAD             (델리게이트 회수)
  → UNREGISTER_SYSTEMS     (스케줄러 system fn 제거)
  → CHECK_SCHEMA           (ComponentSchemaHash 비교)
        호환    → 그대로 진행
        불호환  → 영향받는 컴포넌트를 인메모리 아카이브로 직렬화
  → UNLOAD_OLD_DLL  (Destroy_* → dlclose)
  → LOAD_NEW_DLL    (shadow copy → dlopen → Create_*)
  → REBIND_FN_PTRS  (Core가 archetype 메타의 construct/destruct/move/serialize 재바인딩)
  → RESTORE_DATA    (불호환 경로면 역직렬화)
  → POST_RELOAD     → RESUME
```

컴포넌트 데이터는 **항상 Core 소유의 ECS 아카이브에 거주**, 모듈은 타입 디스크립터와 시스템 함수 포인터만 등록한다. `ComponentTypeId`는 등록 순서가 아닌 qualified name의 안정 해시 — 리로드 후에도 같은 아키타입에 매핑된다.

### 2.5 트레이드오프 결정

| 결정 | 채택 | 이유 |
|---|---|---|
| 핸들 vs `shared_ptr<IRHIResource>` | **타입드 핸들 (idx+gen)** | bindless 렌더링, 핫 리로드 생존, 8바이트, generation으로 dangling 안전 검출 |
| 핫 리로드: 핸들 vs 직렬화 라운드트립 | **핸들 인다이렉션 (ECS 백킹)** | ECS 구조가 이미 필요로 하는 규율과 동일, 직렬화는 schema 변경 시 fallback |
| 배리어: render graph vs RHI 자동 추적 | **render graph 계산, RHI는 emit만** | multi-queue/bindless에서 자동 추적은 보수적 또는 부정확 |
| 셰이더 언어 | **HLSL + DXC → SPIR-V/DXIL** | D3D12 백엔드까지 단일 corpus, `[[vk::*]]` 어트리뷰트 비용 수용 |
| ABI 엄격성 | **strict (헤더 #error 가드)** | 수다스러움 vs Windows CRT 미스매치 silent corruption — 후자가 치명적 |
| 동기화 primitive | **타임라인 fence 기본 + 스왑체인용 binary semaphore** | D3D12 fence와 클린 매핑, 스왑체인 OS 제약만 별도 |

---

## 3. RHI 추상화 설계

### 3.1 두-티어 구조

- **RHI**: 얇고 명시적, 자동 배리어 없음. 백엔드별 1:1 매핑이 자연스러운 vocabulary만 노출.
- **RenderGraph (Renderer 내부)**: 패스 I/O 선언, 배리어 자동 계산, 트랜션트 리소스 관리. RHI에 `BeginRenderPass`를 호출하기 직전에 모든 결정 완료.

### 3.2 RHI는 모듈이다

```cpp
class IRHIBackendModule : public IModule {
public:
    virtual EngineSpan<const struct RHIAdapterInfo> EnumerateAdapters() = 0;
    virtual EngineResult CreateDevice(const RHIDeviceCreateDesc&, IRHIDevice** out) = 0;
};
```

Launch는 config/probe로 `"VulkanRHI"`를 이름으로 요청 → 레지스트리가 dlopen → `QueryInterface(IRHIBackendModule::kInterfaceId)` → `CreateDevice` 호출. Renderer는 `IRHIDevice*`만 받는다.

### 3.3 타입드 핸들

```cpp
template<class Tag> struct RHIHandle {
    uint32_t index; uint32_t generation;
    constexpr bool valid() const { return generation != 0; }
};
using RHIBufferHandle      = RHIHandle<BufferTag>;
using RHITextureHandle     = RHIHandle<TextureTag>;
using RHISamplerHandle     = RHIHandle<SamplerTag>;
using RHIPipelineHandle    = RHIHandle<PipelineTag>;
using RHIShaderHandle      = RHIHandle<ShaderTag>;
using RHIFenceHandle       = RHIHandle<FenceTag>;
using RHISemaphoreHandle   = RHIHandle<SemaphoreTag>;
using RHICommandListHandle = RHIHandle<CommandListTag>;
using RHISwapchainHandle   = RHIHandle<SwapchainTag>;
using RHIBindGroupLayoutHandle = RHIHandle<BindGroupLayoutTag>;
using RHIBindGroupHandle       = RHIHandle<BindGroupTag>;
```

`Destroy*`는 frame fence에 키된 deferred-delete 큐로 enqueue. GPU가 해당 프레임을 마치면 슬롯이 회수되고 generation이 증가. 디버그용 이름 테이블은 dev 빌드 한정으로 `device->DebugName(handle)` 제공.

### 3.4 디바이스/커맨드리스트 인터페이스

```cpp
class IRHIDevice {
public:
    virtual const RHIDeviceCaps& GetCaps() const noexcept = 0;
    virtual uint32_t GetMaxFramesInFlight() const noexcept = 0;

    // 리소스 생성 (실패는 generation==0)
    virtual RHIBufferHandle      CreateBuffer(const RHIBufferDesc&) = 0;
    virtual RHITextureHandle     CreateTexture(const RHITextureDesc&) = 0;
    virtual RHISamplerHandle     CreateSampler(const RHISamplerDesc&) = 0;
    virtual RHIShaderHandle      CreateShader(const RHIShaderDesc&) = 0;
    virtual RHIPipelineHandle    CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&) = 0;
    virtual RHIPipelineHandle    CreateComputePipeline(const RHIComputePipelineDesc&) = 0;
    virtual RHIBindGroupLayoutHandle CreateBindGroupLayout(const RHIBindGroupLayoutDesc&) = 0;
    virtual RHIBindGroupHandle   CreateBindGroup(const RHIBindGroupDesc&) = 0;
    virtual RHIFenceHandle       CreateFence(uint64_t initial) = 0;
    virtual RHISemaphoreHandle   CreateSemaphore() = 0;
    virtual RHISwapchainHandle   CreateSwapchain(const RHISwapchainDesc&) = 0;

    virtual void Destroy(RHIBufferHandle) = 0;   // 각 핸들 타입별 오버로드
    /* ... */

    virtual RHICommandListHandle AcquireCommandList(ERHIQueueType) = 0;
    virtual IRHICommandList*     Lock(RHICommandListHandle) = 0;
    virtual EngineResult         Submit(const RHISubmitInfo&) = 0;
    virtual EngineResult         Present(RHISwapchainHandle, RHISemaphoreHandle wait) = 0;
    virtual EngineResult         BeginFrame() = 0;
    virtual EngineResult         EndFrame() = 0;

    virtual void* MapBuffer(RHIBufferHandle, uint64_t off, uint64_t size) = 0;
    virtual void  UnmapBuffer(RHIBufferHandle) = 0;

    virtual EngineResult SignalFence(RHIFenceHandle, uint64_t v) = 0;
    virtual EngineResult WaitFence(RHIFenceHandle, uint64_t v, uint64_t timeout_ns) = 0;
    virtual uint64_t     GetFenceValue(RHIFenceHandle) = 0;
protected: ~IRHIDevice() = default;
};

class IRHICommandList {
public:
    virtual void Begin() = 0; virtual void End() = 0;
    virtual void BeginRenderPass(const RHIRenderPassBeginInfo&) = 0;
    virtual void EndRenderPass() = 0;
    virtual void ResourceBarrier(EngineSpan<const RHITextureBarrier>) = 0;
    virtual void ResourceBarrier(EngineSpan<const RHIBufferBarrier>) = 0;
    virtual void SetPipeline(RHIPipelineHandle) = 0;
    virtual void SetBindGroup(uint32_t set, RHIBindGroupHandle, EngineSpan<const uint32_t> dyn_off) = 0;
    virtual void SetViewport(const RHIViewport&) = 0;
    virtual void SetScissor(const RHIRect&) = 0;
    virtual void SetVertexBuffers(uint32_t first, EngineSpan<const RHIBufferHandle>, EngineSpan<const uint64_t>) = 0;
    virtual void SetIndexBuffer(RHIBufferHandle, uint64_t off, ERHIIndexType) = 0;
    virtual void SetPushConstants(uint32_t off, uint32_t size, const void*) = 0;
    virtual void Draw(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) = 0;
    virtual void DrawIndirect(RHIBufferHandle, uint64_t, uint32_t, uint32_t) = 0;
    virtual void Dispatch(uint32_t, uint32_t, uint32_t) = 0;
    virtual void DispatchIndirect(RHIBufferHandle, uint64_t) = 0;
    virtual void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) = 0;
    virtual void CopyBufferToTexture(const RHIBufferTextureCopy&) = 0;
    virtual void CopyTexture(const RHITextureCopy&) = 0;
    virtual void BeginDebugLabel(EngineStringView, uint32_t rgba) = 0;
    virtual void EndDebugLabel() = 0;
protected: ~IRHICommandList() = default;
};
```

### 3.5 무엇을 가리고 무엇을 노출하나

- **숨김** (백엔드 내부): `VkDescriptorSet/Pool`, `VkRenderPass/Framebuffer`(Vulkan 1.3 dynamic rendering 사용), `VkPipelineLayout`, queue family/memory type, command pool.
- **노출** (보편 추상): `ERHIPipelineStage`, `ERHIAccess`, `ERHITextureLayout`, `ERHIFormat` — render graph가 배리어 계산에 필요한 vocabulary.

### 3.6 동기화

- 기본: **타임라인 fence** (`VkSemaphore timeline` ↔ `ID3D12Fence`).
- 스왑체인 acquire/present만 **binary semaphore** — OS가 강제하는 부분만 좁게 사용.
- `BeginFrame`에서 다음 슬롯 fence 대기, `EndFrame`에서 시그널. Deferred-delete 큐는 이 fence를 키로 사용.

### 3.7 셰이더 컴파일러도 모듈

`IShaderCompiler` 인터페이스를 갖는 `ShaderCompiler` 모듈(`hot_reloadable=false`, phase `PreDefault`). HLSL → DXC → SPIR-V/DXIL + 엔진 정의 리플렉션 blob. 쿡된 빌드에서는 미리 컴파일된 .spv/.dxil + reflection sidecar를 적재하고, 모듈 자체는 plugin manifest에서 제거하여 shipping에서 떼낼 수 있다.

---

## 4. 디렉토리 레이아웃

```
practice-engine/
├── CMakeLists.txt
├── CMakePresets.json            # linux-{debug,debug-asan,release}, win64-{debug,release}, *-monolithic
├── .editorconfig .gitignore .clang-format .clang-tidy README.md LICENSE
│
├── Engine/
│   ├── Source/
│   │   ├── Runtime/             # 게임에 포함되는 모듈
│   │   │   ├── Core/            # 로깅/assert/타입/모듈/델리게이트/ECS/수학/경로
│   │   │   ├── RHI/             # 인터페이스만 (얇은 SHARED)
│   │   │   ├── VulkanRHI/       # 구현 (런타임 dlopen, 링크-타임 의존성 X)
│   │   │   ├── ApplicationCore/ # GLFW 창/입력
│   │   │   ├── Renderer/        # RHI만 사용, Vulkan 헤더 도달 불가
│   │   │   └── Launch/          # STATIC, LaunchEngineLoop 제공
│   │   ├── Editor/              # (예약, 1단계 비어 있음)
│   │   ├── Developer/           # (예약, dev 전용 모듈 자리)
│   │   └── Programs/
│   │       ├── practice-engine/ # 실행 파일: main → LaunchEngineLoop
│   │       └── ShaderCompiler/  # (예약)
│   └── Shaders/
│       ├── Private/Triangle.{vert,frag}.glsl
│       └── Public/
│
├── Plugins/                     # 자기완결 트리, Engine/Source 레이아웃 미러
├── ThirdParty/                  # FetchContent를 못 쓸 때만 vendoring
├── Config/DefaultEngine.ini     # (스텁)
├── Saved/   Intermediate/   Binaries/<Platform>/<Config>/
├── CMake/
│   ├── EngineModule.cmake       # add_engine_module()
│   ├── CompilerWarnings.cmake   # set_engine_warnings()
│   ├── Sanitizers.cmake         # apply_engine_sanitizers()
│   ├── Platform.cmake           # Win64 / Linux / Mac 감지
│   └── ThirdPartyDeps.cmake     # FetchContent 중앙화
└── Docs/Architecture.md
```

각 모듈의 표준 내부 구조:
```
<Module>/
├── CMakeLists.txt     # add_engine_module(...) 한 줄
├── Public/<Module>/*.h
├── Private/*.cpp *.h
└── Tests/*.test.cpp   # 옵션
```

### 1단계 모듈 목록

| 모듈 | 위치 | 타입 | 의존성 (Stage 1) |
|---|---|---|---|
| Core | Runtime/Core | SHARED | spdlog, fmt, glm |
| RHI | Runtime/RHI | SHARED | Core |
| VulkanRHI | Runtime/VulkanRHI | SHARED | Core, RHI, Vulkan SDK, volk, VMA |
| ApplicationCore | Runtime/ApplicationCore | SHARED | Core, GLFW |
| Renderer | Runtime/Renderer | SHARED | Core, RHI |
| Launch | Runtime/Launch | STATIC | Core, RHI, ApplicationCore, Renderer |
| practice-engine | Programs/practice-engine | EXE | Launch |

---

## 5. CMake 구조

### 5.1 모듈 선언 함수 — `add_engine_module`

`*.Build.cmake` 같은 UBT 흉내는 도입하지 않는다 (이유: 메타-빌드 도구 한 개 작성하는 비용이 1단계 가치를 압도). 모듈별 `CMakeLists.txt`는 다음 한 줄로 축약된다.

```cmake
# Engine/Source/Runtime/Renderer/CMakeLists.txt
add_engine_module(
    NAME         Renderer
    TYPE         SHARED
    PUBLIC_DEPS  Engine::Core Engine::RHI
    PCH_HEADER   Public/Renderer/RendererPCH.h)
```

`CMake/EngineModule.cmake` 책임:
- `Public/`, `Private/` 자동 글로빙(`CONFIGURE_DEPENDS`).
- 타깃 별칭 `Engine::<Module>` 생성.
- `ENGINE_MONOLITHIC=ON`이면 SHARED → STATIC으로 강제 변환.
- `generate_export_header`로 `<MODULE>_API` 매크로 생성 + 모듈 내부에서 사용할 `MODULE_API` private 컴파일 정의 주입.
- `CXX_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN ON`.
- PCH, unity build 옵션 적용.
- `set_engine_warnings()`, `apply_engine_sanitizers()` 호출.
- `install(TARGETS ... EXPORT EngineTargets)` 등록.

### 5.2 런타임 모듈 위치

- 출력 디렉토리: `Binaries/<Platform>/<Config>/` (CMAKE_RUNTIME/LIBRARY_OUTPUT_DIRECTORY).
- 실행 파일과 .dll/.so 공존 배치 → Windows는 application directory 룰, Linux는 `RPATH=$ORIGIN`.

### 5.3 옵션과 프리셋

- 옵션: `ENGINE_BUILD_TESTS`, `ENGINE_BUILD_EDITOR`(1단계 OFF), `ENGINE_MONOLITHIC`, `ENGINE_ENABLE_UNITY_BUILD`, `ENGINE_ENABLE_PCH`, `ENGINE_ENABLE_{ASAN,UBSAN,TSAN}`, `ENGINE_WARNINGS_AS_ERRORS`, `ENGINE_VULKAN_VALIDATION`.
- CMakePresets: `linux-debug`, `linux-debug-asan`, `linux-release`, `win64-debug`, `win64-release`, `linux-debug-monolithic`.

### 5.4 서드파티 (1단계)

| 의존성 | 전략 |
|---|---|
| Vulkan SDK | `find_package(Vulkan REQUIRED)` (시스템 설치) |
| volk | FetchContent(pinned tag), `GIT_SHALLOW` |
| VMA | FetchContent(pinned tag) |
| GLFW | `find_package(glfw3 QUIET)` → 실패 시 FetchContent |
| spdlog | FetchContent(pinned tag), bundled fmt 사용 |
| glm | FetchContent(pinned tag) |
| GoogleTest | FetchContent (테스트 빌드 시에만) |

모든 `FetchContent_Declare`는 `CMake/ThirdPartyDeps.cmake`에 중앙화. 모듈별 CMakeLists는 직접 호출하지 않는다.

---

## 6. 1단계(부트스트랩) 구현 체크리스트

각 단계는 독립적으로 commit 가능. 평행 에이전트 두 곳에서 검증된 인터페이스를 그대로 사용.

### a. 리포 스캐폴딩
- 루트 파일 7종(`CMakeLists.txt`, `CMakePresets.json`, `.editorconfig`, `.gitignore`, `.clang-format`, `.clang-tidy`, `README.md`).
- `CMake/` 5개 스크립트.
- 디렉토리 트리(빈 곳은 `.gitkeep`).
- **종료 조건**: `cmake --preset linux-debug` 성공 (등록된 모듈 0).

### b. Core 모듈
- `Public/Core/`: `CoreMinimal.h`, `Logging.h` (spdlog 래퍼 + `DECLARE_LOG_CATEGORY`), `Assert.h` (`ENGINE_CHECK/VERIFY/FATAL`, 예외 없음), `Types.h`, `Delegate.h` (단일/멀티캐스트), `Module.h` (위 §2.2의 IModule + `DECLARE_ENGINE_MODULE`), `Paths.h` (실행 파일 기준 경로), `EngineAbi.hpp` (§2.1의 가드 헤더).
- spdlog는 콘솔 sink + `Saved/Logs/`의 회전 파일 sink.
- 모듈 레지스트리 구현(§2.3): 디스크립터 수집, Tarjan SCC, phase별 topo 로드, shadow-copy, dlopen/dlclose 래퍼(Win32: `LoadLibraryW`/`GetProcAddressA`).
- **종료 조건**: `Tests/Core.smoke.cpp`가 `[info] [LogCore] Core online`을 stdout과 파일에 남기고 exit 0. ASan clean.

### c. RHI 인터페이스 + VulkanRHI 동적 로드
- `Runtime/RHI`: §3.4의 인터페이스 헤더만 (대부분 헤더). 작은 enum/desc 지원 코드.
- `Runtime/VulkanRHI`: `DECLARE_ENGINE_MODULE(FVulkanRHIModule, VulkanRHI)`. `StartupModule`에서 `volkInitialize` → `VkInstance`(Debug + `ENGINE_VULKAN_VALIDATION` 시 validation layer) → physical device 선택 → logical device + graphics/present queue → 자신을 `IRHIBackendModule`로 등록.
- VkDebugUtilsMessenger 콜백을 등록, `ERROR` 심각도에서 `ENGINE_FATAL`.
- 스크래치 테스트 프로그램이 `"VulkanRHI"`를 이름으로 요청 → 로그 `[VulkanRHI] Device created: <GPU>`.
- **종료 조건**: 테스트 exe는 어떤 Vulkan 심볼과도 직접 링크되지 않는다 (ldd/dumpbin 검증).

### d. ApplicationCore (창/입력)
- GLFW로 `IWindow` 구현. `GLFW_NO_API` 힌트.
- `GetRequiredVulkanInstanceExtensions()`로 인스턴스 확장 노출.
- 입력은 1단계 최소: `Escape`에서 종료.
- **종료 조건**: 스크래치 앱이 1280×720 창을 열고 Escape/X에 깔끔히 종료.

### e. Renderer가 삼각형 그리기
- Renderer 모듈에 Vulkan 헤더 도달 불가 — CI grep으로 강제.
- `FRenderer::{Init,RenderFrame,Shutdown}`. 내부에서 RHI만 호출.
- 셰이더는 `Engine/Shaders/Private/Triangle.{vert,frag}.glsl`. CMake configure-time custom command (`glslc` 호출)로 `.spv` 생성, `Binaries/.../Shaders/`로 복사.
- 스왑체인 생성 → 정점 버퍼(3개) → 그래픽스 파이프라인 → 커맨드 버퍼 → clear color `(0.1, 0.1, 0.15, 1.0)` → 삼각형 draw → present.
- **종료 조건**: 통합 하니스에서 validation 오류 0, 프레임 1회 생성·제출·present 성공.

### f. Launch가 모두 묶기
- `Launch/Public/Launch/LaunchEngineLoop.h`: `int LaunchEngineLoop(int argc, char** argv); void RequestEngineExit(const char* reason);`.
- 시퀀스: Core init → 모듈 레지스트리 init → `"VulkanRHI"` 로드 → 디바이스 획득 → 창 생성 → `FRenderer` init → 루프(`PollEvents/RenderFrame/ShouldClose`) → 역순 종료.
- `Programs/practice-engine/Private/Main.cpp`는 단순히 `LaunchEngineLoop` 호출.
- Windows 1단계는 console subsystem 유지 (`WIN32_EXECUTABLE OFF`) — 로그 가시성 우선.

---

## 7. 검증 계획 (End-to-End)

### 7.1 명령

```bash
# Linux
cmake --preset linux-debug
cmake --build --preset linux-debug -j
./Binaries/Linux/Debug/practice-engine

# Windows (Developer Command Prompt)
cmake --preset win64-debug
cmake --build --preset win64-debug --config Debug
.\Binaries\Win64\Debug\practice-engine.exe

# Sanitizer
cmake --preset linux-debug-asan
cmake --build --preset linux-debug-asan -j
ASAN_OPTIONS=detect_leaks=1 ./Binaries/Linux/Debug/practice-engine

# 모놀리식 빌드 (옵션 검증)
cmake --preset linux-debug-monolithic
cmake --build --preset linux-debug-monolithic -j
./Binaries/Linux/Debug/practice-engine
```

### 7.2 자동 게이트 (CI에서도 동일)

```bash
# Renderer가 Vulkan과 링크/심볼 갖지 않음
ldd Binaries/Linux/Debug/libRenderer.so | grep -qi vulkan && echo FAIL || echo OK
nm -D --defined-only Binaries/Linux/Debug/libRenderer.so | grep -qi '^.\+ T vk' && echo FAIL || echo OK

# VulkanRHI가 exe의 링크-타임 의존성이 아님
ldd Binaries/Linux/Debug/practice-engine | grep -qi vulkanrhi && echo FAIL || echo OK

# Validation ERROR 없음
grep -c 'VK_DEBUG.*ERROR' Saved/Logs/engine.log  # 0이어야 함
```

### 7.3 1단계 통과 기준 (모두 true)

1. Linux(Ubuntu 24.04, Mesa/벤더 드라이버)와 Windows 11에서 `practice-engine` 실행.
2. 1280×720, 타이틀 `"practice-engine"`, 지정한 clear color 위 삼각형.
3. `VulkanRHI`가 동적으로 로드됨 (위 ldd/dumpbin 게이트 통과).
4. Renderer에 Vulkan 심볼 없음 (위 nm/dumpbin 게이트 통과).
5. Debug+validation 10초 실행 동안 ERROR 심각도 0건.
6. ASan run 종료 시 리포트 0건.
7. `linux-debug-monolithic` 빌드도 동일하게 삼각형 실행.
8. `run-clang-tidy Engine/Source/Runtime` exit 0, clang-format diff 없음.
9. 두 플랫폼 삼각형 스크린샷이 `Docs/screenshots/stage1-{linux,windows}.png`에 commit.

---

## 8. 1단계 이후 로드맵 (참고용, 비-구현)

- **2단계**: RenderGraph(자동 배리어/transient resource), Asset System(메모리 매핑된 pak), 입력 매핑, 시간/틱.
- **3단계**: ECS 본격화(archetype 스토리지, system scheduler, 병렬 잡), 게임플레이 모듈 핫 리로드 실증.
- **4단계**: ImGui 기반 에디터 (`Engine/Source/Editor/{EditorCore, EditorWidgets, AssetBrowser, LevelEditor}`).
- **5단계**: macOS(MoltenVK 또는 MetalRHI 별도), D3D12RHI 구현 — 기존 RHI 인터페이스를 변경 없이 충족하는지 검증.
- **6단계**: 모바일/콘솔 PAL(Platform Abstraction Layer) 분리, 셰이더 쿡, shipping 구성.

---

## Critical Files (1단계 작성 대상)

- `/home/sungyoon/Workspace/practice-engine/CMakeLists.txt`
- `/home/sungyoon/Workspace/practice-engine/CMakePresets.json`
- `/home/sungyoon/Workspace/practice-engine/CMake/EngineModule.cmake` — `add_engine_module()` 핵심
- `/home/sungyoon/Workspace/practice-engine/CMake/ThirdPartyDeps.cmake` — FetchContent 중앙화
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Runtime/Core/Public/Core/EngineAbi.hpp` — ABI 가드와 POD value 타입
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Runtime/Core/Public/Core/Module.h` — `IModule`, `DECLARE_ENGINE_MODULE`, factory 계약
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Runtime/Core/Private/ModuleRegistry.cpp` — 의존성 그래프, Tarjan, shadow-copy, dlopen
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Runtime/RHI/Public/RHI/IRHIDevice.h` — 디바이스/커맨드리스트 인터페이스
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Runtime/RHI/Public/RHI/RHITypes.h` — 핸들, enum, desc
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Runtime/VulkanRHI/Private/VulkanRHIModule.cpp` — 모듈 진입점, instance/device 생성, debug messenger
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Runtime/ApplicationCore/Private/GLFWWindow.cpp`
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Runtime/Renderer/Private/Renderer.cpp`
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp`
- `/home/sungyoon/Workspace/practice-engine/Engine/Source/Programs/practice-engine/Private/Main.cpp`
- `/home/sungyoon/Workspace/practice-engine/Engine/Shaders/Private/Triangle.vert.glsl`
- `/home/sungyoon/Workspace/practice-engine/Engine/Shaders/Private/Triangle.frag.glsl`
