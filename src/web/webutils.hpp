// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder


#ifndef WEB_UTILS_HPP
#define WEB_UTILS_HPP

#include <string>
#include <nlohmann/json_fwd.hpp>

bool mergeData(nlohmann::json &orig, const nlohmann::json &patch, bool merge_null);
std::string encodeConfigDataForDb(const std::string& data);
bool decodeConfigDataFromDb(const std::string& db_data, std::string& data);
void logConfigDataSaveFailure(const char* config_type, int account_id, const std::string& world_name, const std::string& data);
void logConfigDataSaveFailure(const char* config_type, int account_id, int char_id, const std::string& world_name, const std::string& data);

#endif
