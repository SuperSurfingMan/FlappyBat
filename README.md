# Flappy Bat 🦇

一款使用 **C++17 + OpenGL 3.3** 从零实现的 2D 横版飞行小游戏，玩法致敬经典 *Flappy Bird*——只是主角换成了一只小蝙蝠：点击鼠标拍打翅膀，穿过成对出现的尖刺障碍，还可以收集道具、挑战「枭王」Boss。项目不依赖任何现成游戏引擎，渲染、UI、音频、场景管理等系统均为手写，模块划分清晰，适合作为 C++ / OpenGL 2D 游戏开发的入门参考工程。

## 功能特性

- **核心玩法**：鼠标点击控制蝙蝠拍翅上升，躲避上下成对的尖刺障碍，穿隙得分；碰到障碍、地面或天花板即进入死亡演出与结算界面。
- **三种游戏模式**：`Classic`（经典）、`TimeRush`（限时冲刺）、`StormNight`（风暴之夜），由主菜单在进入局内前设定。
- **收集品**：局内随机刷新猕猴桃、蚊子、萤火虫等可收集物，触发对应动画反馈。
- **Boss 战**：「枭王」（OwlKing），基于可扩展的怪物框架（导演 + 怪物登记表）实现。
- **天气演出**：雨滴、地面积水水花、狂风、闪电等特效，由 `WeatherDirector` 统一调度，配合自研 GLSL 着色器与实例化渲染实现。
- **成长与成就**：金币、护盾科技树解锁、成就与累计统计统一持久化到 `player_save.dat`；本地分数排行榜保存于 `score.dat`。
- **完整场景流**：启动画面 → 主菜单 → 局内 → 排行榜 / 成就 / 设置（音量调节、枭王形象批次切换等），支持圆形聚焦转场、暂停/继续与 Toast 提示。
- **中文 UI**：界面文字基于 Dear ImGui + VonwaonBitmap 16px 位图字体渲染。

## 技术栈

| 类别 | 技术 | 说明 |
|------|------|------|
| 语言 | C++17 | 使用 `std::filesystem`、`std::any`、结构化绑定等特性 |
| 图形 API | OpenGL 3.3 Core Profile | 通过 GLAD 加载函数指针，正交投影 2D 渲染 |
| 窗口 / 输入 | GLFW 3.4 | 窗口创建、事件循环、鼠标输入 |
| UI / 文字 | Dear ImGui 1.92.4 | 全部界面绘制与中文文字渲染，含调试面板 |
| 图像加载 | stb_image | PNG 等纹理解码 |
| 音频 | miniaudio | BGM 与音效播放 |
| 着色器 | GLSL | `assets/shaders/` 下的 sprite / rain / wind / lightning 等自研着色器 |
| 构建 | CMake ≥ 3.21 + MinGW-w64 | 目前仅支持 MSYS2 MINGW64 工具链（配置阶段强制校验） |
| 目标平台 | Windows | 链接系统库 `opengl32` / `gdi32` / `user32` |

## 代码结构（`src/`）

```
src/
├── main.cpp            # 程序入口
├── app/                # 组合根：Application 主循环、排行榜、统一存档
├── audio/              # AudioSystem：miniaudio 封装（BGM / 音效）
├── core/               # 基础库：Vec2 数学、Config 全局配置、Input、Timer
├── enemy/              # 怪物框架：导演 + 内置怪物登记 + 枭王实现
├── gameplay/           # 玩法域共享概念：GameMode、StageParams、SAT 碰撞检测
├── layer/              # 屏幕覆盖层：LayerStack（主界面 UI 层 / 结算层 / Toast 层）
├── render/             # GL 基础设施：Shader、VAO/VBO、Texture2D、Animation、
│                       #   SpriteRenderer、InstancedQuadRenderer、雨/风/闪电渲染器
├── res/                # ResourceManager 资源管理 + stb / miniaudio 加载翻译单元
├── scene/              # 场景管理（注册表寻址、圆形聚焦转场）与六大场景
│   └── stage/          # 局内玩法子系统：RunFlow、障碍场、道具场、天气导演、HUD 等
├── sprite/             # 精灵体系：Sprite 基类与蝙蝠 / 按钮 / 收集物等派生
└── ui/                 # 推送式 UI 事件派发内核（UISystem / UIWidget / FontManager 等）
```

整体架构：`Application`（GLFW / GLAD / ImGui 初始化 + 帧循环）驱动 `SceneManager`，场景内组合精灵体系、覆盖层与各玩法子系统；渲染侧为「渲染器 + 槽位分发」管线（见 `src/render/WorldSlot.h`）。

## 第三方依赖

所有第三方库均已收录在仓库的 `vendor/` 目录下，**克隆后无需单独安装**：

| 库 | 版本 | 形式 | 官方仓库 |
|----|------|------|----------|
| GLFW | 3.4.0 | 预编译 `libglfw3.a` + `glfw3.dll`（x64） | [github.com/glfw/glfw](https://github.com/glfw/glfw) |
| glad | OpenGL 3.3 Core | 源码，编译为静态库 `glad` | [github.com/Dav1dde/glad](https://github.com/Dav1dde/glad) |
| Dear ImGui | 1.92.4 | 源码，编译为静态库 `imgui`（含 GLFW / OpenGL3 后端） | [github.com/ocornut/imgui](https://github.com/ocornut/imgui) |
| stb_image | — | 单头文件 `stb_image.h` | [github.com/nothings/stb](https://github.com/nothings/stb) |
| miniaudio | — | 单头文件 `miniaudio.h` | [github.com/mackron/miniaudio](https://github.com/mackron/miniaudio) |

系统级依赖仅 Windows 自带的 `opengl32` / `gdi32` / `user32`。

## 本地编译构建（新手指南）

### 1. 准备环境（Windows）

项目在 CMake 配置阶段强制要求 MinGW-w64 工具链（MSVC 等其他工具链会直接报错退出），推荐使用 [MSYS2](https://www.msys2.org/)：

1. 安装 MSYS2 后打开 **MINGW64** 终端（注意不是 MSYS / UCRT64）。
2. 安装工具链：

   ```bash
   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake
   ```

3. 确认可用：

   ```bash
   g++ --version && cmake --version && mingw32-make --version
   ```

### 2. 克隆与工程布局

```bash
git clone https://github.com/SuperSurfingMan/FlappyBat.git
cd FlappyBat
```

仓库结构：

```
FlappyBat/
├── CMakeLists.txt      # 构建脚本（定义 glad / imgui 静态库与 demo 可执行文件）
├── vendor/             # 第三方库（GLFW / glad / imgui / stb / miniaudio）
├── src/                # 全部 C++ 源码
└── README.md
```

### 3. 配置与编译

在仓库根目录（`CMakeLists.txt` 所在目录）执行：

```bash
# 配置（首次会生成 build 目录）
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 编译（-j 并行）
cmake --build build -j
```

构建产物为 `build/demo.exe`：

- 构建后脚本会自动把 `glfw3.dll` 拷贝到 exe 同目录；
- MinGW 运行时（libgcc / libstdc++ / libwinpthread）已通过 `-static` 静态链接，exe 不依赖 `libstdc++-6.dll` 等 DLL。

### 4. 运行

游戏资源（贴图、音频、着色器）在运行时从工作目录的 `assets/` 目录按相对路径加载，因此**需在工程根目录下启动**：

```bash
./build/demo.exe        # MSYS2 / Git Bash
# 或
build\demo.exe          # cmd / PowerShell
```

窗口大小固定为 540 × 960。首次运行后会在工作目录生成 `player_save.dat`（金币 / 科技树 / 成就 / 音量）与 `score.dat`（排行榜）等存档文件。

### 5. 用 VS Code 开发（可选）

CMake 已开启 `CMAKE_EXPORT_COMPILE_COMMANDS`，构建后会在 `build/compile_commands.json` 生成编译数据库，可直接供 clangd 使用；VS Code 中安装 C/C++ 与 clangd 插件、将编译器路径指向 MSYS2 的 g++（如 `C:/msys64/mingw64/bin/g++.exe`），即可获得精确的代码跳转与补全。

### 常见问题

| 现象 | 原因与解决 |
|------|------------|
| 配置阶段报 `This project is intended to be built with MinGW-w64` | 使用了非 MinGW 工具链，请改在 MSYS2 MINGW64 终端中构建 |
| 提示缺少 `glfw3.dll` | 直接运行了别处拷出的 exe；请运行 `build/` 下的 `demo.exe`（构建时已自动拷贝 DLL） |
| 链接期报 MinGW 运行时符号冲突 | 系统 PATH 中混入了其他 MinGW 环境的旧版 DLL；工程已 `-static` 静态链接运行时，请检查 PATH |
