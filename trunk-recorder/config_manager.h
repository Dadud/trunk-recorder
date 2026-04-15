#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <vector>
#include <json.hpp>

struct ConfigValidationIssue {
  std::string path;
  std::string message;
  std::string severity;
};

struct ConfigValidationResult {
  bool ok = false;
  std::vector<ConfigValidationIssue> issues;
};

ConfigValidationResult validate_config_json(const nlohmann::json &data);
std::string redact_config_json(const nlohmann::json &data, bool reveal_secrets = false);
bool write_config_json_atomic(const std::string &config_file, const nlohmann::json &data, std::string &error);
std::string resolve_system_relative_path(const std::string &config_file, const std::string &relative_or_absolute_path);
bool write_text_file_atomic(const std::string &path, const std::string &content, std::string &error);

#endif
