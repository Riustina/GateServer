// ConfigManager.cpp

#include "ConfigManager.h"
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <iostream>

ConfigManager::ConfigManager() {
	// 获取当前路径并拼接配置文件路径
	boost::filesystem::path config_path = boost::filesystem::current_path() / "config.ini";
	std::cerr << "[ConfigManager.cpp] 函数 [ConfigManager()] config path: " << config_path.string() << std::endl;

	// 读取配置文件
	boost::property_tree::ptree pt;
	boost::property_tree::ini_parser::read_ini(config_path.string(), pt);

	// 遍历配置树，填充_config_map
	for (const auto& sectionPair : pt) {
		const std::string& sectionName = sectionPair.first;
		const boost::property_tree::ptree& sectionTree = sectionPair.second;
		SectionInfo sectionInfo;
		for (const auto& keyValuePair : sectionTree) {
			const std::string& key = keyValuePair.first;
			const std::string& value = keyValuePair.second.get_value<std::string>();
			sectionInfo._section_datas[key] = value;
		}
		this->_config_map[sectionName] = sectionInfo;
	}

	// 遍历每一项
	std::cerr << "[ConfigManager.cpp] 函数 [ConfigManager()] 遍历配置项:" << std::endl;
	for (const auto& section : _config_map) {
		std::cerr << "section: " << section.first << std::endl;
		for (const auto& keyValue : section.second._section_datas) {
			std::cerr << "    key: " << keyValue.first << ", value: " << keyValue.second << std::endl;
		}
	}
}