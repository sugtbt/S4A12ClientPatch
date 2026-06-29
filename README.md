# ClientPatch

客户端多功能补丁

### 补丁功能

- 控制台调试日志
- 公网收包

```cpp
// XLog.h 中
#define DEBUG // 开启日志
#define TARGET_IP "127.0.0.1" // Public_IP
#define TARGET_IP_SLASH TARGET_IP "/"
```

### 部署

```bash
./start-server.sh --server-ip Public_IP
```