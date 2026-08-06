# GameGaurd

`GameGaurd.dll` 负责客户端兼容补丁和插件加载。运行时使用客户端原版
`ijl15.dll`。

```ini
[Plugins]
Plugin0=PreventMinimize.dll
Plugin1=86JP.dll
```

插件按 `[Plugins]` 中的配置项顺序加载，支持相对路径和绝对路径，最多加载
`64` 个。插件导出 `ClientPatchPluginInit` 时会在加载后调用。

使用 Visual Studio 2022 构建 `GameGaurd.sln` 的 `Release|x86` 配置。
