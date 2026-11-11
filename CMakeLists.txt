cmake_minimum_required(VERSION 3.21)

# ===============================
# Project
# ===============================
project(ImGuiGameDemo LANGUAGES C CXX)

# 导出 compile_commands.json，供 clangd 等工具做精确的语义分析
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# # ===============================
# # Debug & Sanitizer (MinGW)
# # ===============================
# if(MINGW AND CMAKE_BUILD_TYPE STREQUAL "Debug")

#     message(STATUS "Enable AddressSanitizer")

#     add_compile_options(
#         -g
#         -O0
#         -fsanitize=address
#         -fno-omit-frame-pointer
#     )

#     add_link_options(
#         -fsanitize=address
#     )

# endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===============================
# MSYS2 / MinGW sanity check
# ===============================
if(NOT MINGW)
    message(FATAL_ERROR "This project is intended to be built with MinGW-w64 (MSYS2 MINGW64 shell).")
endif()

# ===============================
# Paths
# ===============================
set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/vendor)

set(GLFW_DIR    ${VENDOR_DIR}/GLFW)
set(GLAD_DIR    ${VENDOR_DIR}/glad)
set(IMGUI_DIR   ${VENDOR_DIR}/imgui)
set(STB_DIR     ${VENDOR_DIR}/stb)
set(MINIAUDIO_DIR ${VENDOR_DIR}/miniaudio)

# ===============================
# GLAD (static library)
# ===============================
add_library(glad STATIC
    ${GLAD_DIR}/src/glad.c
)

target_include_directories(glad PUBLIC
    ${GLAD_DIR}/include
)


# ===============================
# Global compile definitions
# ===============================
# add_compile_definitions(IMGUI_IMPL_OPENGL_LOADER_GLAD)

# ===============================
# ImGui (static library)
# ===============================
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLFW_DIR}/include
    # ${GLAD_DIR}/include
)

# 告诉 ImGui backend 使用 glad
# target_compile_definitions(imgui PRIVATE
#     IMGUI_IMPL_OPENGL_LOADER_GLAD
# )

# ===============================
# Executable
# ===============================
add_executable(demo
    # 入口
    src/main.cpp

    # core/ —— 基础库：数学几何（header-only）、通用工具、计时、输入、全局配置
    src/core/Timer.cpp
    src/core/Input.cpp
    src/core/Config.cpp

    # gameplay/ —— 玩法域：模式策略 / 注册表 / 参数表 / SAT 碰撞检测
    src/gameplay/GameMode.h
    src/gameplay/ItemCatalog.h
    src/gameplay/ModeDefinition.h
    src/gameplay/ModeRegistry.h
    src/gameplay/ModeRegistry.cpp
    src/gameplay/ModeRoster.cpp
    src/gameplay/StageParams.h
    src/gameplay/StageParams.cpp
    src/gameplay/Collision.cpp

    # render/ —— GL 渲染基础设施与渲染器（槽位管线见 src/render/WorldSlot.h）
    src/render/Shader.cpp
    src/render/ShaderLibrary.cpp
    src/render/VertexArray.cpp
    src/render/VertexBuffer.cpp
    src/render/RenderState.cpp
    src/render/Texture2D.cpp
    src/render/Animation.cpp
    src/render/SpriteRenderer.cpp
    src/render/InstancedQuadRenderer.cpp
    src/render/RainRenderer.cpp
    src/render/WindRenderer.cpp
    src/render/LightningRenderer.cpp

    # res/ —— 资源管理器 + 第三方库（stb/miniaudio）实现翻译单元
    src/res/ManifestReader.h
    src/res/ResourceCatalog.h
    src/res/ResourceCatalog.cpp
    src/res/ResourceManager.cpp
    src/res/ImageLoader.cpp
    src/res/MiniaudioLoader.cpp

    # sprite/ —— 精灵
    src/sprite/Sprite.cpp
    src/sprite/ButtonSprite.cpp
    src/sprite/BatSprite.cpp
    src/sprite/FadeSprite.cpp
    src/sprite/CollectableSprite.cpp
    src/sprite/ColorfulSprite.cpp

    # ui/ —— UI 事件派发内核（推送式交互：每帧集中命中判定 + 事件分发，见 src/ui/UISystem.h）
    src/ui/UISystem.cpp
    src/ui/InputScope.h

    # scene/ —— 场景与转场
    src/scene/SceneKey.h
    src/scene/SceneManager.cpp
    src/scene/BootScene.cpp
    src/scene/ReadyScene.cpp
    src/scene/MainScene.cpp
    src/scene/ScoreRankScene.cpp
    src/scene/AchievementsScene.cpp
    src/scene/SettingsScene.cpp

    # scene/stage/ —— 局内玩法子系统（StageFrame 快照 + 玩法场/导演/HUD/表现）
    src/scene/stage/RunFlow.cpp
    src/scene/stage/PlayerAbilities.cpp
    src/scene/stage/ObstacleField.cpp
    src/scene/stage/ItemField.cpp
    src/scene/stage/WeatherDirector.cpp
    src/scene/stage/WeatherRendererRoster.h
    src/scene/stage/WeatherRendererRoster.cpp
    src/scene/stage/RainSystem.cpp
    src/scene/stage/FeedbackFx.cpp
    src/scene/stage/StageHud.cpp
    src/scene/stage/ParallaxField.cpp
    src/scene/stage/RunSettlement.cpp

    # layer/ —— 屏幕覆盖层
    src/layer/LayerStack.h
    src/layer/MainSceneUILayer.cpp
    src/layer/MainSceneOverLayer.cpp
    src/layer/ToastLayer.cpp

    # audio/ —— 音频
    src/audio/AudioSystem.cpp

    # enemy/ —— 怪物框架（docs/怪物系统架构设计.md）：导演 + 内置怪物登记 + 具体怪物实现
    src/enemy/EnemyDirector.cpp
    src/enemy/EnemyRoster.cpp
    src/enemy/owlking/OwlKing.cpp        # 状态机与相位编排
    src/enemy/owlking/OwlKingSkills.cpp  # 技能循环 / 弹幕 / 判定
    src/enemy/owlking/OwlKingRender.cpp  # 表现层（动画状态机 / 世界层 / HUD）

    # app/ —— 组合根与玩家数据
    src/app/Application.cpp
    src/app/Services.h
    src/app/Services.cpp
    src/app/ScoreRank.cpp
    src/app/DataIntegrity.h
    src/app/DataIntegrity.cpp
    src/audio/SFXCatalog.h
    src/audio/SFXCatalog.cpp
)

target_include_directories(demo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GLFW_DIR}/include
    ${STB_DIR}
    ${MINIAUDIO_DIR}
)

# ===============================
# GLFW (prebuilt)
# ===============================
target_link_directories(demo PRIVATE
    ${GLFW_DIR}/lib
)

# ===============================
# Link libraries
# ===============================
target_link_libraries(demo PRIVATE
    imgui
    glad
    glfw3
    opengl32
    gdi32
    user32
)

# MinGW: 静态链接全部 MinGW 运行时（libgcc/libstdc++/libwinpthread），
# 避免 exe 依赖 libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll
# （防止系统 PATH 中其他 MinGW 环境的旧版 DLL 导致符号解析失败）
if(MINGW)
    target_link_options(demo PRIVATE -static)
endif()

# ===============================
# Copy glfw3.dll next to exe
# ===============================
add_custom_command(TARGET demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${GLFW_DIR}/lib/glfw3.dll
        $<TARGET_FILE_DIR:demo>
)

# ===============================
# 数据表自测（纯 C++，无第三方框架）
# 覆盖：模式数量一致性、参数表完整性、音效表完整性、score.dat 旧档兼容。
# 运行：cmake --build build_msys --target demo_data_tests && ./build_msys/demo_data_tests
# ===============================
include(CTest)
if(BUILD_TESTING)
    add_executable(demo_data_tests
        tests/data_tables_test.cpp
        src/gameplay/ModeRegistry.cpp
        src/gameplay/ModeRoster.cpp
        src/gameplay/StageParams.cpp
        src/app/ScoreRank.cpp
        src/scene/stage/RunFlow.cpp
        src/scene/stage/RunSettlement.cpp
        src/audio/SFXCatalog.cpp
        src/res/ResourceCatalog.cpp
    )
    target_include_directories(demo_data_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    # 自测需要按绝对路径读取工程内的 manifest（ctest 的工作目录是构建目录）
    target_compile_definitions(demo_data_tests PRIVATE
        FLAPPYBAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    # 与 demo 一致静态链接 MinGW 运行时，保证自测 exe 脱离 MSYS2 PATH 也能直接运行
    if(MINGW)
        target_link_options(demo_data_tests PRIVATE -static)
    endif()
    add_test(NAME data_tables COMMAND demo_data_tests)
endif()
