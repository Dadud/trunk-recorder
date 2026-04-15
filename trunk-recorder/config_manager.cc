#include "config_manager.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

using json = nlohmann::json;
namespace fs = std::filesystem;

static void add_issue(ConfigValidationResult &result, const std::string &path, const std::string &message, const std::string &severity = "error") {
  result.issues.push_back({path, message, severity});
}

static bool is_number_array(const json &value) {
  if (!value.is_array()) return false;
  for (const auto &entry : value) {
    if (!entry.is_number()) return false;
  }
  return true;
}

static bool is_string_in(const std::string &value, const std::set<std::string> &allowed) {
  return allowed.find(value) != allowed.end();
}

// ---------------------------------------------------------------------------
// Helper CSV structural validation
// These provide basic sanity checks only. Full parsing and semantic
// validation is done by trunk-recorder's talkgroups.cc / unit_tags.cc.
// ---------------------------------------------------------------------------

static std::vector<std::string> split_csv_line(const std::string &line) {
  std::vector<std::string> fields;
  std::string field;
  std::istringstream iss(line);
  while (std::getline(iss, field, ',')) {
    // trim leading/trailing whitespace and any trailing CR (handles CRLF input)
    size_t start = field.find_first_not_of(" \t\r");
    size_t end = field.find_last_not_of(" \t\r");
    if (start == std::string::npos) {
      fields.push_back("");
    } else {
      fields.push_back(field.substr(start, end - start + 1));
    }
  }
  return fields;
}

static int count_csv_columns(const std::string &text) {
  std::istringstream iss(text);
  std::string line;
  if (!std::getline(iss, line)) return -1;
  return static_cast<int>(split_csv_line(line).size());
}

CsvValidationResult validate_talkgroups_csv(const std::string &content) {
  CsvValidationResult result;
  if (content.empty()) {
    result.issues.push_back({"talkgroups", "talkgroups CSV content is empty"});
    return result;
  }

  std::istringstream iss(content);
  std::string line;
  int line_num = 0;

  while (std::getline(iss, line)) {
    line_num++;
    if (line.empty()) continue;
    if (line[0] == '#') continue;  // allow comment lines
    auto fields = split_csv_line(line);
    if (fields.size() < 2) {
      result.issues.push_back({
        "talkgroups:" + std::to_string(line_num),
        "talkgroups CSV row has fewer than 2 fields (expected 8): " + std::to_string(fields.size())
      });
    }
  }

  result.ok = result.issues.empty();
  return result;
}

CsvValidationResult validate_unit_tags_csv(const std::string &content) {
  CsvValidationResult result;
  if (content.empty()) {
    result.issues.push_back({"unit-tags", "unit-tags CSV content is empty"});
    return result;
  }

  std::istringstream iss(content);
  std::string line;
  int line_num = 0;

  while (std::getline(iss, line)) {
    line_num++;
    if (line.empty()) continue;
    if (line[0] == '#') continue;
    auto fields = split_csv_line(line);
    if (fields.size() < 2) {
      result.issues.push_back({
        "unit-tags:" + std::to_string(line_num),
        "unit-tags CSV row has fewer than 2 fields (expected 2): " + std::to_string(fields.size())
      });
    }
  }

  result.ok = result.issues.empty();
  return result;
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

  if (data.contains("defaultMode") && data["defaultMode"].is_string()) {
    const std::string mode = data["defaultMode"].get<std::string>();
    if (!is_string_in(mode, {"analog", "digital"})) {
      add_issue(result, "/defaultMode", "defaultMode must be either analog or digital");
    }
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

  if (data.contains("api")) {
    if (!data["api"].is_object()) {
      add_issue(result, "/api", "api must be an object when present");
    } else {
      const auto &api = data["api"];
      if (api.contains("enabled") && !api["enabled"].is_boolean()) add_issue(result, "/api/enabled", "enabled must be a boolean");
      if (api.contains("bind") && !api["bind"].is_string()) add_issue(result, "/api/bind", "bind must be a string");
      if (api.contains("port") && !api["port"].is_number_integer()) {
        add_issue(result, "/api/port", "port must be an integer");
      } else if (api.contains("port")) {
        const int port = api["port"].get<int>();
        if (port <= 0 || port > 65535) add_issue(result, "/api/port", "port must be between 1 and 65535");
      }
      if (api.contains("token") && !api["token"].is_string()) add_issue(result, "/api/token", "token must be a string");
      if (api.value("enabled", false) && api.value("token", std::string()).empty()) {
        add_issue(result, "/api/token", "token is required when api.enabled is true");
      }
    }
  }

  if (data.contains("sources") && data["sources"].is_array()) {
    for (size_t i = 0; i < data["sources"].size(); i++) {
      const auto &source = data["sources"][i];
      const std::string base = "/sources/" + std::to_string(i);
      if (!source.is_object()) {
        add_issue(result, base, "source must be an object");
        continue;
      }

      if (!source.contains("driver") || !source["driver"].is_string()) {
        add_issue(result, base + "/driver", "driver must be a string");
      } else {
        const std::string driver = source["driver"].get<std::string>();
        if (!is_string_in(driver, {"osmosdr", "usrp", "iqfile", "sigmf", "sigmffile"})) {
          add_issue(result, base + "/driver", "driver must be one of osmosdr, usrp, iqfile, sigmf, sigmffile");
        }
      }

      if (!source.contains("rate") || !source["rate"].is_number()) add_issue(result, base + "/rate", "rate must be numeric");
      if (source.contains("center") && !source["center"].is_number()) add_issue(result, base + "/center", "center must be numeric");
      if (source.contains("error") && !source["error"].is_number()) add_issue(result, base + "/error", "error must be numeric");
      if (source.contains("ppm") && !source["ppm"].is_number()) add_issue(result, base + "/ppm", "ppm must be numeric");
      if (source.contains("gain") && !source["gain"].is_number()) add_issue(result, base + "/gain", "gain must be numeric");
      if (source.contains("ifGain") && !source["ifGain"].is_number()) add_issue(result, base + "/ifGain", "ifGain must be numeric");
      if (source.contains("bbGain") && !source["bbGain"].is_number()) add_issue(result, base + "/bbGain", "bbGain must be numeric");
      if (source.contains("digitalRecorders") && !source["digitalRecorders"].is_number_integer()) add_issue(result, base + "/digitalRecorders", "digitalRecorders must be an integer");
      if (source.contains("analogRecorders") && !source["analogRecorders"].is_number_integer()) add_issue(result, base + "/analogRecorders", "analogRecorders must be an integer");
      if (source.contains("debugRecorders") && !source["debugRecorders"].is_number_integer()) add_issue(result, base + "/debugRecorders", "debugRecorders must be an integer");
      if (source.contains("sigmfRecorders") && !source["sigmfRecorders"].is_number_integer()) add_issue(result, base + "/sigmfRecorders", "sigmfRecorders must be an integer");
      if (source.contains("enabled") && !source["enabled"].is_boolean()) add_issue(result, base + "/enabled", "enabled must be a boolean");
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

      if (!system.contains("type") || !system["type"].is_string()) {
        add_issue(result, base + "/type", "type must be a string");
        continue;
      }

      const std::string type = system["type"].get<std::string>();
      if (!is_string_in(type, {"p25", "smartnet", "conventional", "conventionalP25", "conventionalDMR", "conventionalSIGMF"})) {
        add_issue(result, base + "/type", "system type is not recognized");
      }

      if (system.contains("shortName") && !system["shortName"].is_string()) add_issue(result, base + "/shortName", "shortName must be a string");
      if (system.contains("modulation") && !system["modulation"].is_string()) add_issue(result, base + "/modulation", "modulation must be a string");
      if (system.contains("squelch") && !system["squelch"].is_number()) add_issue(result, base + "/squelch", "squelch must be numeric");
      if (system.contains("analogLevels") && !system["analogLevels"].is_number()) add_issue(result, base + "/analogLevels", "analogLevels must be numeric");
      if (system.contains("digitalLevels") && !system["digitalLevels"].is_number()) add_issue(result, base + "/digitalLevels", "digitalLevels must be numeric");
      if (system.contains("maxDev") && !system["maxDev"].is_number_integer()) add_issue(result, base + "/maxDev", "maxDev must be an integer");
      if (system.contains("filterWidth") && !system["filterWidth"].is_number()) add_issue(result, base + "/filterWidth", "filterWidth must be numeric");
      if (system.contains("conversationMode") && !system["conversationMode"].is_boolean()) add_issue(result, base + "/conversationMode", "conversationMode must be a boolean");
      if (system.contains("talkgroupsFile") && !system["talkgroupsFile"].is_string()) add_issue(result, base + "/talkgroupsFile", "talkgroupsFile must be a string");
      if (system.contains("unitTagsFile") && !system["unitTagsFile"].is_string()) add_issue(result, base + "/unitTagsFile", "unitTagsFile must be a string");
      if (system.contains("unitTagsOTA") && !system["unitTagsOTA"].is_string()) add_issue(result, base + "/unitTagsOTA", "unitTagsOTA must be a string");
      if (system.contains("unitTagsMode") && system["unitTagsMode"].is_string()) {
        const std::string mode = system["unitTagsMode"].get<std::string>();
        if (!is_string_in(mode, {"user", "ota", "OTA", "user_only", "none"})) {
          add_issue(result, base + "/unitTagsMode", "unitTagsMode must be one of user, ota, OTA, user_only, none");
        }
      } else if (system.contains("unitTagsMode")) {
        add_issue(result, base + "/unitTagsMode", "unitTagsMode must be a string");
      }

      if (type == "p25" || type == "smartnet") {
        if (!system.contains("control_channels") || !is_number_array(system["control_channels"]) || system["control_channels"].empty()) {
          add_issue(result, base + "/control_channels", "trunked systems require a non-empty control_channels array");
        }
      }

      if (type == "conventional" || type == "conventionalP25" || type == "conventionalDMR" || type == "conventionalSIGMF") {
        const bool channel_file_exists = system.contains("channelFile");
        const bool channels_exist = system.contains("channels");
        if (channel_file_exists && channels_exist) {
          add_issue(result, base, "Both \"channels\" and \"channelFile\" cannot be defined for a system!");
        }
        if (!channel_file_exists && !channels_exist) {
          add_issue(result, base, "Either \"channels\" or \"channelFile\" need to be defined for a conventional system!");
        }
        if (channel_file_exists && !system["channelFile"].is_string()) {
          add_issue(result, base + "/channelFile", "channelFile must be a string");
        }
        if (channels_exist && (!is_number_array(system["channels"]) || system["channels"].empty())) {
          add_issue(result, base + "/channels", "channels must be a non-empty numeric array");
        }
      }

      if (system.contains("audio_postprocess")) {
        if (!system["audio_postprocess"].is_object()) {
          add_issue(result, base + "/audio_postprocess", "audio_postprocess must be an object");
        } else {
          const auto &audio = system["audio_postprocess"];
          const std::vector<std::string> numeric_fields = {"highpass_hz", "lowpass_hz", "bandreject_hz", "bandreject_width_hz", "loudnorm_i", "loudnorm_tp", "loudnorm_lra"};
          const std::vector<std::string> bool_fields = {"enabled", "loudnorm", "loudnorm_two_pass"};
          for (const auto &field : numeric_fields) {
            if (audio.contains(field) && !audio[field].is_number()) add_issue(result, base + "/audio_postprocess/" + field, field + " must be numeric");
          }
          for (const auto &field : bool_fields) {
            if (audio.contains(field) && !audio[field].is_boolean()) add_issue(result, base + "/audio_postprocess/" + field, field + " must be a boolean");
          }
          if (audio.contains("ffmpeg_filter") && !audio["ffmpeg_filter"].is_string()) add_issue(result, base + "/audio_postprocess/ffmpeg_filter", "ffmpeg_filter must be a string");
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

bool write_text_file_atomic(const std::string &path, const std::string &content, std::string &error) {
  const std::string tmp_file = path + ".tmp";
  const std::string backup_file = path + ".bak";

  try {
    fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent);
  } catch (...) {
    error = "unable to create parent directories";
    return false;
  }

  {
    std::ofstream out(tmp_file, std::ios::trunc);
    if (!out.is_open()) {
      error = "unable to open temporary file for writing";
      return false;
    }
    out << content;
    if (!content.empty() && content.back() != '\n') out << std::endl;
    out.flush();
    if (!out.good()) {
      error = "failed writing temporary file";
      return false;
    }
  }

  std::ifstream existing(path);
  if (existing.good()) {
    existing.close();
    std::remove(backup_file.c_str());
    std::rename(path.c_str(), backup_file.c_str());
  }

  if (std::rename(tmp_file.c_str(), path.c_str()) != 0) {
    error = "unable to replace target file";
    std::remove(tmp_file.c_str());
    return false;
  }

  return true;
}

bool write_config_json_atomic(const std::string &config_file, const json &data, std::string &error) {
  return write_text_file_atomic(config_file, data.dump(2), error);
}

std::string resolve_system_relative_path(const std::string &config_file, const std::string &relative_or_absolute_path) {
  if (relative_or_absolute_path.empty()) return "";
  fs::path input(relative_or_absolute_path);
  if (input.is_absolute()) return input.string();
  fs::path base = fs::absolute(fs::path(config_file)).parent_path();
  return (base / input).string();
}
