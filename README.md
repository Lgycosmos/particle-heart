# 粒子爱心 · Particle Heart (Desktop)

一个 Windows 桌面粒子特效小工具：彩色粒子跟随鼠标拖尾，**点击在光标处汇聚成爱心**。

## 功能

| 操作 | 效果 |
|------|------|
| 移动鼠标 | 粒子拖尾跟随光标（全桌面，不限窗口） |
| 左键点击 | 在光标处汇聚成爱心 |
| 爱心状态下再点左键 | 爆散后重新汇聚 |
| 右键 | 回到拖尾状态 |
| `Esc` | 退出程序 |

## 特点

- **全屏透明覆盖层**：桌面照常显示、点击可穿透，不影响正常使用。
- **纯 Win32 GDI**：零第三方依赖，单文件 exe。
- **逐像素 alpha 混合**：自带白热核心 + 彩色光晕。
- **`/MT` 静态链接**：运行库打包进 exe，任何 Windows 10/11 双击即用，无需安装 VC++ 运行库。

## 编译

需要 Visual Studio（MSVC）。打开「开发者命令提示符」，执行：

```bat
cl particle_desktop.c /Fe:particle_desktop.exe /O2 /MT /link user32.lib gdi32.lib /SUBSYSTEM:WINDOWS
```

或使用 MinGW-w64 / MSYS2：

```bat
gcc particle_desktop.c -o particle_desktop.exe -lgdi32 -luser32 -mwindows -O2
```

## 运行

双击 `particle_desktop.exe`。

> 若 Windows 提示「Windows 已保护你的电脑」，点「更多信息」→「仍要运行」。

## 文件说明

| 文件 | 说明 |
|------|------|
| `particle_desktop.c` | 主程序源码（全屏桌面粒子版） |
| `particle_desktop.exe` | 已编译好的程序（`/MT` 静态链接） |

## License

MIT
