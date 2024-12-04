//common.h
#ifndef COMMON_H
#define COMMON_H
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <ctime>
#include <vector>
#include <deque>

#pragma comment(lib, "ws2_32.lib")

#define SendIP "127.0.0.1"
#define SendPORT 12345
#define RecvIP "127.0.0.1"
#define RecvPORT 8080
#define TIMEOUT_DURATION 1 // 重传等待时间，单位：ms
#define BUFFER 1024 // 数据大小，单位:byte
#define windowSize 16 // 滑动窗口大小

std::ofstream sendLogFile, recvLogFile;
std::string testFilePath = "testfile/"; // 测试文件的存储路径
std::string recvFilePath = "recvfile/"; // 接收文件的存储路径
uint64_t totalSentBytes = 0;// 发送端传输的总字节数
uint64_t totalRecvBytes = 0;// 发送端接收到的总字节数

// 定义数据包格式
struct Packet {
    uint32_t seqNum;       // 序列号
    uint32_t ackNum;       // 确认号
    uint32_t length;       // 数据长度
    char filename[256];    // 文件名
    char data[BUFFER];       // 数据
    uint32_t checksum;     // 校验和
    bool syn = false;      // SYN标志
    bool ack = false;      // ACK标志
    bool fin = false;      // FIN标志
};

// 计算校验和
uint32_t calculateChecksum(Packet pkt) {
    uint32_t sum = 0;
    uint16_t *data = reinterpret_cast<uint16_t*>(&pkt.data);
    size_t length = sizeof(pkt.data);
    // 计算所有16位字的数据的和
    for (size_t i = 0; i < length / 2; i++) {
        sum += data[i];
    }
    // 如果结构体大小是奇数，则处理最后一个字节
    if (length % 2 != 0) {
        sum += reinterpret_cast<uint8_t*>(&pkt.data)[length - 1];
    }
    // 把溢出部分加回到sum中
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    // 取反（补码）得到校验和
    return ~sum;
}

// 打开文件
std::ifstream openInputFile(std::string filepath)
{  
    std::ifstream inputFile(filepath, std::ios::binary);
    if (!inputFile.is_open()) {
        std::cerr << "File open failed!" << std::endl;
        exit(-1);
    }
    std::cout << "Input file opened: " << filepath << std::endl;
    return inputFile;
}

//日志输出
void logInfo(std::ofstream &logFile, const std::string& message, Packet& pkt, double duration) {
    // 特殊处理 SYN, ACK, FIN包
    if (pkt.syn && !pkt.ack) {
        logFile << message << "[SYN] packet - ";
    } else if (pkt.syn && pkt.ack) {
        logFile << message << "[SYN-ACK] packet - ";
    } else if (pkt.ack && pkt.fin) {
        logFile << message << "[FIN-ACK] packet - ";
    } else if (pkt.fin && !pkt.ack) {
        logFile << message << "[FIN] packet - ";
    } else if (pkt.ack && !pkt.syn && !pkt.fin) {
        logFile << message << "[ACK] packet - ";
    } else {
        logFile << message << "[Data] packet - "
                << "Filename: " << pkt.filename
                << ", SeqNum: " << pkt.seqNum 
                << ", AckNum: " << pkt.ackNum
                << ", Length: " << pkt.length
                << ", windowSize: " << windowSize
                << ", Checksum: " << pkt.checksum
                 << ", ";
    }
    logFile << "Time: " << duration << "ms\n";
}

// 发送数据包函数
void sendPacket(SOCKET sock, sockaddr_in &sockaddr_in, Packet &pkt, std::ofstream &logFile) {
    pkt.checksum = calculateChecksum(pkt);// 计算校验和
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);// 获取开始时间
    int sentBytes = sendto(sock, (char *)&pkt, sizeof(pkt), 0, (sockaddr *)&sockaddr_in, sizeof(sockaddr_in));  
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);// 获取结束时间
    LARGE_INTEGER elapsed;
    elapsed.QuadPart = end.QuadPart - start.QuadPart;
    double duration = (elapsed.QuadPart * 1000.0) / frequency.QuadPart;
    logInfo(logFile, "Sent ", pkt, duration); // 填写日志
}

// 接收数据包并校验校验和
int recvPacket(SOCKET sock, sockaddr_in &fromAddr, Packet &pkt, std::ofstream &logFile) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER start;
    int addrLen = sizeof(fromAddr);
    QueryPerformanceCounter(&start);
    int recvLen = recvfrom(sock, (char *)&pkt, sizeof(pkt), 0, (sockaddr *)&fromAddr, &addrLen);
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);// 获取结束时间
    LARGE_INTEGER elapsed;
    elapsed.QuadPart = end.QuadPart - start.QuadPart;
    double duration = (elapsed.QuadPart * 1000.0) / frequency.QuadPart;
    logInfo(logFile, "Received ", pkt, duration);
    if (recvLen > 0) {
        // 计算接收到的数据包的校验和
        uint32_t checksum = calculateChecksum(pkt);
        if (pkt.checksum != checksum) {
            std::cerr << "Checksum mismatch: packet corrupted" << std::endl;
            return -1;  // 校验和不匹配，返回错误
        }
    }
    return recvLen;
}

#endif
