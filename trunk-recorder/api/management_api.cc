#include "management_api.h"

#include "../config_manager.h"
#include "../../lib/httplib/httplib.h"

#include <fstream>
#include <memory>
#include <thread>
#include <json.hpp>

using json = nlohmann::json;

namespace {
std::string load_file_text(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open()) return "";
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool is_authorized(const httplib::Request &req, const std::string &token) {
  if (token.empty()) return false;
  auto auth = req.get_header_value("Authorization");
  if (auth == "Bearer " + token) return true;
  auto api_key = req.get_header_value("X-API-Key");
  return api_key == token;
}

void json_response(httplib::Response &res, const json &body, int status = 200) {
  res.status = status;
  res.set_content(body.dump(2), "application/json");
}
}

ManagementApi::ManagementApi(const std::string &bind_host,
                             int bind_port,
                             const std::string &token,
                             const std::string &config_file,
                             ApiRuntimeState *runtime_state)
    : host(bind_host), port(bind_port), api_token(token), config_path(config_file), state(runtime_state), server_ptr(nullptr), thread_ptr(nullptr) {}

ManagementApi::~ManagementApi() {
  stop();
}

bool ManagementApi::start() {
  auto *server = new httplib::Server();

  server->Get("/healthz", [](const httplib::Request &, httplib::Response &res) {
    json_response(res, { {"ok", true} });
  });

  server->Get("/api/v1/runtime", [this](const httplib::Request &, httplib::Response &res) {
    json_response(res, {
      {"apiEnabled", true},
      {"reloadRequested", state ? state->reload_requested.load() : false}
    });
  });

  server->Get("/api/v1/config", [this](const httplib::Request &req, httplib::Response &res) {
    const bool reveal = req.has_param("revealSecrets") && req.get_param_value("revealSecrets") == "true" && is_authorized(req, api_token);
    const auto text = load_file_text(config_path);
    if (text.empty()) {
      json_response(res, {{"error", "config_not_found"}}, 404);
      return;
    }
    try {
      auto parsed = json::parse(text);
      res.set_content(redact_config_json(parsed, reveal), "application/json");
    } catch (...) {
      json_response(res, {{"error", "config_parse_failed"}}, 500);
    }
  });

  server->Post("/api/v1/config/validate", [this](const httplib::Request &req, httplib::Response &res) {
    if (!is_authorized(req, api_token)) {
      json_response(res, {{"error", "unauthorized"}}, 401);
      return;
    }
    try {
      auto parsed = json::parse(req.body);
      auto result = validate_config_json(parsed);
      json issues = json::array();
      for (const auto &issue : result.issues) {
        issues.push_back({{"path", issue.path}, {"message", issue.message}, {"severity", issue.severity}});
      }
      json_response(res, {{"ok", result.ok}, {"issues", issues}}, result.ok ? 200 : 400);
    } catch (const std::exception &e) {
      json_response(res, {{"error", "invalid_json"}, {"message", e.what()}}, 400);
    }
  });

  server->Put("/api/v1/config", [this](const httplib::Request &req, httplib::Response &res) {
    if (!is_authorized(req, api_token)) {
      json_response(res, {{"error", "unauthorized"}}, 401);
      return;
    }
    try {
      auto parsed = json::parse(req.body);
      auto result = validate_config_json(parsed);
      if (!result.ok) {
        json issues = json::array();
        for (const auto &issue : result.issues) {
          issues.push_back({{"path", issue.path}, {"message", issue.message}, {"severity", issue.severity}});
        }
        json_response(res, {{"error", "validation_failed"}, {"issues", issues}}, 400);
        return;
      }
      std::string error;
      if (!write_config_json_atomic(config_path, parsed, error)) {
        json_response(res, {{"error", "persist_failed"}, {"message", error}}, 500);
        return;
      }
      json_response(res, {{"ok", true}, {"applied", false}, {"restartRequired", true}});
    } catch (const std::exception &e) {
      json_response(res, {{"error", "invalid_json"}, {"message", e.what()}}, 400);
    }
  });

  server->Post("/api/v1/config/apply", [this](const httplib::Request &req, httplib::Response &res) {
    if (!is_authorized(req, api_token)) {
      json_response(res, {{"error", "unauthorized"}}, 401);
      return;
    }
    if (state) state->reload_requested.store(true);
    json_response(res, {{"ok", true}, {"reloadRequested", true}, {"restartRequired", true}});
  });

  server->Get(R"(/api/v1/systems/(\d+)/talkgroups)", [this](const httplib::Request &req, httplib::Response &res) {
    if (!is_authorized(req, api_token)) {
      json_response(res, {{"error", "unauthorized"}}, 401);
      return;
    }
    const auto text = load_file_text("talkgroups.csv");
    json_response(res, {{"systemId", req.matches[1].str()}, {"content", text}});
  });

  server->Get(R"(/api/v1/systems/(\d+)/unit-tags)", [this](const httplib::Request &req, httplib::Response &res) {
    if (!is_authorized(req, api_token)) {
      json_response(res, {{"error", "unauthorized"}}, 401);
      return;
    }
    const auto text = load_file_text("unit-tags.csv");
    json_response(res, {{"systemId", req.matches[1].str()}, {"content", text}});
  });

  server_ptr = server;
  auto *thread = new std::thread([this, server]() {
    server->listen(host.c_str(), port);
  });
  thread_ptr = thread;
  return true;
}

void ManagementApi::stop() {
  auto *server = static_cast<httplib::Server *>(server_ptr);
  auto *thread = static_cast<std::thread *>(thread_ptr);

  if (server) {
    server->stop();
  }
  if (thread) {
    if (thread->joinable()) thread->join();
    delete thread;
  }
  if (server) delete server;

  server_ptr = nullptr;
  thread_ptr = nullptr;
}
