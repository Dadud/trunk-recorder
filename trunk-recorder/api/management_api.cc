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

json load_config_json(const std::string &path) {
  const auto text = load_file_text(path);
  if (text.empty()) return json();
  return json::parse(text);
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

json issues_to_json(const ConfigValidationResult &result) {
  json issues = json::array();
  for (const auto &issue : result.issues) {
    issues.push_back({{"path", issue.path}, {"message", issue.message}, {"severity", issue.severity}});
  }
  return issues;
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
    json_response(res, {{"ok", true}});
  });

  server->Get("/readyz", [](const httplib::Request &, httplib::Response &res) {
    json_response(res, {{"ok", true}});
  });

  server->Get("/api/v1/runtime", [this](const httplib::Request &, httplib::Response &res) {
    json_response(res, {
      {"apiEnabled", true},
      {"reloadRequested", state ? state->reload_requested.load() : false},
      {"configPath", config_path}
    });
  });

  server->Get("/api/v1/config/schema", [this](const httplib::Request &, httplib::Response &res) {
    json_response(res, {
      {"version", 1},
      {"canonicalFormat", "config.json"},
      {"notes", json::array({
        "config.json remains canonical",
        "mutation endpoints require auth",
        "apply may still require restart for many settings"
      })},
      {"topLevelKeys", json::array({"ver", "sources", "systems", "plugins", "api"})}
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

  server->Get("/api/v1/sources", [this](const httplib::Request &, httplib::Response &res) {
    try {
      auto parsed = load_config_json(config_path);
      json_response(res, parsed.contains("sources") ? parsed["sources"] : json::array());
    } catch (...) {
      json_response(res, {{"error", "config_parse_failed"}}, 500);
    }
  });

  server->Get("/api/v1/systems", [this](const httplib::Request &, httplib::Response &res) {
    try {
      auto parsed = load_config_json(config_path);
      json_response(res, parsed.contains("systems") ? parsed["systems"] : json::array());
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
      json_response(res, {{"ok", result.ok}, {"issues", issues_to_json(result)}}, result.ok ? 200 : 400);
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
        json_response(res, {{"error", "validation_failed"}, {"issues", issues_to_json(result)}}, 400);
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

  server->Patch("/api/v1/config", [this](const httplib::Request &req, httplib::Response &res) {
    if (!is_authorized(req, api_token)) {
      json_response(res, {{"error", "unauthorized"}}, 401);
      return;
    }
    try {
      auto existing = load_config_json(config_path);
      if (!existing.is_object()) {
        json_response(res, {{"error", "config_not_found"}}, 404);
        return;
      }
      auto patch = json::parse(req.body);
      existing.merge_patch(patch);
      auto result = validate_config_json(existing);
      if (!result.ok) {
        json_response(res, {{"error", "validation_failed"}, {"issues", issues_to_json(result)}}, 400);
        return;
      }
      std::string error;
      if (!write_config_json_atomic(config_path, existing, error)) {
        json_response(res, {{"error", "persist_failed"}, {"message", error}}, 500);
        return;
      }
      json_response(res, {{"ok", true}, {"patched", true}, {"restartRequired", true}});
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
    try {
      auto parsed = load_config_json(config_path);
      size_t index = static_cast<size_t>(std::stoul(req.matches[1].str()));
      if (!parsed.contains("systems") || !parsed["systems"].is_array() || index >= parsed["systems"].size()) {
        json_response(res, {{"error", "system_not_found"}}, 404);
        return;
      }
      const auto &system = parsed["systems"][index];
      const std::string file_path = resolve_system_relative_path(config_path, system.value("talkgroupsFile", ""));
      json_response(res, {{"systemId", index}, {"path", file_path}, {"content", load_file_text(file_path)}});
    } catch (const std::exception &e) {
      json_response(res, {{"error", "read_failed"}, {"message", e.what()}}, 500);
    }
  });

  server->Put(R"(/api/v1/systems/(\d+)/talkgroups)", [this](const httplib::Request &req, httplib::Response &res) {
    if (!is_authorized(req, api_token)) {
      json_response(res, {{"error", "unauthorized"}}, 401);
      return;
    }
    try {
      auto parsed = load_config_json(config_path);
      size_t index = static_cast<size_t>(std::stoul(req.matches[1].str()));
      if (!parsed.contains("systems") || !parsed["systems"].is_array() || index >= parsed["systems"].size()) {
        json_response(res, {{"error", "system_not_found"}}, 404);
        return;
      }

      // Basic structural validation: check column counts and non-emptiness.
      // Full semantic parsing is done by trunk-recorder's talkgroups.cc.
      auto csv_result = validate_talkgroups_csv(req.body);
      if (!csv_result.ok) {
        json issues = json::array();
        for (const auto &issue : csv_result.issues) {
          issues.push_back({{"path", issue.path}, {"message", issue.message}});
        }
        json_response(res, {{"error", "csv_validation_failed"}, {"issues", issues}}, 400);
        return;
      }

      const auto &system = parsed["systems"][index];
      const std::string file_path = resolve_system_relative_path(config_path, system.value("talkgroupsFile", ""));
      std::string error;
      if (!write_text_file_atomic(file_path, req.body, error)) {
        json_response(res, {{"error", "persist_failed"}, {"message", error}}, 500);
        return;
      }
      json_response(res, {{"ok", true}, {"path", file_path}, {"restartRequired", true}});
    } catch (const std::exception &e) {
      json_response(res, {{"error", "write_failed"}, {"message", e.what()}}, 500);
    }
  });

  server->Get(R"(/api/v1/systems/(\d+)/unit-tags)", [this](const httplib::Request &req, httplib::Response &res) {
    if (!is_authorized(req, api_token)) {
      json_response(res, {{"error", "unauthorized"}}, 401);
      return;
    }
    try {
      auto parsed = load_config_json(config_path);
      size_t index = static_cast<size_t>(std::stoul(req.matches[1].str()));
      if (!parsed.contains("systems") || !parsed["systems"].is_array() || index >= parsed["systems"].size()) {
        json_response(res, {{"error", "system_not_found"}}, 404);
        return;
      }
      const auto &system = parsed["systems"][index];
      const std::string file_path = resolve_system_relative_path(config_path, system.value("unitTagsFile", ""));
      json_response(res, {{"systemId", index}, {"path", file_path}, {"content", load_file_text(file_path)}});
    } catch (const std::exception &e) {
      json_response(res, {{"error", "read_failed"}, {"message", e.what()}}, 500);
    }
  });

  server->Put(R"(/api/v1/systems/(\d+)/unit-tags)", [this](const httplib::Request &req, httplib::Response &res) {
    if (!is_authorized(req, api_token)) {
      json_response(res, {{"error", "unauthorized"}}, 401);
      return;
    }
    try {
      auto parsed = load_config_json(config_path);
      size_t index = static_cast<size_t>(std::stoul(req.matches[1].str()));
      if (!parsed.contains("systems") || !parsed["systems"].is_array() || index >= parsed["systems"].size()) {
        json_response(res, {{"error", "system_not_found"}}, 404);
        return;
      }

      // Basic structural validation: check column counts and non-emptiness.
      // Full semantic parsing is done by trunk-recorder's unit_tags.cc.
      auto csv_result = validate_unit_tags_csv(req.body);
      if (!csv_result.ok) {
        json issues = json::array();
        for (const auto &issue : csv_result.issues) {
          issues.push_back({{"path", issue.path}, {"message", issue.message}});
        }
        json_response(res, {{"error", "csv_validation_failed"}, {"issues", issues}}, 400);
        return;
      }

      const auto &system = parsed["systems"][index];
      const std::string file_path = resolve_system_relative_path(config_path, system.value("unitTagsFile", ""));
      std::string error;
      if (!write_text_file_atomic(file_path, req.body, error)) {
        json_response(res, {{"error", "persist_failed"}, {"message", error}}, 500);
        return;
      }
      json_response(res, {{"ok", true}, {"path", file_path}, {"restartRequired", true}});
    } catch (const std::exception &e) {
      json_response(res, {{"error", "write_failed"}, {"message", e.what()}}, 500);
    }
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
