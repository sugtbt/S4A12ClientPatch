# ClientPatch

客户端多功能补丁，`ijl15.dll` 使用客户端原版 IJL。`Az.dll` 固定加载
`GameGaurd.dll`，再由 `GameGaurd.dll` 按配置加载其他补丁插件。

### 自定义补丁功能

- 控制台调试日志
- 自定义收发包
- 阻止客户端启动时最小化其他窗口（`PreventMinimize.dll`）

```ini
# 86JP.ini

[SystemConfig]
Debug = 0              # 日志 1开启 0关闭
PublicEnable = 0       # 自定义收发包 1开启 0关闭
PublicIP = 127.0.0.1   # PublicEnable=1时生效
```

### 调用链路初识

```text
Az.dll → 加载 GameGaurd.dll
GameGaurd.dll → 应用客户端兼容补丁 → 读取 GameGaurd.ini → 加载并初始化插件
PreventMinimize.dll → 阻止客户端最小化其他窗口
86JP.dll → 读取 86JP.ini → 安装日志和地址转换 Hook
```

### GameGaurd.dll IDA交叉引用

| Address         | Function     | Instruction                                                  |
| --------------- | ------------ | ------------------------------------------------------------ |
| .text:10043AFB  | sub_10043AC0 | push offset a127001 ; "127.0.0.1"                            |
| .text:10045F3C  | sub_10045EA0 | mov ecx, offset a127001 ; "127.0.0.1"                        |
| .text:100466DE  | sub_10046240 | mov ecx, offset a127001 ; "127.0.0.1"                        |
| .text:10067ADF  | sub_10067990 | push offset a127001_0 ; "127.0.0.1/"                         |
| .rdata:100A291C |              | a127001 db '127.0.0.1',0 ; DATA XREF: sub_10043AC0+3B ↑o     |
| .rdata:100A9510 |              | a127001_0 db '127.0.0.1/',0 ; DATA XREF: sub_10067990+14F ↑o |

### 快速启动

```bash
./start-server.sh --server-ip PublicIP
```
