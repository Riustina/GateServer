// ConfigManager.h
#pragma once

#include <map>
#include <string>
#include <iostream>

struct SectionInfo {
    SectionInfo() {}
    ~SectionInfo() {
        _section_datas.clear();
    }

    SectionInfo(const SectionInfo& src) {
        _section_datas = src._section_datas;
    }

    SectionInfo& operator = (const SectionInfo& src) {
        if (&src == this) return *this;
        this->_section_datas = src._section_datas;
        return *this;
    }

    std::map<std::string, std::string> _section_datas;

    // 优化：返回 const string& 避免字符串拷贝
    const std::string& operator[](const std::string& key) const {
        auto it = _section_datas.find(key);
        if (it == _section_datas.end()) {
            std::cerr << "[ConfigManager.h] 函数 [SectionInfo::operator[]] key ["
                << key << "] not found" << std::endl;
            static const std::string empty_str = "";
            return empty_str;
        }
        return it->second;
    }
};

class ConfigManager
{
public:
    ~ConfigManager() {
        _config_map.clear();
    }

    // 优化：返回 const SectionInfo& 避免拷贝整个 map
    const SectionInfo& operator[](const std::string& section) const {
        auto it = _config_map.find(section);
        if (it == _config_map.end()) {
            std::cerr << "[ConfigManager.h] 函数 [ConfigManager::operator[]] section ["
                << section << "] not found" << std::endl;
            static const SectionInfo empty_section; // 静态空对象用于安全返回
            return empty_section;
        }
        return it->second;
    }

    ConfigManager& operator=(const ConfigManager& src) {
        if (&src == this) return *this;
        this->_config_map = src._config_map;
        return *this;
    }

    ConfigManager(const ConfigManager& src) {
        this->_config_map = src._config_map;
    }

    ConfigManager();

private:
    std::map<std::string, SectionInfo> _config_map;
};