// ConfigManager.h

#pragma once

#include <map>
#include <string>

struct SectionInfo {
    SectionInfo() {}
    ~SectionInfo() {
        _section_datas.clear();
    }

    SectionInfo(const SectionInfo& src) {
        _section_datas = src._section_datas;
    }

    SectionInfo& operator = (const SectionInfo& src) {
        if (&src == this) {
            return *this;
        }

        this->_section_datas = src._section_datas;
    }

    std::map<std::string, std::string> _section_datas;
    std::string  operator[](const std::string& key) {
        if (_section_datas.find(key) == _section_datas.end()) {
            return "";
        }
        // 这里可以添加一些边界检查  
        return _section_datas[key];
    }
};

class ConfigManager
{
public:
    ~ConfigManager() {
        _config_map.clear();
    }
    SectionInfo operator[](const std::string& section) {
        if (_config_map.find(section) == _config_map.end()) {
            return SectionInfo();
        }
        return _config_map[section];
    }


    ConfigManager& operator=(const ConfigManager& src) {
        if (&src == this) {
            return *this;
        }

        this->_config_map = src._config_map;
    };

    ConfigManager(const ConfigManager& src) {
        this->_config_map = src._config_map;
    }

    ConfigManager();
private:
    // 存储section和key-value对的map  
    std::map<std::string, SectionInfo> _config_map;
};

