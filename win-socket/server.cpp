#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif

constexpr int kBufferSize = 4096;

bool receiveLine(SOCKET socket, std::string& pending, std::string& line) {
    // 只要还没成功凑出一行、或者没遇到断开连接
    for (;;) {
        // 检查缓冲区里有没有现成的完整行
        const std::size_t newline = pending.find('\n');
        if (newline != std::string::npos) {
            line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }
        // 如果缓冲区没有换行符，向网卡索取新数据
        char buffer[kBufferSize];
        const int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return false;
        }
        pending.append(buffer, received);
    }
}

bool sendLine(SOCKET socket, const std::string& message) {
    // 末尾显式加上一个换行符 \n
    const std::string framed = message + "\n";
    // 记录已经成功发送了多少个字节
    std::size_t sent = 0;
    while (sent < framed.size()) {
        /*
        socket（第一个参数）：你要通过哪个连接发数据（这里是客户端的 clientSocket）。
        framed.data() + sent（第二个参数）：
            由于我们在循环中发送，+ sent 表示指针偏移量：如果前面已经成功发了 10 个字节，指针就往后移动 10 位，从第 11 个字节开始发送剩下的内容。
        static_cast<int>(framed.size() - sent)（第三个参数）：
            告诉系统这次还要发送多少个字节（总长度减去已经发了的长度）。
        0（第四个参数）：
            默认标志位，通常直接填 0 即可（表示按常规方式发送）。      
        send 的返回值（int result）代表这次系统调用实际成功发出去多少个字节  
        */
        const int result = send(socket, framed.data() + sent,
                                static_cast<int>(framed.size() - sent), 0);
        if (result == SOCKET_ERROR || result == 0) {
            return false;
        }
        sent += result;
    }
    return true;
}

int main(int argc, char* argv[]) {
    // 控制台编码
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    int port = 5000;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    if (port < 1 || port > 65535) {
        std::cerr << "端口范围必须是 1 到 65535\n";
        return 1;
    }

    // 存储 Windows Sockets 动态链接库初始化信息
    WSADATA wsaData{};
    // 初始化 2.2 版本的 Winsock 网络库
    // 操作系统在初始化成功后，会把底层网络环境的实际信息填入 wsaData
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup 初始化失败\n";
        return 1;
    }

    // AF_INET：使用 IPv4 地址
    // SOCK_STREAM：面向连接的流式套接字（对应 TCP 协议，可靠、有序）
    // SOCK_DGRAM：无连接的数据报套接字（对应 UDP 协议，不可靠、无序但高效）
    // protocol (Protocol)：通常填 0，让操作系统根据前面的 af 和 type 自动推导并选择默认协议（例如 AF_INET + SOCK_STREAM 默认就是 IPPROTO_TCP）
    // 也可以显式指定，如 IPPROTO_TCP 或 IPPROTO_UDP
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        // 获取上一次 Winsock API 调用失败时的具体错误代码
        std::cerr << "socket 创建失败，错误码: " << WSAGetLastError() << '\n';
        // 终止对 Winsock 2 DLL 的使用，并释放它所占用的系统资源
        WSACleanup();
        return 1;
    }

    // 表示 IPv4 地址和端口的核心数据结构
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    // INADDR_ANY == 0 / 0.0.0.0 : 监听本机所有的网络接口
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(static_cast<u_short>(port));

    // 将创建好的套接字（Socket）与指定的 IP 地址和端口号绑定
    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "bind 失败，错误码: " << WSAGetLastError() << '\n';
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }
    // 将绑定的套接字置于“监听（Listening）”状态，开始等待并接收来自客户端的 TCP 连接请求
    // SOMAXCONN: 指定内核为该监听套接字维护的挂起连接队列（Backlog Queue）的最大长度
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen 失败，错误码: " << WSAGetLastError() << '\n';
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "正在监听 TCP 端口 " << port << "，等待客户端连接...\n";
    sockaddr_in clientAddress{};
    int addressLength = sizeof(clientAddress);
    // 阻塞等待并接受一个来自客户端的 TCP 连接请求。当连接成功建立后，它会返回一个全新的套接字（clientSocket），专门用于和该客户端进行后续的双向数据收发
    // listenSocket（监听套接字）
    SOCKET clientSocket = accept(listenSocket, reinterpret_cast<sockaddr*>(&clientAddress), &addressLength);
    closesocket(listenSocket);

    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "accept 失败，错误码: " << WSAGetLastError() << '\n';
        WSACleanup();
        return 1;
    }

    // 准备缓冲区: INET_ADDRSTRLEN：这是一个在 <ws2tcpip.h> 中定义好的宏，专门用来表示 IPv4 字符串的最大长度（其值为 22）。它足够容纳标准点分十进制的 IPv4 地址
    char clientIp[INET_ADDRSTRLEN]{};
    // inet_ntop == Internet Network To Presentation
    /*
    AF_INET：指定协议类型为 IPv4。
    &clientAddress.sin_addr：指向前面 accept 填充的客户端二进制 IP 地址。
    clientIp：目标缓冲区，转换后的字符串将存入这里。
    sizeof(clientIp)：缓冲区的最大字节数，防止内存越界。
    */
    inet_ntop(AF_INET, &clientAddress.sin_addr, clientIp, sizeof(clientIp));
    std::cout << "客户端已连接: " << clientIp << '\n'
              << "输入文字发送；输入 /quit 结束会话。\n";

    // 声明并初始化一个线程安全的原子布尔变量 running
    std::atomic<bool> running{true};
    // 在后台启动一个独立的线程（receiver），专门负责持续接收来自客户端的网络消息，并在连接断开或收到退出指令时，安全地通知主线程结束会话
    std::thread receiver([&]() {
        // pending（积压缓冲区）：因为 TCP 是“面向字节流”的协议，可能会发生粘包或半包现象。这个变量专门用来暂存从 socket 读到但还不够一整行的数据
        std::string pending;
        // line：用来存放通过 receiveLine 成功截取出来的、带换行符完整的一行消息
        std::string line;
        while (receiveLine(clientSocket, pending, line)) {
            if (line == "/quit") {
                break;
            }
            std::cout << "客户端: " << line << '\n';
        }
        running = false;
        std::cout << "客户端已断开。\n";
    });

    std::string text;
    while (running && std::getline(std::cin, text)) {
        if (text == "/quit") {
            sendLine(clientSocket, "/quit");
            break;
        }
        if (!sendLine(clientSocket, text)) {
            std::cerr << "发送失败，错误码: " << WSAGetLastError() << '\n';
            break;
        }
    }

    // 显式切断套接字的发送和接收能力，并向对端发送 TCP 的 FIN（结束）报文
    shutdown(clientSocket, SD_BOTH);
    // ：销毁内核中的套接字对象，彻底释放该连接占用的操作系统资源
    closesocket(clientSocket);
    // 等待后台的 receiver 线程彻底执行完毕并退出，然后由主线程回收它的资源
    receiver.join();
    // 清理并卸载最开始通过 WSAStartup 加载的 Windows 网络库
    WSACleanup();
    return 0;
}
