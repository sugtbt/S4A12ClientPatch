# ClientPatch

客户端多功能补丁。`ijl15.dll` 是 IJL 兼容代理，首次加载时会自动生成 `ijl15.ini`，按清单直接挂载插件；`Az.dll` 原有的固定 `GameGaurd.dll` 加载链保持不变。

### 自定义补丁功能

- 控制台调试日志
- 自定义收发包
- 阻止客户端启动时最小化其他窗口

```ini
# 86JP.ini

[SystemConfig]
Debug = 0              # 日志 1开启 0关闭
PublicEnable = 0       # 自定义收发包 1开启 0关闭
PublicIP = 127.0.0.1   # PublicEnable=1时生效
```

### 调用链路初识

```text
ijl15.dll → 读取 ijl15.ini → 直接加载清单中的插件（默认 86JP.dll）
    1. 插件路径默认相对 ijl15.dll 所在目录解析，也支持绝对路径
    2. `Early` 插件立即加载
    3. `AfterGameGaurd` 插件等待现有 Az.dll 加载 GameGaurd.dll 后加载

86JP.dll → 读取 86JP.ini → 安装自身功能 Hook
            → Hook kernel32!GetStartupInfoW
            → 主模块地址 dnf_base + 0x04AE71A5，触发执行PluginEntry()
                1. Hook 游戏日志函数、SendMessageW窗口消息
                2. DelayHook 延迟线程(等待GameGuard.dll)
                    - 数据包解密 Proxy_CipherEncrypt
                    - 游戏日志 ProxyGameLog
                    - Hook收发包 ws2_32!inet_addr
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
