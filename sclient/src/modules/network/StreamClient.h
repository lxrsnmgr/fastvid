#ifndef __SRC_MODULES_NETWORK_STREAMCLIENT_H__
#define __SRC_MODULES_NETWORK_STREAMCLIENT_H__

namespace sclient{
//流媒体客户端
//支持 TCP、UDP、RTP 三种传输协议：
//- TCP：简单可靠，按消息边界接收
//- UDP：支持分片重组、NACK 重传、FEC 前向纠错和自适应抖动缓冲
//- RTP：基于 UDP，支持 H.264 FU-A 分片重组
class StreamClient{
public:
    StreamClient();
    ~StreamClient();

    //连接到服务器
    bool Connect(const ClientConfig config)
};
}//namespace sclient
#endif
