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

// 发送文件
void sendFile(SOCKET sock, sockaddr_in& recvAddr, std::ifstream& inputFile, const std::string& fileName)
{
    uint32_t seqNum = 1;
    uint32_t ackNum = 0;
    char buffer[1024];
    Packet pkt;
    
    // 传输文件名（仅在第一次发送时）
    strncpy(pkt.filename, fileName.c_str(), sizeof(pkt.filename) - 1);
    pkt.filename[sizeof(pkt.filename) - 1] = '\0';  // 确保文件名以NULL结尾

    while (inputFile.read(buffer, sizeof(buffer)) || inputFile.gcount() > 0) {
        int bytesRead = inputFile.gcount();
        pkt.seqNum = seqNum;
        pkt.ackNum = ackNum;
        pkt.length = bytesRead;
        memcpy(pkt.data, buffer, bytesRead);

        int retries = 0;
        bool ackReceived = false;

        while (retries < MAX_RETRIES && !ackReceived) {
            // 发送数据包

            sendPacket(sock, recvAddr, pkt, sendLogFile);

            // 等待ACK：设置计时器
            Packet ackPkt;
            sockaddr_in fromAddr;
            bool timedOut = false;

            // 等待ACK && 超时重传机制
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            LARGE_INTEGER start;
            QueryPerformanceCounter(&start);// 获取开始时间

            while (true) {
                int recvLen = recvPacket(sock, fromAddr, ackPkt, sendLogFile);
                LARGE_INTEGER end;
                QueryPerformanceCounter(&end);// 获取结束时间
                LARGE_INTEGER elapsed;
                elapsed.QuadPart = end.QuadPart - start.QuadPart;
                double duration = (elapsed.QuadPart * 1000.0) / frequency.QuadPart;

                // ACK确认机制
                if (recvLen > 0) {
                    if (ackPkt.seqNum == seqNum) {
                        ackNum = ackPkt.ackNum;  // 更新ACK号
                        seqNum++;
                        ackReceived = true;
                        break;
                    }
                    // 接收到的ACK序号不一致，说明接收错误，重新传输
                    else {
                        std::cout << "Received out-of-order ACK. Expected: " << seqNum << " but got: " << ackPkt.seqNum << std::endl;
                    }
                }

                // 超时判断
                if (duration > TIMEOUT_DURATION) {
                    std::cout << "Timeout waiting for ACK. Retransmitting packet..." << std::endl;
                    timedOut = true;
                    break;
                }
            }

            // 超时且未接收到ACK确认
            if (!ackReceived && timedOut) {
                retries++;
                // 重传次数已达到最大重传次数
                if (retries >= MAX_RETRIES) {
                    std::cerr << "Max retries reached. File transmission failed." << std::endl;
                    return; // 终止传输，认为传输失败
                }
            }
        }   
    }
    std::cout << "File " << fileName << " transmission completed!" << std::endl;
    //logFile.close();
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
    // 逐个发送文件
    for (int i = 1; i < argc; i++) {
        std::string testFilename = argv[i];
        std::string inputFilePath = testFilePath + testFilename;
        std::ifstream inputFile = openInputFile(inputFilePath);
        
        sendFile(sock, recvAddr, inputFile, testFilename);

        // 关闭文件后继续下一个文件传输
        inputFile.close();
    }

    // 断开连接
    if (!senderDisconnect(sock, recvAddr)) {
    std::cerr << "Failed to destroy connection" << std::endl;
    return -1;
    }

    sendLogFile.close();
    // 关闭套接字
    closesocket(sock);
    WSACleanup();
    std::cout << "Sender finished, socket closed." << std::endl;
    return 0;
}
