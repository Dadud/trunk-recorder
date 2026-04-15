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

struct CsvValidationIssue {
  std::string path;
  std::string message;
};

struct CsvValidationResult {
  bool ok = false;
  std::vector<CsvValidationIssue> issues;
};

ConfigValidationResult validate_config_json(const nlohmann::json &data);
std::string redact_config_json(const nlohmann::json &data, bool reveal_secrets = false);
bool write_config_json_atomic(const std::string &config_file, const nlohmann::json &data, std::string &error);
std::string resolve_system_relative_path(const std::string &config_file, const std::string &relative_or_absolute_path);
bool write_text_file_atomic(const std::string &path, const std::string &content, std::string &error);

/**
 * Basic structural validation for helper CSV files.
 * These check column counts per row and non-emptiness only.
 * Full semantic parsing (type checks, talkgroup number validation, etc.)
 * is performed by trunk-recorder's talkgroups.cc / unit_tags.cc at runtime.
 */
CsvValidationResult validate_talkgroups_csv(const std::string &content);
CsvValidationResult validate_unit_tags_csv(const std::string &content);

#endif
