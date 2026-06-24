#include "config.hpp"
#include "cheat.hpp"
#include "utils/hotkeys.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <shlobj.h>

namespace fs = std::filesystem;

namespace config {
	static std::vector<entry_t> s_configs{};
	static std::string s_last{};

	static std::string dir() {
		return STRS("weave\\settings\\");
	}

	static bool valid_name(const std::string& name) {
		if (name.empty())
			return false;
		if (name.find_first_of("\\/:*?\"<>|") != std::string::npos)
			return false;
		return true;
	}

	static std::string path(const std::string& name) {
		return dir() + name + STRS(".cfg");
	}

	static std::unordered_map<std::string, std::string> read_names() {
		std::unordered_map<std::string, std::string> names;
		std::ifstream nf(dir() + STRS(".names"));
		if (nf.good()) {
			try {
				auto j = json_t::parse(nf);
				for (auto& [key, val] : j.items())
					names[key] = val.get<std::string>();
			}
			catch (...) {}
		}
		return names;
	}

	void refresh() {
		s_configs.clear();

		const auto d = dir();
		if (!fs::exists(d)) {
			fs::create_directories(d);
			return;
		}

		auto name_map = read_names();

		for (const auto& entry : fs::directory_iterator(d)) {
			if (!entry.is_regular_file())
				continue;

			auto p = entry.path();
			if (p.extension() != STRS(".cfg"))
				continue;

			auto stem = p.stem().string();

			entry_t e;
			e.name = stem;
			if (auto it = name_map.find(stem); it != name_map.end())
				e.display_name = it->second;
			else {
				e.display_name = stem;
				for (auto& c : e.display_name)
					if (c == '_')
						c = ' ';
			}

			s_configs.push_back(std::move(e));
		}

		std::sort(s_configs.begin(), s_configs.end(),
			[](const auto& a, const auto& b) { return a.name < b.name; });
	}

	const std::vector<entry_t>& list() {
		return s_configs;
	}

	static json_t serialize() {
		json_t root;

		root[STRS("version")] = 1;
		root[STRS("dpi_scale")] = settings->dpi_scale;

		auto& vars = root[STRS("vars")];

		for (const auto& [ptr, name] : settings->get_bools()) {
			bool cmp = false;
			if (auto def = default_settings->find_bool(name))
				cmp = *def == *ptr;
			if (!cmp)
				vars[name] = (bool)(*ptr);
		}

		for (const auto& [ptr, name] : settings->get_ints()) {
			bool cmp = false;
			if (auto def = default_settings->find_int(name))
				cmp = *def == *ptr;
			if (!cmp)
				vars[name] = (int)(*ptr);
		}

		for (const auto& [ptr, name] : settings->get_flags()) {
			bool cmp = false;
			if (auto def = default_settings->find_flag(name))
				cmp = def->get() == ptr->get();
			if (!cmp)
				vars[name] = ptr->get();
		}

		for (const auto& [ptr, name] : settings->get_colors()) {
			bool cmp = false;
			if (auto def = default_settings->find_color(name))
				cmp = def->get().u32() == ptr->get().u32();
			if (!cmp) {
				auto& jc = vars[name];
				jc[STRS("r")] = ptr->get().r();
				jc[STRS("g")] = ptr->get().g();
				jc[STRS("b")] = ptr->get().b();
				jc[STRS("a")] = ptr->get().a();
			}
		}

		auto& hk_arr = root[STRS("hotkeys")];
		for (size_t i = 0; i < hotkeys->m_hotkeys.size(); ++i) {
			auto hk = hotkeys->m_hotkeys[i];
			if (hk->m_hash == HASH("hotkey-uninitialized") || hk->m_hash == 0)
				continue;
			if (hk->m_key == 0 && hk->m_type != hotkey_t::e_type::always_on)
				continue;

			json_t hk_obj;
			hk_obj[STRS("key")] = hk->m_key;
			hk_obj[STRS("type")] = (int)hk->m_type;
			hk_obj[STRS("show")] = (bool)hk->m_show_in_binds;
			hk_obj[STRS("hash")] = hk->m_hash;
			hk_arr.push_back(std::move(hk_obj));
		}

		return root;
	}

	static void save_names(const std::unordered_map<std::string, std::string>& names) {
		try {
			json_t j;
			for (auto& [key, val] : names)
				j[key] = val;
			std::ofstream f(dir() + STRS(".names"));
			if (f.good()) {
				f << j.dump(4);
				f.close();
			}
		}
		catch (...) {}
	}

	static void deserialize(const json_t& root) {
		std::memcpy(&settings->begin, &default_settings->begin,
			(uintptr_t)&settings->end - (uintptr_t)&settings->begin);

		if (root.contains(STRS("dpi_scale"))) {
			settings->dpi_scale = root[STRS("dpi_scale")].get<int>();
		}

		if (!root.contains(STRS("vars")))
			return;

		const auto& vars = root[STRS("vars")];
		for (auto it = vars.begin(); it != vars.end(); ++it) {
			const auto& key = it.key();
			const auto& val = it.value();

			switch (val.type()) {
				case json_t::value_t::boolean: {
					if (auto ptr = settings->find_bool(key))
						*ptr = val.get<bool>();
					break;
				}
				case json_t::value_t::number_unsigned:
				case json_t::value_t::number_integer: {
					if (auto ptr_int = settings->find_int(key))
						*ptr_int = val.get<int>();
					else if (auto ptr_flags = settings->find_flag(key))
						ptr_flags->get() = val.get<uint32_t>();
					break;
				}
				case json_t::value_t::object: {
					if (auto clr_ptr = settings->find_color(key)) {
						if (val.contains(STRS("r"))) clr_ptr->m_value[0] = val[STRS("r")].get<float>() / 255.f;
						if (val.contains(STRS("g"))) clr_ptr->m_value[1] = val[STRS("g")].get<float>() / 255.f;
						if (val.contains(STRS("b"))) clr_ptr->m_value[2] = val[STRS("b")].get<float>() / 255.f;
						if (val.contains(STRS("a"))) clr_ptr->m_value[3] = val[STRS("a")].get<float>() / 255.f;
					}
					break;
				}
				default:
					break;
			}
		}

		if (root.contains(STRS("hotkeys"))) {
			for (const auto& hk_obj : root[STRS("hotkeys")]) {
				auto hash = hk_obj[STRS("hash")].get<uint32_t>();
				if (hash == HASH("hotkey-uninitialized") || hash == 0)
					continue;

				auto it = std::find_if(hotkeys->m_hotkeys.begin(), hotkeys->m_hotkeys.end(),
					[hash](auto hk) { return hk->m_hash == hash; });
				if (it == hotkeys->m_hotkeys.end())
					continue;

				(*it)->m_key = hk_obj[STRS("key")].get<int>();
				(*it)->m_type = (hotkey_t::e_type)hk_obj[STRS("type")].get<int>();
				if (hk_obj.contains(STRS("show")))
					(*it)->m_show_in_binds = hk_obj[STRS("show")].get<bool>();
			}
		}
	}

	bool save(const std::string& name, const std::string& display_name) {
		if (!valid_name(name))
			return false;

		const auto d = dir();
		if (!fs::exists(d))
			fs::create_directories(d);

		auto dn = display_name;
		if (dn.empty()) {
			auto names = read_names();
			auto it = names.find(name);
			dn = it != names.end() ? it->second : name;
		}
		auto root = serialize();
		root[STRS("name")] = dn;

		std::ofstream file(path(name), std::ios::binary);
		if (!file.good())
			return false;

		const auto json_str = root.dump(4);
		file.write(json_str.data(), json_str.size());
		file.close();

		{
			auto names = read_names();
			names[name] = dn;
			save_names(names);
		}

		set_last(name);
		return true;
	}

	bool load(const std::string& name) {
		if (!valid_name(name))
			return false;

		const auto p = path(name);
		std::ifstream file(p, std::ios::binary | std::ios::ate);
		if (!file.good())
			return false;

		auto size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::string json_str((size_t)size, '\0');
		file.read(json_str.data(), size);
		file.close();

		try {
			auto root = json_t::parse(json_str);
			deserialize(root);
		}
		catch (const std::exception& e) {
			PUSH_LOG("failed to parse config '%s': %s\n", name.c_str(), e.what());
			return false;
		}

		set_last(name);
		return true;
	}

	bool remove(const std::string& name) {
		if (!valid_name(name))
			return false;

		const auto p = path(name);
		if (!fs::exists(p))
			return false;

		auto removed = fs::remove(p);
		if (removed) {
			auto names = read_names();
			names.erase(name);
			save_names(names);
		}
		return removed;
	}

	bool rename(const std::string& old_name, const std::string& new_name) {
		if (!valid_name(old_name) || !valid_name(new_name))
			return false;

		const auto old_path = path(old_name);
		const auto new_path = path(new_name);

		if (!fs::exists(old_path))
			return false;

		if (fs::exists(new_path))
			return false;

		fs::rename(old_path, new_path);

		auto names = read_names();
		auto it = names.find(old_name);
		if (it != names.end()) {
			names[new_name] = it->second;
			names.erase(it);
		}
		save_names(names);
		return true;
	}

	bool save_current() {
		return save(s_last);
	}

	bool load_last() {
		if (s_last.empty())
			return false;
		return load(s_last);
	}

	void set_last(const std::string& name) {
		s_last = name;

		try {
			std::ofstream file(dir() + STRS(".last"), std::ios::binary);
			if (file.good()) {
				file.write(name.data(), name.size());
				file.close();
			}
		}
		catch (...) {}
	}

	bool set_name(const std::string& name, const std::string& display_name) {
		if (!valid_name(name) || display_name.empty())
			return false;

		auto names = read_names();
		names[name] = display_name;
		save_names(names);
		return true;
	}

	std::string get_last() {
		if (s_last.empty()) {
			try {
				const auto last_path = dir() + STRS(".last");
				std::ifstream file(last_path, std::ios::binary | std::ios::ate);
				if (file.good()) {
					auto size = file.tellg();
					file.seekg(0, std::ios::beg);
					std::string buf((size_t)size, '\0');
					file.read(buf.data(), size);
					s_last = buf;
				}
			}
			catch (...) {}
		}
		return s_last;
	}
}
