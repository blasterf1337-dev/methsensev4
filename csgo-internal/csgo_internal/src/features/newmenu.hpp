#pragma once
#include "../../deps/weave-gui/include.hpp"
#include "../render.hpp"
#include "../config.hpp"
#include <optional>

static std::string format_time(time_t t) {
	using namespace std::chrono_literals;
	return std::format("{}", std::chrono::sys_seconds{ t * 1s });
}

static std::string format_time_rel(time_t t) {
	const auto format_time_ = [](time_t t, std::string str) {
		return std::vformat(t == 1 ? STRS("{} {}") : STRS("{} {}s"), std::make_format_args(t, str));
	};

	if (t < 60)
		return format_time_(t, STRS("second"));

	t /= 60;
	if (t < 60)
		return format_time_(t, STRS("minute"));

	t /= 60;
	if (t < 24)
		return format_time_(t, STRS("hour"));

	t /= 24;
	if (t < 365)
		return format_time_(t, STRS("day"));

	t /= 365;
	return format_time_(t, STRS("year"));
}

namespace __menu {
	extern void on_tab_change();
	extern void create_header();
} // namespace __menu

struct menu_t {
	using byte_t = uint8_t;
	using item_t = gui::i_renderable*;
	using items_t = std::vector<item_t>;
	using content_items_t = std::pair<items_t, items_t>;

public:
	struct nav_node_t {
		std::string m_name{};
		void* m_icon{};
		std::vector<std::string> m_children{};
		std::optional<std::pair<color_t, color_t>> m_colors{};
		content_items_t m_items{};
	};

	bool m_opened = true;
	int m_active_tab = -1;
	std::string m_active_tab_name{};
	std::string m_active_subtab_name{};
	void* m_active_tab_icon{};
	bool m_in_search = false;

	gui::containers::group_t* content = nullptr;
	gui::containers::group_t* header = nullptr;

	vec2d window_size = { 820.f, 540.f };
	gui::instance::window_t* window = new gui::instance::window_t{ window_size, {} };

	inline void change_tab(int tab_index, bool force = false) {
		if (m_in_search)
			return content->update_state(__menu::on_tab_change);

		if (m_active_tab != tab_index || force) {
			m_active_tab = tab_index;
			content->update_state(__menu::on_tab_change);
		}
	}

	inline void set_tab_attributes(const std::string& name, void* icon = nullptr, const std::string& subtab_name = "") {
		m_active_tab_name = name;
		m_active_subtab_name = subtab_name;
		m_active_tab_icon = icon;
		header->update_state(__menu::create_header);
	}

	gui::containers::group_t* create_group(std::string name, const std::vector<gui::i_renderable*>& items) {
		auto group = new gui::containers::group_t{ name };

		for (auto item: items)
			group->add(item);

		return group;
	}

	void recreate_gui(std::function<void()> callback = nullptr);
	void create_gui();
};

inline auto menu = std::make_unique<menu_t>();
