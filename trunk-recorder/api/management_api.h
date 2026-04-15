#ifndef MANAGEMENT_API_H
#define MANAGEMENT_API_H

#include <atomic>
#include <string>

struct ApiRuntimeState {
  std::atomic<bool> reload_requested{false};
};

class ManagementApi {
public:
  ManagementApi(const std::string &bind_host,
                int bind_port,
                const std::string &token,
                const std::string &config_file,
                ApiRuntimeState *runtime_state);
  ~ManagementApi();

  bool start();
  void stop();

private:
  std::string host;
  int port;
  std::string api_token;
  std::string config_path;
  ApiRuntimeState *state;
  void *server_ptr;
  void *thread_ptr;
};

#endif
