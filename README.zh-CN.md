# Stackies

[英文版](README.md)

Stackies 是一款使用 **C++20、Qt 6 和 SQLite** 构建的轻量级桌面便签软件。你可以把便签放在独立窗口中，也可以将它们合并成带有彩色侧边标签的**便签组**，再通过**展开视图**一次查看和整理整组便签。

Stackies 面向 **Windows 和 Linux**。便签数据保存在本地，无需注册账号。

## 主要功能

- **自由组合便签：** 拖动合并、调整标签顺序、跨便签组移动，或随时把一页分离为独立窗口。
- **展开视图：** 在玻璃风格的画布中浏览整个便签组，并重新排列或拖出便签。
- **富文本编辑：** 文字样式、文字颜色、项目符号、编号列表和待办清单。
- **外观设置：** 预设与自定义纸张颜色、便签组透明度、窗口置顶。
- **便签管理：** 隐藏窗口不等于删除；误删的便签可从“最近删除”中恢复。
- **多语言界面：** English、简体中文、繁體中文、日本語、한국어、Español。

## 安装与运行

- **Windows（x64）：** 解压便携版 ZIP，运行 `Stackies.exe`。如果系统提示缺少 Microsoft Visual C++ 运行库，可运行随包提供的 `vc_redist.x64.exe`。
- **Linux（x86_64）：** 给 AppImage 添加执行权限（`chmod +x Stackies-*.AppImage`）后启动。建议使用 X11/`xcb`，以获得更完整的窗口管理体验。

也可以按照下方的[平台与源码构建](#平台与源码构建)说明自行编译。

## 快速上手

1. 在系统托盘菜单选择 **新建独立便签**，创建一张独立便签。
2. 把一张便签的标题栏拖到另一张便签上，停留约 **800 ms**，看到合并提示后松开，即可形成便签组。
3. 通过侧边彩色标签切换便签；拖动标签可以调整顺序、移动到别的便签组或拆分出去。
4. 点击便签组标题栏的网格图标，进入**展开视图**。
5. 点击标题栏的 **×** 只会隐藏窗口；可用托盘菜单 **显示所有便签** 重新显示。

### 常用操作

| 功能 | 操作 |
| --- | --- |
| 在便签组内新建便签 | 标题栏 **+** |
| 编辑正文 | 铅笔图标或双击正文；按 **Ctrl+Enter** 结束编辑 |
| 合并便签 | 将标题栏拖到另一便签上，出现合并提示后松开 |
| 排序、移动、分离 | 拖动彩色侧边标签或展开视图中的卡片 |
| 更改纸张颜色或透明度 | 右键标题栏或侧边标签 |
| 修改文字颜色和格式 | 编辑工具栏 |
| 置顶或取消置顶 | 标题栏图钉 |
| 隐藏便签组 | 标题栏 **×**（不会删除便签） |
| 删除便签 | 右键标题栏或侧边标签 → **删除便签…** |
| 恢复已删除便签 | 托盘菜单 → **最近删除…** |
| 切换界面语言 | 托盘菜单 → **语言**，重启后生效 |
| 修改数据库位置 | 托盘菜单 → **修改数据位置…** |

被删除的便签会保留在**最近删除**中，直到用户主动永久删除。软件也会记住窗口位置、可见状态、标签顺序等工作区设置。

## 数据与隐私

所有便签和便签组都保存在本地 SQLite 数据库 `notes.sqlite` 中。保存便签不需要账号或外部服务。

| 系统 | 默认数据目录 |
| --- | --- |
| Windows | `%APPDATA%\Stackies` |
| Linux | `~/.local/share/Stackies`（或系统配置的 XDG 数据目录） |

通过 **修改数据位置…** 可将数据库迁移至其他目录，包括原目录的父目录或子目录。Stackies 会先保存并验证数据库副本，再切换路径、退出程序，重启后从新位置读取。如果目标目录已有 Stackies 数据库，可选择**合并**（保留两边的便签）、**切换**（使用目标数据库并保留原数据库）或**覆盖**（用当前数据库替换目标数据库）。合并会保留不同版本的便签、便签组和回收站内容；覆盖操作会先在目标目录创建带时间标记的数据库备份。点击**恢复默认位置** 可恢复系统默认目录。

运行期间可能出现 SQLite 的 `-wal` 和 `-shm` 辅助文件。手动备份或搬运之前，请**完全退出 Stackies**，并保留尚未清理的相关 SQLite 文件。如果数据库放在 Google Drive 等同步文件夹中，请避免两台电脑同时运行，切换设备前确认同步已完成。文件夹同步不等于支持多人实时访问的数据库服务。

Stackies 运行期间每 **30 秒**检查一次数据库修订号。如果另一台设备的修改已经同步到本机，并且当前没有正在编辑或尚未保存的内容，便签窗口会自动更新。保存前也会核对修订号，防止旧内容覆盖新内容。**这不是双设备同时编辑的同步系统**：请一次只在一台设备上编辑，切换前确认云端文件已经同步完成。如检测到修改冲突，Stackies 会保留外部同步的最新数据库，同时将本机改动过的便签另存为**独立的冲突副本**。副本保存成功后可继续使用或正常退出，不需要手动操作恢复数据库。同一数据目录只允许运行一个 Stackies 实例；重复启动会唤醒已打开的程序（若便签全部隐藏，则恢复显示其中一张）。

用户在界面中指定的目录优先于 `STACKIES_DATA_DIR` 环境变量；后者仅作为开发和测试时可选的备用设置。

### 诊断日志

日志和崩溃报告始终保存在**本机目录**，不会跟着数据库移动：

- Windows：`%LOCALAPPDATA%\Stackies\logs\app.log`；崩溃报告保存在 `%LOCALAPPDATA%\Stackies\crashes`。
- Linux：`~/.local/state/Stackies/logs/app.log`（也可由 `XDG_STATE_HOME` 指定）。

正常运行只记录警告和错误；需要详细日志时可添加启动参数 `--debug`。托盘菜单不提供日志目录入口。

## 平台与源码构建

**依赖：** CMake 3.21+、C++20、SQLite3，以及 Qt 6.4+（Core、Gui、Widgets、Svg、Network、LinguistTools）。

### Windows

安装 Visual Studio 2022 C++ 工具、Qt 6 MSVC x64、Python 3 和 SQLite3（例如通过 vcpkg）。示例：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md" `
  "-DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64"
cmake --build build --config Release
```

### Linux

以 Debian/Ubuntu 系列发行版为例：

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libsqlite3-dev libx11-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/Stackies
```

Linux 构建目录包含用于可选桌面集成的 `stackies.svg` 和 `stackies.desktop`。便携包打包脚本还会收集运行依赖和许可证材料。

桌面集成是可选操作。若使用普通便携目录，可以把 `stackies.desktop` 复制到 `~/.local/share/applications/`，并将其中 `Exec=` 改为 `run.sh` 的绝对路径（使用系统 Qt 构建则可指向 `Stackies`），将 `Icon=` 改为 `stackies.svg` 的绝对路径。AppImage 自身包含桌面元数据，但直接运行不会自动将其注册到应用列表。

**Linux 限制：** X11/`xcb` 对窗口定位和置顶的支持通常更完整；原生 Wayland 受合成器限制，部分窗口操作和拖动合并功能会受限。不同桌面环境的行为可能有差异。

### 测试

已具备 Qt 构建环境时，可运行核心与界面回归测试：

```bash
ctest --test-dir build --output-on-failure
```

也可以在不安装 Qt 的情况下单独运行核心测试：

```bash
cmake -S . -B build-test -DSTICKY_BUILD_UI=OFF -DBUILD_TESTING=ON
cmake --build build-test
ctest --test-dir build-test --output-on-failure
```

## 许可证

Stackies 采用 [MIT License](LICENSE) 开源。Qt 动态库和 Tabler 图标等第三方组件仍遵循各自的许可证。

## 致谢

- 使用 [Qt](https://www.qt.io/) 和 [SQLite](https://www.sqlite.org/) 构建。
- 工具栏图标来自 [Tabler Icons](https://tabler.io/icons)，采用 MIT 许可证；详见[图标许可证](assets/tabler/LICENSE)。
- 其他依赖的许可证信息见[第三方组件说明](THIRD_PARTY_NOTICES.md)。
