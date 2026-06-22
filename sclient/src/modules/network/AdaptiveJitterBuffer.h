#ifndef __SRC_MODULES_NETWORK_ADAPTIVEJITTERBUFFER_H__
#define __SRC_MODULES_NETWORK_ADAPTIVEJITTERBUFFER_H__

#include "../../common/log/Logger.h"
#include "../../common/metrics/LatencyStats.h"
#include <string>

namespace sclient{
//抖动缓冲模式
//根据网络质量自动切换，在延迟和流畅度之间取得平衡
enum class JitterBufferMode{
    kBypass,        //旁路模式：零延迟，适用于局域网
    kLowLatency,    //低延迟模式：最小缓冲，适用于稳定网络
    kSmooth,        //流畅模式：较大缓冲，适用于不稳定网络
};

//网络质量评级
//基于抖动、丢包率和跳帧率综合评价
enum class NetworkQuality{
    kExcellent,     //局域网/本地：旁路模式，最小缓冲
    kGood,          //稳定 Wi-Fi：低延迟模式，适当缓冲
    kFair,          //不稳定 Wi-Fi /轻微丢包：流畅模式，较大缓冲
    kPoor,          //高丢包/高抖动：流畅模式 + 积极保护
};

//自适应抖动缓冲配置
struct AdaptiveJitterBufferConfig{
    int min_delay_ms = 2;
    double safety_factor = 1.5;
    double smooth_factor = 2.5;
    int base_max_wait_ms = 80;
    std::size_t base_max_frames = 8;
};

//自适应抖动缓冲器
//根据网络质量自动调整缓冲策略
//- 网络良好时使用旁路模式
//- 网络一般时使用低延迟模式，最小化延迟
//- 网络较差时使用流畅模式，优先保证播放流畅
//状态机转换基于连续采样和 p95 抖动值，避免频繁切换
class AdaptiveJitterBuffer{
    AdaptiveJitterBuffer()
        : _active_mode(JitterBufferMode::kBypass),
          _network_quality(NetworkQuality::kExcellent),
          _auto_mode(true),
          _window(100, 4),
          _consecutive_high_jitter(0),
          _consecutive_low_jitter(0),
          _low_to_smooth_p95_exceeded(false),
          _smooth_low_p95_since_ns(0),
          _last_logged_mode(JitterBufferMode::kBypass),
          _last_periodic_log_ns(0),
          _current_max_wait_ms(80),
          _current_max_frames(8){

          }

    //配置缓冲参数
    void Configure(const AdaptiveJitterBufferConfig &config){
        _config = config;
        _current_max_wait_ms = _config.base_max_wait_ms;
        _current_max_frames = _config.base_max_frames;
    }

    //记录抖动值（简化版本，无丢包和跳帧信息）
    void RecordJitter(double jitter_ms, std::uint64_t now_ns){
        RecordJitter(jitter_ms, 0.0, 0.0, now_ns);
    }

    //记录抖动值并更新缓冲策略
    //@param jitter_ms 当前抖动值（毫秒）
    //@param fragment_loss_percent 分片丢包率（百分比）
    //@param skip_rate_percent 跳帧率（百分比）
    //@param now_ns 当前时间戳（纳秒）
    void RecordJitter(
            double jitter_ms,
            double fragment_loss_percent,
            double skip_rate_percent,
            std::uint64_t now_ns){
        _window.Record(jitter_ms);

        AssessNetworkQuality(jitter_ms, fragment_loss_percent, skip_rate_percent);

        //采样数足够后开始自动调整
        if(_auto_mode && _window.sample_count() >= 10){
            EvaluateStateTransition(jitter_ms, now_ns);
            ApplyQualityParams();
        }

        const double target_ms = static_cast<double>(TargetDelayNs()) / 1000000.0;

        //模式或质量变化时记录日志
        if(_active_mode != _last_logged_mode || _network_quality != _last_logged_quality){
            common::log::Logger::Info(
                    "jitter buffer mode: " + ModeName(_last_logged_mode) + " ->" + ModeName(_active_mode)
                    + " quality=" + QualityName(_network_quality)
                    + ", target=" + FormatDouble(target_ms) + "ms"
                    + ", jitter_p95=" + FormatDouble(jitter_p95_ms()) + "ms"
                    + ", loss=" + FormatDouble(fragment_loss_percent) + "%");
            _last_logged_mode = _active_mode;
            _last_logged_quality = _network_quality;
        }

        //每5秒记录一次状态日志
        if(now_ns - _last_periodic_log_ns >= 5000000000ULL){
            common::log::Logger::Info(
                    "jitter buffer status: mode" + ModeName(_active_mode)
                    + " quality=" + QualityName(_network_quality)
                    + " target=" + FormatDouble(target_ms) + "ms"
                    + " p50=" + FormatDouble(jitter_50_ms()) + "ms"
                    + " p95=" + FormatDouble(jitter_p95_ms()) + "ms"
                    + " loss=" + FormatDouble(fragment_loss_percent) + "%"
                    + " max_wait=" = std::to_string(_current_max_wait_ms) + "ms"
                    + " max_frames=" + std::to_string(_current_max_frames)
                    + " samples=" + std::to_string(_window.sample_count()));
            _last_periodic_log_ns = now_ns;
        }
    }

    //获取目标延迟（纳秒）
    std::uint64_t TargetDelayNs() const{
        return ComputeDelayForMode(_active_mode);
    }

    //设置固定模式（禁用自动调整）
    void SetFixedMode(JitterBufferMode mode){
        _auto_mode = false;
        _active_mode = mode;
    }

    //启动自动模式
    void EnableAutoMode(){
        _auto_mode = true;
    }

    //重置所有状态
    void Reset(){
        _window = LatencyStats(100, 4);
        _active_mode = JitterBufferMode::kBypass;
        _network_quality = NetworkQuality::kExcellent;
        _consecutive_low_jitter = 0;
        _consecutive_high_jitter = 0;
        _low_to_smooth_p95_exceeded = false;
        _smooth_low_p95_since_ns = 0;
        _last_logged_mode = JitterBufferMode::kBypass;
        _last_logged_quality = NetworkQuality::kExcellent;
        _last_periodic_log_ns = 0;
        _current_max_wait_ms = _config.base_max_wait_ms;
        _current_max_frames = _config.base_max_frames;
    }

    JitterBufferMode active_mode() const {return _active_mode;};

    NetworkQuality network_quality() const {return _network_quality;};

    std::string active_mode_name() const {return ModeName(_active_mode);};

    std::string network_quality_name() const {return QualityName(_network_quality);};

    double jitter_p95_ms() const {return _window.Snapshot().p95_ms;};

    double jitter_50_ms() const {return _window.Snapshot().p50_ms;};

    std::size_t jitter_sampes() const {return _window.sample_count();};

    int quallity_max_wait_ms() const {return _current_max_wait_ms;};

    std::size_t quality_max_frames() const {return _current_max_frames;};
private:
    //评估网络质量等级
    void AssessNetworkQuality(double jitter_ms, double loss_percent, double skip_percent){
        const double p95 = jitter_p95_ms();

        if(p95 > 50.0 || loss_percent > 1.0 || skip_percent > 5.0){
            _network_quality = NetworkQuality::kPoor;
        } else if(p95 > 10.0 || loss_percent > 0.1 || skip_percent > 1.0){
            _network_quality = NetworkQuality::kFair;
        } else if(p95 > 1.0 || loss_percent > 0.01){
            _network_quality = NetworkQuality::kGood;
        } else {
            _network_quality = NetworkQuality::kExcellent;
        }
    }

    //根据网络质量调整缓冲参数
    void ApplyQualityParams(){
        switch(_network_quality){
            case NetworkQuality::kExcellent:
                _current_max_wait_ms = std::max(20, _config.base_max_wait_ms / 4);
                _current_max_frames = std::max<std::size_t>(2, _config.base_max_frames / 4);
                break;

            case NetworkQuality::kGood:
                _current_max_wait_ms = _config.base_max_wait_ms / 2;
                _current_max_frames = std::max<std::size_t>(4, _config.base_max_frames / 2);
                break;

            case NetworkQuality::kFair:
                _current_max_wait_ms = _config.base_max_wait_ms;
                _current_max_frames = _config.base_max_frames;
                break;

            case NetworkQuality::kPoor:
                _current_max_wait_ms = _config.base_max_wait_ms * 3;
                _current_max_frames = _config.base_max_frames * 4;
                break;
        }
    }

    //评估状态转换
    //状态机逻辑：
    //- Bypass -> LowLatency：连续10次抖动 > 1ms
    //- LowLatency -> Bypass：连续20次抖动 < 0.5ms
    //- LowLatency -> Smooth：p95 连续两次超过 10ms
    //- Smooth -> LowLatency：p95 持续低于 5ms 超过 3 秒
    void EvaluateStateTransition(double jitter_ms, std::uint64_t now_ns){
        const double p95 = jitter_p95_ms();

        switch(_active_mode){
            case JitterBufferMode::kBypass:
                if(jitter_ms > 1.0){
                    ++_consecutive_high_jitter;
                } else {
                    _consecutive_high_jitter = 0;
                }
                if(_consecutive_high_jitter >= 10){
                    _active_mode = JitterBufferMode::kLowLatency;
                    _consecutive_low_jitter = 0;
                    _consecutive_high_jitter = 0;
                    _low_to_smooth_p95_exceeded = false;
                }
                break;

            case JitterBufferMode::kLowLatency:
                if(jitter_ms < 0.5){
                    ++_consecutive_low_jitter;
                } else {
                    _consecutive_low_jitter = 0;
                }
                if(_consecutive_low_jitter >= 20){
                    _active_mode = JitterBufferMode::kBypass;
                    _consecutive_low_jitter = 0;
                    _consecutive_high_jitter = 0;
                    break;
                }
                if(p95 > 10.0){
                    if(!_low_to_smooth_p95_exceeded){
                        _low_to_smooth_p95_exceeded = true;
                    } else {
                        _active_mode = JitterBufferMode::kSmooth;
                        _consecutive_low_jitter = 0;
                        _consecutive_high_jitter = 0;
                        _low_to_smooth_p95_exceeded = false;
                        _smooth_low_p95_since_ns = 0;
                    }
                } else {
                    _low_to_smooth_p95_exceeded = false;
                }
                break;

            case JitterBufferMode::kSmooth:
                if(p95 < 5.0){
                    if(_smooth_low_p95_since_ns == 0){
                        _smooth_low_p95_since_ns = now_ns;
                    } else if(now_ns - _smooth_low_p95_since_ns >= 3000000000ULL){
                        _active_mode = JitterBufferMode::kLowLatency;
                        _consecutive_low_jitter = 0;
                        _consecutive_high_jitter = 0;
                        _low_to_smooth_p95_exceeded = false;
                        _smooth_low_p95_since_ns = 0;
                    }
                } else {
                    _smooth_low_p95_since_ns = 0;
                }
                break;
        }
    }

    //根据当前模式计算目标延迟
    std::uint64_t ComputeDelayForMode(JitterBufferMode mode) const{
        switch(mode){
            case JitterBufferMode::kBypass:
                return 0;

            case JitterBufferMode::kLowLatency:{
                const double p95 = jitter_p95_ms();
                const double target = std::max(
                        static_cast<double>(_config.min_delay_ms),
                        p95 * _config.safety_factor);
                return static_cast<std::uint64_t>(target * 1000000.0);
            }

            case JitterBufferMode::kSmooth:{
                const double p95 = jitter_p95_ms();
                const double target = std::max(
                        static_cast<double>(_config.min_delay_ms),
                        p95 * _config.smooth_factor);
                return static_cast<std::uint64_t>(target * 1000000.0);
            }
        }
        return 0;
    }

    static std::string ModeName(JitterBufferMode mode){
        switch(mode){
            case JitterBufferMode::kBypass: return "bypass";
            case JitterBufferMode::kLowLatency: return "low_latency";
            case JitterBufferMode::kSmooth: return "smooth";
        }
        return "unknown";
    }

    static std::string QualityName(NetworkQuality quality){
        switch(quality){
            case NetworkQuality::kExcellent: return "excellent";
            case NetworkQuality::kGood: return "good";
            case NetworkQuality::kPoor: return "poor";
            case NetworkQuality::kFair: return "fair";
        }
        return "unknown";
    }

    static std::string FormatDouble(double value){
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", value);
        return buf;
    }

    JitterBufferMode _active_mode;          //当前激活的缓冲模式
    NetworkQuality _network_quality;        //当前网络质量评级
    bool _auto_mode;                        //是否启用自动模式
    LatencyStats _window;                   //抖动统计窗口（用于计算 p95）

    int _consecutive_high_jitter;           //连续高抖动计数
    int _consecutive_low_jitter;            //连续低抖动计数
    bool _low_to_smooth_p95_exceeded;       //p95 是否已超过阈值（用于状态转换）
    std::uint64_t _smooth_low_p95_since_ns; //p95 开始低于阈值时间

    JitterBufferMode _last_logged_mode;     //上次日志记录的模式
    NetworkQuality _last_logged_quality;    //上次日志记录的质量
    std::uint64_t _last_periodic_log_ns;    //上次周期性日志时间
    AdaptiveJitterBufferConfig _config;     //配置参数

    int _current_max_wait_ms;               //当前最大等待时间（根据质量调整）
    std::size_t _current_max_frames;        //当前最大缓冲帧数（根据质量调整）
};

}//namespace sclient
#endif
