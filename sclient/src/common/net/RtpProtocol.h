#ifndef __SRC_COMMMON_NET_RTPPROTOCOL_H__
#define __SRC_COMMMON_NET_RTPPROTOCOL_H__

#include <array>
#include <variant>
#include <vector>
#include <cstdint>

namespace sclient{
namespace common{
namespace net{

//RTP 协议常量定义
//RTP 是实时音视频传输的标准协议
//参考 RFC 3550
static const std::size_t kRtpHeaderSize = 12;           //RTP 固定头部大小（字节）
static const std::size_t kRtpExtensionHeaderSize = 4;   //RTP 扩展头部大小（profile_id + length）
static const std::uint8_t kRtpVersion = 2;              //RTP 版本号（当前固定为2）
static const std::uint8_t kH264FuAtype = 28;            //H.264 FU-A 分片类型（RFC 6184）
static const std::uint16_t kRtpLatencyExtensionProfileId = 0x5353;  //自定义延迟扩展的 profile ID
static const std::size_t kRtpLatencyExtensionPayloadSize = 16;      //延迟扩展载荷大小（2个64位时间戳）
static const std::size_t kRtpLatencyExtensionWordSize = kRtpLatencyExtensionPayloadSize / 4;//载荷大小（以32为字为单位）

//RTP头部字段
//包含 RTP 包头中解析出的关键信息
struct RtpHeaderFields{
    bool marker = false;                //标记位，通常用于标识帧边界
    std::uint8_t payload_type = 96;     //载荷类型，96-127为动态类型
    std::uint16_t sequence_number = 0;  //序列号，用于检测丢包和重排序
    std::uint32_t timestamp = 0;        //时间戳，标识媒体采样时刻
    std::uint32_t ssrc = 0;             //同步源标识符
};

//RTP 头部扩展
struct RtpHeaderExtension{
    std::uint16_t profile_id = 0;       //扩展 profile 标识
    std::vector<std::uint8_t> payload;  //扩展载荷数据
};

//自定义延迟扩展
//用于在 RTP 包中携带采集和发送时间戳，实现端到端延迟测量
struct RtpLatencyExtension{
    std::uint64_t capture_timestamp_ns = 0;         //采集时间戳（纳秒）
    std::uint64_t transport_send_timestamp_ns = 0;  //发送时间戳（纳秒）
};

//大端字节序读写工具函数
//RTP 协议使用网络字节序（大端），而 x86/x64 结构使用小段字节序
//因此需要进行字节序转换

//从大端字节序读取16位整数
inline std::uint16_t ReadBe16(const std::uint8_t *data){
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8) |
                                      static_cast<std::uint16_t>(data[1]));
}

//从大端字节序读取32位整数
inline std::uint32_t ReadBe32(const std::uint8_t *data){
    return (static_cast<std::uint32_t>(data[0]) << 24) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) |
           static_cast<std::uint32_t>(data[3]);
}

//从大端字节序读取64位整数
inline std::uint64_t ReadBe64(const std::uint8_t *data){
    return (static_cast<std::uint64_t>(data[0]) << 56) |
           (static_cast<std::uint64_t>(data[1]) << 48) |
           (static_cast<std::uint64_t>(data[2]) << 40) |
           (static_cast<std::uint64_t>(data[3]) << 32) |
           (static_cast<std::uint64_t>(data[4]) << 24) |
           (static_cast<std::uint64_t>(data[5]) << 16) |
           (static_cast<std::uint64_t>(data[6]) << 8) |
           static_cast<std::uint64_t>(data[7]);
}

//将16位整数写入大端字节序
inline void WriteBe16(std::uint16_t value, std::uint8_t *data){
    data[0] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    data[1] = static_cast<std::uint8_t>(value & 0xFF);
}

//将32位整数写入大端字节序
inline void WriteBe32(std::uint32_t value, std::uint8_t *data){
    data[0] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    data[1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    data[2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    data[3] = static_cast<std::uint8_t>(value & 0xFF);
}

//将64位整数写入大端字节序
inline void WriteBe64(std::uint64_t value, std::uint8_t *data){
    data[0] = static_cast<std::uint8_t>((value >> 56) & 0xFF);
    data[1] = static_cast<std::uint8_t>((value >> 48) & 0xFF);
    data[2] = static_cast<std::uint8_t>((value >> 40) & 0xFF);
    data[3] = static_cast<std::uint8_t>((value >> 32) & 0xFF);
    data[4] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    data[5] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    data[6] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    data[7] = static_cast<std::uint8_t>(value & 0xFF);
}

}
}
}

#endif
