# ijl15 代理加载器

`ijl15.dll` 保留当前客户端导入的 IJL 兼容导出，并从 `ijl15.ini` 加载插件。
加载器与 `Az.dll` 独立，现有的 `Az.dll -> GameGaurd.dll` 固定加载链保持不变。

设计参考 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
（MIT 许可证）所采用的代理 DLL 和运行时插件加载方案。本实现只保留当前客户端
需要的精简 x86 IJL 兼容接口。

```ini
[Loader]
Enabled=1
Debug=0
PluginCount=1
WaitTimeoutMs=15000

[Plugins]
Plugin0=86JP.dll
Plugin0Stage=Early
```

如果插件必须在固定的 Az/GameGaurd 加载完成后运行，请使用
`AfterGameGaurd` 阶段。加载器会等待 `GameGaurd.dll` 出现，再对配置路径调用
`LoadLibraryW`；加载器本身不会加载 `GameGaurd.dll`。
