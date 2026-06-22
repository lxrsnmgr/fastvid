#include "StreamClient.h"
#include "StreamClientInternal.h"

#include <chrono>

namespace sclient{
using network_internal::BuildSocketError;
using network_internal::MonotonicNowNs;

StreamClient::StreamClient()
    : _socket_fd(-1),
      _running(false),
      _last_completed_frame_sequence(0),
      _has_last_completed_frame_sequence(false),
      _next_jitter_buffer_sequence(0),
      _has_next_jitter_buffer_sequence(false),
      _previous_network_latency_ms(0.0),
      _has_previous_network_latency(false){

      }

StreamClient::~StreamClient(){
    Close();
}

bool StreamClient::Connect(const ClientConfig &config, std::string *error_message){

}
}
