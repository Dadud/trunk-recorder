#include "config_manager.h"

#include <fstream>
#include <cstdio>

using json = nlohmann::json;

static void add_issue(ConfigValidationResult &result, const std::string &path, const std::string &message, const std::string &severity = "error") {
  result.issues.push_back({path, message, severity});
}

ConfigValidationResult validate_config_json(const json &data) {
  ConfigValidationResult result;

  if (!data.is_object()) {
    add_issue(result, "/", "config must be a JSON object");
    return result;
  }

  if (!data.contains("ver") || !data["ver"].is_number()) {
    add_issue(result, "/ver", "ver must be present and numeric");
  } else if (data["ver"].get<double>() < 2) {
    add_issue(result, "/ver", "ver must be 2 or greater");
  }

  if (!data.contains("sources") || !data["sources"].is_array()) {
    add_issue(result, "/sources", "sources must be an array");
  } else if (data["sources"].empty()) {
    add_issue(result, "/sources", "sources must contain at least one source");
  }

  if (!data.contains("systems") || !data["systems"].is_array()) {
    add_issue(result, "/systems", "systems must be an array");
  } else if (data["systems"].empty()) {
    add_issue(result, "/systems", "systems must contain at least one system");
  }

  if (data.contains("sources") && data["sources"].is_array()) {
    for (size_t i = 0; i < data["sources"].size(); i++) {
      const auto &source = data["sources"][i];
      const std::string base = "/sources/" + std::to_string(i);
      if (!source.is_object()) {
        add_issue(result, base, "source must be an object");
        continue;
      }
      if (!source.contains("driver") || !source["driver"].is_string()) add_issue(result, base + "/driver", "driver must be a string");
      if (!source.contains("rate") || !source["rate"].is_number()) add_issue(result, base + "/rate", "rate must be numeric");
      if (source.contains("driver") && source["driver"].is_string()) {
        const std::string driver = source["driver"].get<std::string>();
        if (driver != "osmosdr" && driver != "usrp" && driver != "iqfile" && driver != "sigmf" && driver != "sigmffile") {
          add_issue(result, base + "/driver", "driver must be one of osmosdr, usrp, iqfile, sigmf, sigmffile");
        }
      }
    }
  }

  if (data.contains("systems") && data["systems"].is_array()) {
    for (size_t i = 0; i < data["systems"].size(); i++) {
      const auto &system = data["systems"][i];
      const std::string base = "/systems/" + std::to_string(i);
      if (!system.is_object()) {
        add_issue(result, base, "system must be an object");
        continue;
      }
      if (!system.contains("type") || !system["type"].is_string()) add_issue(result, base + "/type", "type must be a string");
      if (system.contains("type") && system["type"].is_string()) {
        const std::string type = system["type"].get<std::string>();
        if (type == "p25" || type == "smartnet") {
          if (!system.contains("control_channels") || !system["control_channels"].is_array() || system["control_channels"].empty()) {
            add_issue(result, base + "/control_channels", "trunked systems require a non-empty control_channels array");
          }
        }
      }
    }
  }

  result.ok = result.issues.empty();
  return result;
}

std::string redact_config_json(const json &data, bool reveal_secrets) {
  json copy = data;
  if (!reveal_secrets) {
    if (copy.contains("api") && copy["api"].is_object() && copy["api"].contains("token")) {
      copy["api"]["token"] = "<redacted>";
    }
    if (copy.contains("systems") && copy["systems"].is_array()) {
      for (auto &system : copy["systems"]) {
        if (system.is_object()) {
          if (system.contains("apiKey")) system["apiKey"] = "<redacted>";
          if (system.contains("broadcastifyApiKey")) system["broadcastifyApiKey"] = "<redacted>";
        }
      }
    }
  }
  return copy.dump(2);
}

bool write_config_json_atomic(const std::string &config_file, const json &data, std::string &error) {
  const std::string tmp_file = config_file + ".tmp";
  const std::string backup_file = config_file + ".bak";

  {
    std::ofstream out(tmp_file, std::ios::trunc);
    if (!out.is_open()) {
      error = "unable to open temporary file for writing";
      return false;
    }
    out << data.dump(2) << std::endl;
    out.flush();
    if (!out.good()) {
      error = "failed writing temporary config file";
      return false;
    }
  }

  std::ifstream existing(config_file);
  if (existing.good()) {
    existing.close();
    std::remove(backup_file.c_str());
    if (std::rename(config_file.c_str(), backup_file.c_str()) != 0) {
      error = "unable to create config backup";
      std::remove(tmp_file.c_str());
      return false;
    }
  }

  if (std::rename(tmp_file.c_str(), config_file.c_str()) != 0) {
    error = "unable to replace config file";
    std::remove(tmp_file.c_str());
    return false;
  }

  return true;
}
