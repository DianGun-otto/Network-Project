//sender.cpp
#include "common.h"

// 初始化发送端Socket
void initialSock(SOCKET &sock, sockaddr_in& recvAddr)
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed!" << std::endl;
        exit(-1);
    }
    
    sockaddr_in sendAddr;
    memset(&sendAddr, 0, sizeof(sendAddr));
    sendAddr.sin_family = AF_INET;
    sendAddr.sin_port = htons(SendPORT);
    sendAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr *)&sendAddr, sizeof(sendAddr)) == SOCKET_ERROR) {
    std::cerr << "Bind failed!" << std::endl;
    exit(-1);
    }
    std::cout << "Sender socket initialized and bound to address " << SendIP << ":" << SendPORT << std::endl;

    memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(RecvPORT);
    recvAddr.sin_addr.s_addr = inet_addr(RecvIP);
    std::cout << "Send destination address " << RecvIP << ":" << RecvPORT << std::endl;
}

// 发送端建立连接
bool senderConnect(SOCKET &sock, sockaddr_in& recvAddr)
{
    Packet synPkt;
    synPkt.seqNum = 0;
    synPkt.ackNum = 0;
    synPkt.syn = true;
    
    sendPacket(sock, recvAddr, synPkt, sendLogFile);
    std::cout << "Sent SYN, waiting for SYN-ACK..." << std::endl;

    Packet ackPkt;
    sockaddr_in fromAddr;

    int recvLen = recvPacket(sock, fromAddr, ackPkt, sendLogFile);
    if (recvLen > 0 && ackPkt.syn && ackPkt.ack) {

        std::cout << "Received SYN-ACK from receiver" << std::endl;

        // 发送 ACK
        ackPkt.seqNum = ackPkt.ackNum;
        ackPkt.ackNum = ackPkt.seqNum + 1;
        ackPkt.syn = false;
        ackPkt.ack = true;

        sendPacket(sock, recvAddr, ackPkt, sendLogFile);

        std::cout << "Sent ACK, connection established" << std::endl;

        return true;
    }
    return false;
}

// 发送端断开连接
bool senderDisconnect(SOCKET &sock, sockaddr_in& recvAddr)
{
    // 发送 FIN 包，表示传输结束
    Packet pkt;
    pkt.fin = true;
    pkt.ack = false;

    sendPacket(sock, recvAddr, pkt, sendLogFile);
    std::cout << "Sent FIN, waiting for ACK..." << std::endl;

    // 等待接收端的 ACK
    sockaddr_in fromAddr;

    int recvLen = recvPacket(sock, fromAddr, pkt, sendLogFile);
    if (recvLen > 0 && pkt.fin && pkt.ack) {
        std::cout << "Received ACK and FIN" << std::endl;
        // 发送 ACK 以确认连接关闭
        pkt.fin = false;
        pkt.ack = true;
        sendPacket(sock, recvAddr, pkt, sendLogFile);
        std::cout << "Sending final ACK, connection closed" << std::endl;
        return true;
    }
    return false;
}

void sendFile(SOCKET sock, sockaddr_in& recvAddr, std::ifstream& inputFile, const std::string& fileName)
{
    uint32_t seqNum = 1;
    uint32_t ackNum = 0;
    char buffer[BUFFER];
    Packet pkt;

    // 传输文件名（仅在第一次发送时）
    strncpy(pkt.filename, fileName.c_str(), sizeof(pkt.filename) - 1);
    pkt.filename[sizeof(pkt.filename) - 1] = '\0';  // 确保文件名以NULL结尾

    //int retries = 0;
    bool ackReceived = false;
    
    // Reno 拥塞控制变量
    cwnd = 1; // 拥塞窗口，初始为 1
    ssthresh = SSTHRESH;  // 初始的慢启动阈值
    uint32_t dupAckCount = 0;  // 用于跟踪重复ACK的次数
    bool fastRetransmitFlag = false; // 快速重传标志

    // 循环发送数据
    while (inputFile) {
        std::vector<Packet> windowPackets;

        // 构造一个数据包窗口，直到文件末尾
        for (uint32_t i = 0; i < cwnd && (inputFile.read(buffer, sizeof(buffer)) || inputFile.gcount() > 0); i++) {
            int bytesRead = inputFile.gcount();
            pkt.seqNum = seqNum + i;
            pkt.ackNum = ackNum + i;
            pkt.length = bytesRead;
            memcpy(pkt.data, buffer, bytesRead);

            windowPackets.push_back(pkt);
            totalSentBytes += pkt.length;  // 累计传输的数据字节数
        }

        // 发送窗口内的所有数据包
        for (auto &pkt : windowPackets) {
            sendPacket(sock, recvAddr, pkt, sendLogFile);  // 发送数据包
        }

        // 等待确认（直到所有数据包发送完才开始接收ACK）
        int windowBase = seqNum;
        int ackCount = 0;  // 用于统计接收到的ACK数量

        // 等待ACK的过程
        while (ackCount < windowPackets.size()) {
            Packet ackPkt;
            sockaddr_in fromAddr;
            bool timedOut = false;
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            LARGE_INTEGER start;
            QueryPerformanceCounter(&start);  // 获取开始时间
            
            // 只在发送完所有数据包后开始接收ACK
            while (true) {
                int recvLen = recvPacket(sock, fromAddr, ackPkt, sendLogFile);
                LARGE_INTEGER end;
                QueryPerformanceCounter(&end);  // 获取结束时间
                LARGE_INTEGER elapsed;
                elapsed.QuadPart = end.QuadPart - start.QuadPart;
                double duration = (elapsed.QuadPart * 1000.0) / frequency.QuadPart;

                // ACK确认机制
                if (recvLen > 0) {
                    // 如果收到的是预期范围内的ACK
                    if (ackPkt.ackNum >= windowBase && ackPkt.ackNum < windowBase + windowPackets.size()) {
                        ackNum = ackPkt.ackNum;  // 更新ACK号
                        seqNum = ackPkt.ackNum + 1;
                        windowBase = ackPkt.ackNum + 1;  // 滑动窗口
                        ackCount++;

                        // 慢启动阶段
                        if (cwnd < ssthresh) {
                            cwnd++;  // 慢启动阶段，cwnd每接收到一个ACK就增加1
                        } 

                        // 如果收到了足够的ACK，跳出等待
                        if (ackCount >= windowPackets.size()) {
                            if(cwnd >= ssthresh && cwnd < MAX_WINDOW_SIZE) {
                                cwnd++; // 拥塞避免阶段,每个RTT,cwnd加1
                            }
                            break;
                        }
                    } 
                    // 如果收到的是重复ACK，执行快速重传
                    else if (ackPkt.ackNum == ackNum) {
                        dupAckCount++;
                        if (dupAckCount >= 3) {
                            std::cout << "Fast retransmit triggered. Retransmitting window." << std::endl;
                            fastRetransmitFlag = true;
                            break;
                        }
                    } 

                    else {
                        std::cout << "Received out-of-order ACK. Expected: " << windowBase << " but got: " << ackPkt.ackNum << std::endl;
                    }
                }

                // 超时判断
                if (duration > TIMEOUT_DURATION) {
                    std::cout << "Timeout waiting for ACK. Retransmitting window..." << std::endl;
                    timedOut = true;
                    break;
                }
            }

            if (timedOut || fastRetransmitFlag) {
                ssthresh = cwnd/2; // 更新ssthresh为cwnd的一半
                for (auto &pkt : windowPackets) {
                    //if (pkt.seqNum > ackNum)  // 重传未被确认的数据包
                        sendPacket(sock, recvAddr, pkt, sendLogFile);
                }
                cwnd = 1; // 回到慢启动阶段
                dupAckCount = 0;  // 重置重复ACK计数器
                fastRetransmitFlag = false;
            } else {
                break;
            }
        }

        // if (retries >= MAX_RETRIES) {
        //     std::cerr << "Max retries reached. File transmission failed." << std::endl;
        //     return;  // 终止传输
        // }

        // 如果文件已经读取完，跳出循环
        if (inputFile.eof() && ackCount == windowPackets.size()) {
            break;
        }
    }

    std::cout << "File " << fileName << " transmission completed!" << std::endl;
}



int main(int argc, char* argv[]) 
{
    if (argc < 2) {
        std::cerr << "Usage: sender <file1> <file2> ... <fileN>" << std::endl;
        return -1;
    }

    sendLogFile = std::ofstream("sendLog.txt", std::ios::trunc);
    SOCKET sock;
    sockaddr_in recvAddr;
    initialSock(sock, recvAddr);
    
    // 建立连接
    if (!senderConnect(sock, recvAddr)) {
        std::cerr << "Failed to establish connection" << std::endl;
        return -1;
    }
    
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);// 开始计时
    // 逐个发送文件
    for (int i = 1; i < argc; i++) {
        std::string testFilename = argv[i];
        std::string inputFilePath = testFilePath + testFilename;
        std::ifstream inputFile = openInputFile(inputFilePath);
        
        sendFile(sock, recvAddr, inputFile, testFilename);

        // 关闭文件后继续下一个文件传输
        inputFile.close();
    }
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);// 获取结束时间
    LARGE_INTEGER elapsed;// 计算传输时间
    elapsed.QuadPart = end.QuadPart - start.QuadPart;
    double duration = (elapsed.QuadPart * 1000.0) / frequency.QuadPart;
    double throughput = static_cast<double>(totalSentBytes/1e6) / (duration/1e3);  // 计算吞吐率,单位：Mbps
    // 断开连接
    if (!senderDisconnect(sock, recvAddr)) {
    std::cerr << "Failed to destroy connection" << std::endl;
    return -1;
    }
    sendLogFile.close();
    closesocket(sock);// 关闭套接字
    WSACleanup();

    //日志输出
    std::cout << "Sending duration time: " << duration << "ms" << std::endl;
    std::cout << "Total bytes transmitted: " << totalSentBytes << " bytes" << std::endl;
    std::cout << "Send Throughput: " << throughput << " Mbps" << std::endl;
    std::cout << "Sender finished, socket closed." << std::endl;
    return 0;
}
