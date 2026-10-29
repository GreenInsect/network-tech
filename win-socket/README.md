# Windows TCP Socket 示例

本示例使用 C++ 直接调用 Windows Winsock 2 API（`socket`、`bind`、`listen`、`accept`、`connect`、`send`、`recv`），不依赖 Python 或第三方网络库。服务端与客户端可双向发送 UTF-8 文本；服务端一次接受一个客户端。

## 编译

使用 Visual Studio Developer Command Prompt：

```bat
cl /std:c++17 /EHsc server.cpp ws2_32.lib /Fe:server.exe
cl /std:c++17 /EHsc client.cpp ws2_32.lib /Fe:client.exe
```

或使用已安装 MinGW-w64 的 PowerShell：

```powershell
g++ -std=c++17 -Wall -Wextra server.cpp -o server.exe -lws2_32
g++ -std=c++17 -Wall -Wextra client.cpp -o client.exe -lws2_32
```

## 两台主机连接

1. 在服务器主机启动：`server.exe 5000`。默认监听所有 IPv4 网卡。
2. 在服务器主机运行 `ipconfig`，找到当前局域网适配器的 IPv4 地址，例如 `192.168.1.20`。
3. 在客户端主机运行：`client.exe 192.168.1.20 5000`，将地址替换为服务器主机的实际地址。
4. 两端终端都可以输入文字发送。输入 `/quit` 结束会话。

若 Windows 防火墙询问是否允许通信，仅对“专用网络”允许；若客户端仍无法连接，在防火墙高级设置中为服务器添加入站 TCP `5000` 端口规则。两台电脑需能互相访问同一局域网，客户端不能使用 `127.0.0.1` 连接另一台主机。

## TCP 消息格式

TCP 是字节流，没有内置消息边界。示例在每条 UTF-8 消息后追加换行符，接收端保留未读数据并按换行符拆帧，因此一次 `recv` 收到半条或多条消息都能正确处理。代码使用明文通信，仅适用于可信任的局域网，不要直接暴露到互联网。
