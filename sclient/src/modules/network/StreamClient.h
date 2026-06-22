#ifndef __SRC_MODULES_NETWORK_STREAMCLIENT_H__
#define __SRC_MODULES_NETWORK_STREAMCLIENT_H__

#include "../../common/protocol/Protocol.h"
#include "types/ClientConfig.h"
#include "types/ReveiveFrame.h"
#include "types/UdpReceiveStats.h"
#include "AdaptiveJitterBuffer.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <deque>
#include <unordered_set>

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
    bool Connect(const ClientConfig &configi, std::string *error_message);

    //接收一帧视频数据（阻塞）
    bool ReceiveFrame(ReceivedFrame *frame, std::string *error_message);

    //获取 UDP 接收统计信息
    UdpReceiveStats udp_receive_stats() const;

    //获取绑定的本地接口
    int BoundPort() const;

    //关闭连接
    void Close();
private:
    //RAII 守卫：在作用域结束时发布 UDP 统计快照
    class UdpStatsSnapshotGuard{
    public:
        explicit UdpStatsSnapshotGuard(StreamClient *client)
            : _client(client){

            }

        ~UdpStatsSnapshotGuard(){
            if(_client != nullptr){
                _client->PublishUdpReceiveStatsSnapshot();
            }
        }

    private:
        StreamClient *_client;
    };

    //UDP 帧组装状态
    //大帧被分割成多个 UDP 分片传输，此结构跟踪组装进度
    struct UdpFrameAssembly{
        protocol::MessageHeader header;
        protocol::FrameDiagnosticMetaData metadata;
        std::vector<std::uint8_t> payload;                  //组装后的帧数据
        std::vector<bool> received_fragments;               //各分片是否已收到
        std::vector<std::uint32_t> fragment_offsets;        //各分片在帧中的偏移
        std::vector<std::uint32_t> fragment_payload_sizes;  //各分片载荷大小
        std::size_t received_fragment_count = 0;            //已收到的分片数
        std::vector<std::uint8_t> fec_payload;              //FEC 校验分片数据
        bool has_fec_payload = false;                       //是否收到 FEC 分片
        std::uint64_t fec_payload_timestamp_ns = 0;         //FEC 分片时间戳    
        std::uint64_t first_seen_timestamp_ns = 0;          //首次见到此帧的时间
        std::uint64_t last_fragment_timestamp_ns = 0;       //最后一个分片到达时间
        std::uint64_t last_nack_timestamp_ns = 0;           //上次发送 NACK 的时间
        int nack_attempts = 0;                              //NACK 重试次数
    };

    //抖动缓冲区中的帧
    struct BufferedUdpFrame{
        ReceivedFrame frame;
        std::uint64_t buffered_timestamp_ns = 0;        //入缓冲区时间
    };

    //RTP 帧组装状态
    //RTP 包可能包含完整的 NAL 单元或 FU-A 分片，需要重组
    struct RtpFrameAssembly{
        std::vector<std::uint8_t> payload;          //组装后的帧数据
        std::uint32_t timestamp = 0;                //RTP 时间戳（标识同一帧）
        std::uint32_t ssrc = 0;                     //同步源标识
        std::uint16_t next_sequence_number = 0;     //期望的下一个系列号
        std::uint64_t capture_timestamp_ns = 0;     //采集时间戳（从扩展解析）
        std::uint64_t transport_send_timestamp = 0; //发送时间戳（从扩展解析）
        bool active = false;                        //是否正在组装帧
        bool has_sequence_number = false;           //是否已设置序列号
        bool sender_metadata_available = false;     //发送端元数据是否可用
        bool sender_metadata_invalid = false;       //发送端元数据是否无效
        bool frame_damaged = false;                 //帧是否损坏（丢包导致）
        bool fu_in_progress = false;                //是否正在组装 FU-A 分片
    };

    //TCP 接收
    bool ReceiveAll(char *buffer, std::size_t length, std::string *error_message);
    bool ReceiveTcpFrame(ReceivedFrame *frame, std::string *error_message);

    //UDP 接收和处理
    bool ReceiveUdpFrame(ReceivedFrame *frame, std::string *error_message);
    bool ProcessUdpDatagram(
            const char *data,
            std::size_t datagram_size,
            std::uint64_t now_ns,
            bool defer_ready_pop,
            ReceivedFrame *frame,
            bool *frame_ready);
    bool FinalizeRecoverableUdpFecFrames(std::uint64_t now_ns, bool defer_ready_pop, ReceivedFrame *frame);
    bool TryPopReadyUdpFrame(ReceivedFrame *frame, std::uint64_t now_ns);
    bool FinalizeCompletedUdpFrame(
            std::uint64_t frame_sequence,
            UdpFrameAssembly *assembly,
            std::uint64_t now_ns,
            bool defer_ready_pop,
            ReceivedFrame *frame);

    //抖动和丢包传入（测试用）
    std::uint64_t CurrentJitterBufferTargetDelayNs();
    std::uint64_t InjectedUdpJitterDelayNs(const ReceivedFrame &frame) const;
    bool ShouldInjectUdpLoss(const protocol::UdpFrameFragmentHeader &fragment_header);

    //FEC 前后纠错
    bool MabeyRecoverWithUdpFec(UdpFrameAssembly *assembly);

    //NACK 重传
    bool IsRecentlyCompletedUdpFrameSequence(std::uint64_t sequence)  const;
    void RememberCompletedUdpFrameSequence(std::uint64_t sequence);
    void MaybeSendUdpNackRequests(std::uint64_t now_ns);
    bool SendUdpNack(std::uint64_t frame_sequence, const std::vector<std::uint16_t> &missing_fragments);

    //状态管理
    void ResetUdpState();
    void ResetRtpState();
    void PruneExpiredUdpAssemblies(std::uint64_t now_ns);
    void BufferCompletedUdpFrame(ReceivedFrame &&frame);
    void OnCompletedUdpFrame(const ReceivedFrame &frame, std::uint64_t now_ns);

    //RTP 接收
    bool ReceiveRtpFrame(ReceivedFrame *feame, std::string *error_message);
    bool TryPopReadyRtpFrame(ReceivedFrame *frame);

    //统计和保活
    void UpdateJitterBufferDepthStats();
    void RecordJitterBufferWait(std::uint64_t wait_ns);
    bool SendKeepAlive();
    void KeepAliveLoop();
    void PublishUdpReceiveStatsSnapshot();

    ClientConfig _config;
    int _socket_fd;
    std::atomic_bool _running;
    std::thread _keepalive_thread;
    std::mutex _send_mutex;
    mutable std::mutex _udp_receive_stats_snapshot_mutex;
    UdpReceiveStats _udp_receive_stats;
    UdpReceiveStats _udp_receive_stats_snapshot;
    std::vector<char> _udp_datagram_buffer;
    std::vector<char> _rtp_datagram_buffer;
    std::map<std::uint64_t, UdpFrameAssembly> _udp_assemblies;
    std::map<std::uint64_t, BufferedUdpFrame> _udp_jitter_buffer;
    RtpFrameAssembly _rtp_completed_frame_sequence;
    std::uint64_t _last_completed_frame_sequence;
    bool _has_last_completed_frame_sequence;
    std::uint64_t _next_jitter_buffer_sequence;
    bool _has_next_jitter_buffer_sequence;
    std::map<std::uint64_t, std::vector<bool>> _udp_test_dropped_fragments;
    std::deque<std::uint64_t> _recently_completed_udp_sequence_order;
    std::unordered_set<std::uint64_t> _recently_completed_udp_sequence;
    double _previous_network_latency_ms;
    bool _has_previous_network_latency;
    AdaptiveJitterBuffer _adaptive_jitter;
};
}//namespace sclient
#endif
