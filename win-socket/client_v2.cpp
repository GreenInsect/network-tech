#define WIN32_LEAN_AND_MEAN
// Mingw-w64 的 ws2tcpip.h 只在 _WIN32_WINNT >= 0x0600 时才声明 inet_pton，
// 而该头文件默认按较低的系统版本裁剪 API，所以必须显式指定。
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <process.h>  // _beginthreadex
#include <string>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif

constexpr int kBufferSize = 4096;

bool receiveLine(SOCKET socket, std::string& pending, std::string& line) {
    for (;;) {
        const std::size_t newline = pending.find('\n');
        if (newline != std::string::npos) {
            line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }

        char buffer[kBufferSize];
        const int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return false;
        }
        pending.append(buffer, received);
    }
}

bool sendLine(SOCKET socket, const std::string& message) {
    const std::string framed = message + "\n";
    std::size_t sent = 0;
    while (sent < framed.size()) {
        const int result = send(socket, framed.data() + sent,
                                static_cast<int>(framed.size() - sent), 0);
        if (result == SOCKET_ERROR || result == 0) {
            return false;
        }
        sent += result;
    }
    return true;
}

// 接收线程需要的数据。本机 MinGW 是 win32 线程模型，
// libstdc++ 没有编译 gthreads，std::thread 不可用，因此使用 _beginthreadex。
struct ReceiverContext {
    SOCKET socket;
    std::atomic<bool>* quitting;
};

unsigned __stdcall receiverThread(void* raw) {
    ReceiverContext* context = static_cast<ReceiverContext*>(raw);
    std::string pending;
    std::string line;
    while (receiveLine(context->socket, pending, line)) {
        if (line == "/quit") {
            break;
        }
        std::cout << "服务器: " << line << '\n';
    }
    if (!context->quitting->load()) {
        std::cout << "服务器已断开。\n";
    }
    return 0;
}

int main(int argc, char* argv[]) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2 || argc > 3) {
        std::cerr << "用法: client.exe <服务器IPv4地址> [端口]\n";
        return 1;
    }
    int port = 5000;
    if (argc == 3) {
        char* end = nullptr;
        const long parsed = std::strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || parsed < 1 || parsed > 65535) {
            std::cerr << "端口必须是 1 到 65535 的整数\n";
            return 1;
        }
        port = static_cast<int>(parsed);
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup 初始化失败\n";
        return 1;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "socket 创建失败，错误码: " << WSAGetLastError() << '\n';
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, argv[1], &serverAddress.sin_addr) != 1) {
        std::cerr << "无效的 IPv4 地址: " << argv[1] << '\n';
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "connect 失败，错误码: " << WSAGetLastError() << '\n';
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "已连接到服务器 " << argv[1] << ':' << port << '\n'
              << "输入文字发送；输入 /quit 结束会话。\n";

    std::atomic<bool> quitting{false};
    ReceiverContext context{clientSocket, &quitting};
    const uintptr_t receiver =
        _beginthreadex(nullptr, 0, receiverThread, &context, 0, nullptr);
    if (receiver == 0) {
        std::cerr << "创建接收线程失败\n";
        shutdown(clientSocket, SD_BOTH);
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    std::string text;
    while (std::getline(std::cin, text)) {
        if (text == "/quit") {
            sendLine(clientSocket, "/quit");
            break;
        }
        if (!sendLine(clientSocket, text)) {
            std::cerr << "发送失败，错误码: " << WSAGetLastError() << '\n';
            break;
        }
    }

    quitting.store(true);
    // 先 shutdown 让阻塞中的 recv 返回 0，再等线程退出，最后才能关闭套接字。
    shutdown(clientSocket, SD_BOTH);
    WaitForSingleObject(reinterpret_cast<HANDLE>(receiver), INFINITE);
    CloseHandle(reinterpret_cast<HANDLE>(receiver));
    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
