#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

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

int main(int argc, char* argv[]) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2 || argc > 3) {
        std::cerr << "用法: client.exe <服务器IPv4地址> [端口]\n";
        return 1;
    }
    int port = 5000;
    if (argc == 3) {
        port = std::atoi(argv[2]);
    }
    if (port < 1 || port > 65535) {
        std::cerr << "端口范围必须是 1 到 65535\n";
        return 1;
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
    /*
    clientSocket（第一个参数）：客户端自己刚才通过 socket() 创建的套接字。
    reinterpret_cast<sockaddr*>(&serverAddress)（第二个参数）：
        &serverAddress ：这里存放的是目标服务器的 IP 地址和端口号（和之前服务端的 sockaddr_in 结构完全一样，只不过填的是你要连接的服务器地址，比如 "127.0.0.1" 和端口 5000）。
        强转：和 bind 类似，connect 的原型接收通用地址结构体指针 sockaddr*，所以需要用 reinterpret_cast 进行类型转换。
    sizeof(serverAddress)（第三个参数）：告诉内核目标地址结构体的字节大小。    
    */
    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "connect 失败，错误码: " << WSAGetLastError() << '\n';
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "已连接到服务器 " << argv[1] << ':' << port << '\n'
              << "输入文字发送；输入 /quit 结束会话。\n";

    std::thread receiver([&]() {
        std::string pending;
        std::string line;
        while (receiveLine(clientSocket, pending, line)) {
            if (line == "/quit") {
                break;
            }
            std::cout << "服务器: " << line << '\n';
        }
        std::cout << "服务器已断开。\n";
    });

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

    shutdown(clientSocket, SD_BOTH);
    closesocket(clientSocket);
    receiver.join();
    WSACleanup();
    return 0;
}
