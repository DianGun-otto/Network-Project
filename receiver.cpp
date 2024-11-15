// receiver.cpp
#include "common.h"

// 创建接收端Socket
SOCKET createSock()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed!" << std::endl;
        exit(-1);
    }

    sockaddr_in recvAddr;
    memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(RecvPORT);
    recvAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr *)&recvAddr, sizeof(recvAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed!" << std::endl;
        exit(-1);
    }

    std::cout << "Receiver socket initialized and bound to address " << RecvIP << ":" << RecvPORT << std::endl;

    return sock;
}

// 接收端建立连接
bool receiverConnect(SOCKET &sock) {
    Packet synPkt;
    sockaddr_in fromAddr;

    // 等待 SYN
    int recvLen = recvPacket(sock, fromAddr, synPkt, recvLogFile);
    if (recvLen > 0 && synPkt.syn && !synPkt.ack) {
        std::cout << "Received SYN from sender" << std::endl;

        // 发送 SYN-ACK
        synPkt.ackNum = synPkt.seqNum + 1;
        synPkt.seqNum = 0;
        synPkt.syn = true;
        synPkt.ack = true;
        sendPacket(sock, fromAddr, synPkt, recvLogFile);
        std::cout << "Sending SYN-ACK" << std::endl;

        // 等待 ACK
        recvLen = recvPacket(sock, fromAddr, synPkt, recvLogFile);
        if (recvLen > 0 && synPkt.ack && !synPkt.syn) {
            std::cout << "Connection established" << std::endl;
            return true;
        }
    }
    return false;
}

// 接收文件
void receiveFile(SOCKET sock)
{
    Packet pkt;
    uint32_t expectedSeqNum = 1;
    std::ofstream outputFile;
    std::string currentFileName;
    while (true) {
        sockaddr_in fromAddr;
        int recvLen = recvPacket(sock, fromAddr, pkt, recvLogFile);
        if (recvLen > 0) {
            // 接收端断开连接
            if(!pkt.ack && pkt.fin) {
                std::cout << "Received FIN from sender" << std::endl;
                // 发送 FIN + ACK
                pkt.ackNum = pkt.seqNum + 1;
                pkt.seqNum = 0;
                pkt.fin = true;
                pkt.ack = true;
                sendPacket(sock, fromAddr, pkt, recvLogFile);
                std::cout << "Sending ACK and FIN" << std::endl;

                // 等待最终的 ACK
                recvLen = recvPacket(sock, fromAddr, pkt, recvLogFile);
                if (recvLen > 0 && !pkt.fin && pkt.ack) {
                    std::cout << "Connection closed" << std::endl;
                    return;
                }
            }

            // 接收文件数据
            else {            
                // 判断是否为新文件传输
                if (pkt.seqNum == 1) {
                    // 获取文件名
                    currentFileName = pkt.filename;
                    // 拼接文件路径
                    std::string currentFilePath = recvFilePath + currentFileName;
                    // 创建新文件
                    outputFile.close();  // 关闭之前的文件
                    expectedSeqNum = 1;
                    outputFile.open(currentFilePath, std::ios::binary);
                    if (!outputFile.is_open()) {
                        std::cerr << "Error creating output file!" << std::endl;
                        exit(-1);
                    }
                    std::cout << "Created new file: " << currentFileName << std::endl;
                }

                // 写数据到文件
                if (pkt.seqNum == expectedSeqNum) {
                    outputFile.write(pkt.data, pkt.length);
                    totalRecvBytes += pkt.length;  // 累计接收的数据字节数
                    // 发送ACK
                    Packet ackPkt;
                    ackPkt.seqNum = pkt.seqNum;
                    ackPkt.ackNum = pkt.ackNum + 1;
                    //ackPkt.ack = true;
                    sendPacket(sock, fromAddr, ackPkt, recvLogFile); 
                    expectedSeqNum++;
                } else {
                    std::cerr << "Out of order packet received. Expected SeqNum: " << expectedSeqNum << " but got: " << pkt.seqNum << std::endl;
                }
            }
        }
    }
}

int main() 
{ 
    recvLogFile = std::ofstream("recvLog.txt", std::ios::trunc);
    
    SOCKET sock = createSock();
    // 建立连接
    if (!receiverConnect(sock)) {
        std::cerr << "Failed to establish connection" << std::endl;
        return -1;
    }
    
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);// 开始计时
    receiveFile(sock); // 接收文件
    LARGE_INTEGER end;// 获取结束时间
    QueryPerformanceCounter(&end);
    LARGE_INTEGER elapsed;// 计算接收时间
    elapsed.QuadPart = end.QuadPart - start.QuadPart;
    double duration = (elapsed.QuadPart * 1000.0) / frequency.QuadPart;
    double throughput = static_cast<double>(totalRecvBytes/1e6) / (duration/1e3);  // 计算吞吐率,单位：Mbps
    recvLogFile.close();
    // 关闭套接字
    closesocket(sock);
    WSACleanup();

    std::cout << "Receiving duration time: " << duration << "ms" << std::endl;
    std::cout << "Total bytes received: " << totalRecvBytes << " bytes" << std::endl;
    std::cout << "Recv Throughput: " << throughput << " Mbps" << std::endl;
    std::cout << "Receiver finished, socket closed." << std::endl;
    return 0;
}
