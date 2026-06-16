#ifndef __SRC_COMMON_METRICS_H__
#define __SRC_COMMON_METRICS_H__

#include <vector>

namespace sclient{
//延迟统计概要
//半酣样本数量、最小/最大/平均值和常用百分位数
struct LatencySummary{
    std::size_t count = 0;  //样本总数
    double last_ms = 0.0;   //最近一次采集值(毫秒)
    double min_ms = 0.0;    //最小值
    double avg_ms = 0.0;    //最大值
    double p50_ms = 0.0;    //中位数(第50百分位)
    double p95_ms = 0.0;    //第95百分位
    double p99_ms = 0.0;    //第99百分位
    double max_ms = 0.0;    //最大值
};

//延迟统计工具类
//使用环形缓冲区存储最近N个采样值，支持计算各种统计指标
//采用惰性刷新策略，避免每次查询都重新排序
class LatencySatas{
public:
    //构造函数
    //max_samples 环形缓冲区大小，即最多保留的采样值
    //snapshot_refresh_interval 快照刷新间隔(采样次数)
    explicit LatencySatas(std::size_t max_samples = 4096, std::size_t snapshot_refresh_interval = 8)
        : _max_samples(max_samples),
          _snapshot_refresh_interval(std::max<std::size_t>(1, snapshot_refresh_interval)),
          _samples_ms(max_samples, 0.0),
          _next_index(0),
          _sample_count(0),
          _dirty(false),
          _cached_summary_valid(false),
          _pending_snapshot_samples(0),
          _last_ms(0.0){

          }




private:
    std::size_t _max_samples;                       //环形缓冲区的最大容量
    std::size_t _snapshot_refresh_interval;         //快照刷新间隔(采样次数)
    std::vector<double> _samples_ms;                //环形缓冲区
    std::size_t _next_index;                        //下一个写入位置
    std::size_t _sample_count;                      //已采样总数
    mutable bool _dirty;                            //数据是否已变化
    mutable bool _cached_summary_valid;             //缓存摘要是否有效
    mutable std::size_t _pending_snapshot_samples;  //自上次刷新以来的采样数
    mutable LatencySummary _cached_summary;         //缓存的统计摘要
    double _last_ms;                                //最后一次采样值
};
}

#endif
