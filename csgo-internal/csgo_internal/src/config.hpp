#pragma once
#include "vars.hpp"
#include <string>
#include <vector>

namespace config {
	struct entry_t {
		std::string name;
		std::string display_name;
	};

	void refresh();
	const std::vector<entry_t>& list();

	bool save(const std::string& name, const std::string& display_name = "");
	bool load(const std::string& name);
	bool remove(const std::string& name);
	bool rename(const std::string& old_name, const std::string& new_name);

	bool save_current();
	bool load_last();
	void set_last(const std::string& name);
	std::string get_last();

	bool set_name(const std::string& name, const std::string& display_name);
}
