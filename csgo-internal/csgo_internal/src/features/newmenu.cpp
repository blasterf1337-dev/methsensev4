#include "newmenu.hpp"
#include "../features/misc.hpp"
#include "../features/visuals/logs.hpp"
#include "../features/visuals/skin_changer.hpp"
#include "../lua/api.hpp"
#include "../render.hpp"
#include "../utils/hotkeys.hpp"
#include "../utils/sha2.hpp"
#include "../utils/threading.hpp"
#include "../vars.hpp"
#include "localization.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include "../../deps/fontawesome/include.hpp"

#define ICON(x) fa::icons.at(STRS(x))

using namespace gui;
using namespace std::chrono_literals;

static std::string search_phrase{};

static std::string create_config_name{};
static int active_config_index{};
static std::vector<config::entry_t> active_configs{};
static std::string edit_config_display_name{};

static std::string import_script_name{};
static int active_script_index{};
static std::string active_script_rename{};
static std::vector<fs::path> active_scripts{};

STFI utils::bytes_t read_file_bin(std::string path) {
	utils::bytes_t result;

	std::ifstream ifs{ path, std::ios::binary | std::ios::ate };
	if (ifs) {
		std::ifstream::pos_type pos = ifs.tellg();
		result.resize((size_t)pos);

		ifs.seekg(0, std::ios::beg);
		ifs.read((char*)result.data(), pos);
	}

	return result;
}

STFI std::vector<fs::path> get_files_with_extension(const std::string& directory, const std::string& extension) {
	std::vector<fs::path> filenames{};

	for (const auto& entry : fs::directory_iterator{ directory })
		if (entry.is_regular_file() && entry.path().extension() == extension)
			filenames.push_back(entry.path());

	return filenames;
}

STFI std::string remove_extension(const std::string& s, const std::string& ext) {
	if (s.find(ext) == std::string::npos)
		return s;

	return s.substr(0, s.size() - ext.size());
}

static std::function<std::string(int)> get_slider_format(uint8_t type) {
	switch (type) {
		case 1:
			return [](int value) { return dformat(STR("{}%"), value); };
		case 2:
			return [](int value) {
				if (value > 100)
					return dformat(STR("Health + {}hp"), value - 100);

				return dformat(STR("{}hp"), value);
			};
		case 3:
			return [](int value) {
				if (value == 0)
					return STRS("Auto");

				return dformat("{}%", value);
			};
		case 4:
			return [](int value) { return dformat((char*)u8"{}°", value); };
		case 5:
			return [](int value) { return dformat(STR("{}px"), value); };
	}

	return [](int value) { return std::format("{}", value); };
}

static void update_dpi_scale(bool updated) {
	if (!updated)
		return;

	switch (settings->dpi_scale) {
		case 0: dpi::change_scale(1.f); break;
		case 1: dpi::change_scale(1.25f); break;
		case 2: dpi::change_scale(1.5f); break;
		case 3: dpi::change_scale(1.75f); break;
		case 4: dpi::change_scale(2.f); break;
	}
}

static i_renderable* subtabs(const std::vector<std::string>& items, int& i) {
	auto column = new containers::group_t{};

	c_style style{};
	style.text_color = { 127, 127, 127 };
	style.controls_text_padding = { 16.f, 4.f };
	style.container_padding.x = 24.f;
	style.transparent_clickable = true;
	style.invisible_clickable = true;
	style.small_text = false;
	style.window_padding.y = 0.f;

	column->set_style(style);

	for (const auto& name : items) {
		auto item = new controls::button_t{ name, [=]() { menu->change_tab(i); }, vec2d{ 164.f - 16.f, 26.f } * dpi::_get_actual_scale(), e_items_align::start };
		style.small_text = true;

		style.set_callback([=](c_style& s) {
			const auto active = menu->m_active_tab == i;
			s.bold_text = active;
			s.text_color = active ? color_t{ 255, 161, 0 } : color_t{ 127, 127, 127 };
		});

		item->set_style(style);
		column->add(item);
		++i;
	}

	return column;
}

static void create_navigation(const std::vector<menu_t::nav_node_t>& nodes) {
	auto nav = new containers::group_t{ "", vec2d{ 172.f, 316.f } };
	nav->set_position_type(e_position_type::absolute);
	nav->set_position(vec2d{ 16.f - styles::get().container_padding.x, 89.f });
	const auto tab_size = vec2d{ 164.f, 32.f };

	nav->override_style().bold_text = true;
	nav->override_style().text_color = { 127, 127, 127 };
	nav->override_style().transparent_clickable = true;

	int i = 0;
	for (auto& node : nodes) {
		if (!node.m_children.empty()) {
			auto tab = new controls::collapse_t{ node.m_name, []() {}, tab_size, node.m_icon };
			tab->set_item(subtabs(node.m_children, i));
			nav->add(tab);
		}
		else {
			auto tab = new controls::button_t{ node.m_name, [=]() { menu->change_tab(i); }, tab_size, e_items_align::start, node.m_icon };

			tab->override_style().bold_text = true;
			tab->override_style().text_color = { 127, 127, 127 };
			tab->override_style().transparent_clickable = true;

			tab->override_style().clickable_hovered = { 100, 100, 100 };
			tab->override_style().set_callback([=](c_style& s) {
				const auto active = menu->m_active_tab == i;
				s.text_color = active ? color_t{ 255, 161, 0 } : color_t{ 127, 127, 127 };
				s.bold_text = active;
			});

			if (node.m_colors.has_value()) {
				tab->override_style().accent_color1 = node.m_colors->first;
				tab->override_style().accent_color2 = node.m_colors->second;
				tab->override_style().gradient_text_in_button = true;
			}

			nav->add(tab);
			++i;
		}
	}

	menu->window->add(nav);
}

static void create_navigation() {
	std::vector<menu_t::nav_node_t> nodes{};

	auto create_tab = [&](const std::string& name, void* icon, const std::vector<std::string>& subtabs = {}) -> menu_t::nav_node_t& {
		auto& tab = nodes.emplace_back();
		tab.m_name = name;
		tab.m_children = subtabs;
		tab.m_icon = icon;
		return tab;
	};

	create_tab(LOCALIZE("tab.ragebot"), nullptr,
		{ LOCALIZE("tab.ragebot.aimbot"),
		 LOCALIZE("tab.ragebot.antiaim"),
		 LOCALIZE("tab.ragebot.misc") });

	create_tab(LOCALIZE("tab.visuals"), nullptr,
		{ LOCALIZE("tab.visuals.players"),
		 LOCALIZE("tab.visuals.local"),
		 LOCALIZE("tab.visuals.world"),
		 LOCALIZE("tab.visuals.other") });

	create_tab(LOCALIZE("tab.misc"), nullptr);
	create_tab(LOCALIZE("tab.skins"), nullptr);
	create_tab(LOCALIZE("tab.scripts"), nullptr);
	create_tab(LOCALIZE("tab.settings"), nullptr);
	create_tab(LOCALIZE("tab.hotkeys"), nullptr);

	return create_navigation(nodes);
}

static void create_footer() {
	auto footer = new containers::group_t{ "", vec2d{ 0.f, 32.f } };
	c_style style{};
	style.container_padding = {};
	style.disable_scroll = true;
	footer->set_style(style);

	footer->set_position_type(e_position_type::absolute);
	footer->set_position(vec2d{ 0.f, 450.f });

	auto row = new containers::row_t{ e_items_align::center };
	c_style row_style{};
	row_style.container_padding = {};
	row_style.window_padding = {};
	row->set_style(row_style);

	auto icon = controls::text(ICON("m") + STRS("  methsense"));
	icon->override_style().bold_text = true;
	icon->override_style().gradient_text_in_button = true;
	icon->override_style().accent_color1 = gui::accent_color1;
	icon->override_style().accent_color2 = gui::accent_color2;
	row->add(icon);

	footer->add(row);
	menu->window->add(footer);
}

void menu_t::create_gui() {
	menu->header = new containers::group_t{};
	menu->content = new containers::group_t{ "", vec2d{ 532.f + 8.f, 420.f } };
	menu->content->override_style().scroll_fade = true;

	__menu::create_header();
	create_footer();
	__menu::on_tab_change();

	menu->window->add(menu->header);
	menu->window->add(menu->content);

	create_navigation();
}

void menu_t::recreate_gui(std::function<void()> callback) {
	static std::mutex mtx{};
	THREAD_SAFE(mtx);

	menu->window->update_state([callback]() {
		if (callback != nullptr)
			callback();

		menu->window->clear();

		menu->header = new containers::group_t{};
		menu->content = new containers::group_t{ "", vec2d{ 532.f + 8.f, 420.f } };
		menu->content->override_style().scroll_fade = true;

		__menu::create_header();
		create_footer();
		__menu::on_tab_change();

		menu->window->add(menu->header);
		menu->window->add(menu->content);

		create_navigation();
	});
}

static auto create_popup(vec2d size, int flags) {
	auto popup = menu->window->create_popup();
	popup->m_flags.add(flags);
	popup->set_position(globals().mouse_position - menu->window->get_position());
	popup->set_size(size * dpi::_get_actual_scale());
	return popup;
}

static void confirm_action(const std::string& text, callback_t action, bool prefer_no = false) {
	auto popup = create_popup({ 0.f, 80.f }, container_flag_is_popup | popup_flag_animation | popup_flag_close_on_click | container_flag_visible);

	auto group = new containers::group_t{};
	group->add(controls::text(text));

	auto row = new containers::row_t{};

	group->override_style().container_padding = { 8.f, 8.f };
	group->override_style().window_padding.y = 32.f;

	row->add(controls::button(LOCALIZE("yes"), action, vec2d{ 0.f, 24.f } * dpi::_get_actual_scale(), e_items_align::center, nullptr, !prefer_no));
	row->add(controls::button(
		LOCALIZE("no"), [=]() { popup->close(); }, vec2d{ 0.f, 24.f } * dpi::_get_actual_scale(), e_items_align::center, nullptr, prefer_no));

	group->add(row);
	popup->add(group);
}

static i_renderable* config_actions() {
	auto cfg_load = [=]() {
		menu->content->update_state([=]() {
			menu->content->set_loading();
			menu->window->lock();
			std::thread([=]() {
				auto& cfg = active_configs[active_config_index];
				config::load(cfg.name);

				__menu::on_tab_change();
				menu->window->unlock();
				menu->content->stop_loading();

				cheat_logs->add_info(dformat(STRS("\"{}\" has been loaded!"), cfg.display_name));
			}).detach();
		});
	};

	auto cfg_save = [=]() {
		confirm_action(LOCALIZE("config.save_confirmation"), []() {
			menu->content->update_state([=]() {
				menu->content->set_loading();
				menu->window->lock();
				std::thread([=]() {
					auto& cfg = active_configs[active_config_index];
					config::save(cfg.name);

					__menu::on_tab_change();
					menu->window->unlock();
					menu->content->stop_loading();
					cheat_logs->add_info(dformat(STRS("\"{}\" has been saved!"), cfg.display_name));
				}).detach();
			});
		});
	};

	auto cfg_delete = [=]() {
		confirm_action(
			LOCALIZE("config.delete_confirmation"),
			[=]() {
				menu->content->update_state([=]() {
					menu->content->set_loading();
					menu->window->lock();
					std::thread([=]() {
						auto& cfg = active_configs[active_config_index];
						config::remove(cfg.name);
						__menu::on_tab_change();
						menu->window->unlock();
						menu->content->stop_loading();
					}).detach();
				});
			},
			true);
	};

	auto group = new containers::group_t{};

	c_style s{};
	s.container_padding = { 0.f, 8.f };
	s.window_padding.y = 32.f;
	s.bold_text = true;

	group->set_style(s);

	auto btn_size = vec2d{ 116.f, 32.f } * dpi::_get_actual_scale();
	s.container_padding = { 8.f, 8.f };

	{
		auto r = new containers::row_t{};
		r->set_style(s);
		r->add(controls::button(LOCALIZE("config.load"), cfg_load, btn_size, e_items_align::center, nullptr, true));
		r->add(controls::button(LOCALIZE("config.save"), cfg_save, btn_size, e_items_align::center, nullptr, false));
		group->add(r);
	}

	{
		auto r = new containers::row_t{};
		r->set_style(s);
		r->add(controls::button(LOCALIZE("config.delete"), cfg_delete, btn_size, e_items_align::center, nullptr, false));
		group->add(r);
	}

	return group;
}

static i_renderable* script_actions() {
	auto script_load = [=]() {
		menu->content->update_state([=]() {
			menu->content->set_loading();
			menu->window->lock();
			std::thread([=]() {
				auto& active_script = active_scripts[active_script_index];
				auto bytes = read_file_bin(active_script.string());
				if (!bytes.empty()) {
					auto name = active_script.stem().string();
				//	lua::load_script(name, name, { bytes.begin(), bytes.end() });
				}

				menu->recreate_gui();
				menu->window->unlock();
				menu->content->stop_loading();
			}).detach();
		});
	};

	auto script_unload = [=]() {
		menu->content->update_state([=]() {
			menu->content->set_loading();
			menu->window->lock();
			std::thread([=]() {
				auto& active_script = active_scripts[active_script_index];
			//	lua::unload_script(active_script.stem().string());
				menu->recreate_gui();
				menu->window->unlock();
				menu->content->stop_loading();
			}).detach();
		});
	};

	auto script_delete = [=]() {
		confirm_action(
			STRS("Are you sure you want to delete this script?"), [=]() {
				menu->content->update_state([=]() {
					menu->content->set_loading();
					menu->window->lock();
					std::thread([=]() {
						auto& active_script = active_scripts[active_script_index];
						fs::remove(active_script);
						__menu::on_tab_change();
						menu->window->unlock();
						menu->content->stop_loading();
					}).detach();
				});
			},
			true);
	};

	auto group = new containers::group_t{};

	c_style s{};
	s.container_padding = { 0.f, 8.f };
	s.window_padding.y = 32.f;
	s.bold_text = true;

	group->set_style(s);

	auto btn_size = vec2d{ 116.f, 32.f } * dpi::_get_actual_scale();
	s.container_padding = { 8.f, 8.f };

	{
		auto r = new containers::row_t{};
		r->set_style(s);

		r->add(controls::button(LOCALIZE(("script.load")), script_load, btn_size, e_items_align::center, nullptr, true));

		group->add(r);
	}

	{
		auto r = new containers::row_t{};
		r->set_style(s);
		r->add(controls::button(LOCALIZE(("script.unload")), script_unload, btn_size, e_items_align::center, nullptr, false));
		r->add(controls::button(LOCALIZE(("script.delete")), script_delete, btn_size, e_items_align::center, nullptr, false));
		group->add(r);
	}

	return group;
}

static auto create_nested_items() {
	auto popup = create_popup({}, gui::container_flag_is_popup | gui::popup_flag_animation | gui::container_flag_visible);
	popup->override_style().clickable_color = { 24, 24, 24 };
	popup->override_style().clickable_hovered = { 28, 28, 28 };

	return popup;
}

static void hotkeys_tab() {
	auto group = new containers::column_t{};

	auto add_hotkey = [&](std::string name, hotkey_t& hotkey, void* icon = nullptr) {
		auto nest_items = [h = &hotkey]() {
			auto items = create_nested_items();
			items->add(controls::checkbox(STRS("Show in hotkeys list"), &h->m_show_in_binds, []() { config::save_current(); }));
		};

		group->add(controls::hotkey(name, (int*)&hotkey.m_type, &hotkey.m_key, icon, nest_items, []() { config::save_current(); }));
	};

	add_hotkey(LOCALIZE(("hotkey.doubletap")), hotkeys->doubletap, nullptr);
	add_hotkey(LOCALIZE(("hotkey.hide_shot")), hotkeys->hide_shot, nullptr);
	add_hotkey(LOCALIZE(("hotkey.override_damage")), hotkeys->override_damage, nullptr);
	add_hotkey(LOCALIZE(("hotkey.fake_duck")), hotkeys->fake_duck, nullptr);
	add_hotkey(LOCALIZE(("hotkey.desync_inverter")), hotkeys->desync_inverter, nullptr);
	add_hotkey(LOCALIZE(("hotkey.slow_walk")), hotkeys->slow_walk, nullptr);
	add_hotkey(LOCALIZE(("hotkey.freestand")), hotkeys->freestand, nullptr);

	add_hotkey(LOCALIZE(("hotkey.manual_right")), hotkeys->manual_right, nullptr);
	add_hotkey(LOCALIZE(("hotkey.manual_left")), hotkeys->manual_left, nullptr);
	add_hotkey(LOCALIZE(("hotkey.manual_back")), hotkeys->manual_back, nullptr);
	add_hotkey(LOCALIZE(("hotkey.manual_forward")), hotkeys->manual_forward, nullptr);

	add_hotkey(LOCALIZE(("hotkey.peek_assist")), hotkeys->peek_assist, nullptr);
	add_hotkey(LOCALIZE(("hotkey.thirdperson")), hotkeys->thirdperson, nullptr);

	menu->content->add(group);
}

static i_renderable* configs_selectables(int* value, const std::vector<std::string>& items, void* texture, std::function<void(bool)> on_update) {
	auto group = new containers::group_t{};
	group->override_style().container_padding = { 0.f, 0.f };
	group->override_style().text_color = { 127, 127, 127 };
	group->override_style().transparent_clickable = true;

	const auto button_size = vec2d{ 256.f - 8.f, 32.f } * dpi::_get_actual_scale();

	int i = 0;
	for (const auto& it : items) {
		auto callback = [on_update, i, value]() {
			const auto updated = i != *value;
			on_update(updated);
			*value = i;
		};

		auto btn = controls::button(it, callback, button_size, e_items_align::start, texture);
		btn->override_style().container_padding.y = 8.f;
		btn->override_style().transparent_clickable = true;
		btn->override_style().set_callback([value, i](c_style& s) {
			if (*value == i) {
				s.text_color = color_t{ 255, 255, 255 };
				s.bold_text = true;
				s.transparent_clickable = false;
				s.clickable_hovered = { 24, 24, 24 };
			}
			else {
				s.text_color = color_t{ 127, 127, 127 };
				s.bold_text = false;
				s.transparent_clickable = true;
				s.clickable_hovered = { 75, 75, 75 };
			}
			});
		group->add(btn);
		++i;
	}

	return group;
}
static void settings_tab() {
	config::refresh();
	active_configs = config::list();

	std::vector<std::string> configs_list{};
	configs_list.reserve(active_configs.size());
	for (auto& config : active_configs)
		configs_list.emplace_back(config.display_name);

	menu->content->add(controls::text(STRS("Configs"), true));

	{
		auto row = new containers::row_t{};
		auto config_name = controls::text_input(&create_config_name, LOCALIZE("config.name_input"));
		c_style s{};
		s.item_width = 176.f;
		config_name->set_style(s);
		s.container_padding = { 8.f, 0.f };
		row->set_style(s);
		row->add(config_name);
		row->add(controls::button(LOCALIZE("config.create"), [=]() {
			if (create_config_name.empty()) {
				msgbox::add(STRS("Enter the config name!"));
				return;
			}
			config::save(create_config_name);
			create_config_name.clear();
			menu->recreate_gui();
		}, vec2d{ 64.f, 32.f } * dpi::_get_actual_scale()));
		menu->content->add(row);
	}

	if (configs_list.empty()) {
		c_style s{};
		s.text_color = { 127, 127, 127 };
		auto t = controls::text(STRS("There's no configs at the moment"));
		t->set_style(s);
		menu->content->add(t);
	} else {
		menu->content->add(configs_selectables(&active_config_index, configs_list, nullptr, [=](bool) {
			menu->recreate_gui();
		}));
	}

	if (!active_configs.empty()) {
		active_config_index = std::clamp<int>(active_config_index, 0, (int)active_configs.size() - 1);
		auto& cfg = active_configs[active_config_index];
		menu->content->add(controls::text(cfg.name, true));
		menu->content->add(config_actions());
		menu->content->add(controls::text(STRS("Display name"), true));
		{
			auto row = new containers::row_t{};
			edit_config_display_name = cfg.display_name;
			auto name_input = controls::text_input(&edit_config_display_name, STRS("Enter display name..."));
			c_style s{};
			s.item_width = 176.f;
			name_input->set_style(s);
			s.container_padding = { 8.f, 0.f };
			row->set_style(s);
			row->add(name_input);
			row->add(controls::button(LOCALIZE("config.set_name"), [=]() {
				if (edit_config_display_name.empty()) {
					msgbox::add(STRS("Enter a display name!"));
					return;
				}
				config::set_name(cfg.name, edit_config_display_name);
				menu->recreate_gui();
			}, vec2d{ 64.f, 32.f } * dpi::_get_actual_scale()));
			menu->content->add(row);
		}
	}
}

static void scripts_tab() {
	active_scripts = get_files_with_extension(STRS("weave\\lua"), STRS(".lua"));

	std::vector<std::string> scripts_list{};
	scripts_list.reserve(active_scripts.size());
	for (auto& script : active_scripts)
		scripts_list.emplace_back(remove_extension(script.filename().string(), STRS(".lua")));

	menu->content->add(controls::text(STRS("Scripts"), true));

	{
		auto row = new containers::row_t{};
		row->override_style().container_padding = { 4.f, 0.f };
		auto refresh = controls::button(ICON("arrows-rotate"), [=]() { menu->recreate_gui(); }, vec2d{ 16, 16 } * dpi::_get_actual_scale());
		refresh->override_style().tooltip = STRS("Refresh");
		refresh->override_style().transparent_clickable = true;
		refresh->override_style().invisible_clickable = true;
		row->add(refresh);

		auto open_folder = controls::button(ICON("folder"), []() {
			ShellExecuteA(0, 0, (fs::current_path() / fs::path(STRS("weave/lua"))).string().c_str(), 0, 0, SW_SHOWNORMAL);
		}, vec2d{ 16, 16.f } * dpi::_get_actual_scale());
		open_folder->override_style().tooltip = STRS("Open scripts location");
		open_folder->override_style().transparent_clickable = true;
		open_folder->override_style().invisible_clickable = true;
		row->add(open_folder);

		row->override_style().container_padding.y = 8;
		menu->content->add(row);
	}

	if (scripts_list.empty()) {
		auto t = controls::text(ICON("empty-set") + STRS(" No scripts found"));
		t->override_style().text_color = { 127, 127, 127 };
		menu->content->add(t);
	} else {
		menu->content->add(configs_selectables(&active_script_index, scripts_list, nullptr, [=](bool) {
			menu->recreate_gui();
		}));
	}

	if (!active_scripts.empty()) {
		active_script_index = std::clamp<int>(active_script_index, 0, (int)active_scripts.size() - 1);
		auto& active_script = active_scripts[active_script_index];
		menu->content->add(controls::text_input(&active_script_rename, STRS("Enter script name...")));
		menu->content->add(controls::prompt(STRS("Path: "), active_script.string()));
		menu->content->add(script_actions());
	}
}

static void misc_tab() {
	menu->content->add(controls::text(STRS("Movement"), true));

	menu->content->add(controls::checkbox(STRS("Bunny-hop"), &settings->movement.bhop));
	menu->content->add(controls::checkbox(STRS("Auto-strafe"), &settings->movement.autostrafe));

	if (settings->movement.autostrafe)
		menu->content->add(controls::slider(STRS("Strafe smoothness"), &settings->movement.strafe_smooth, 0, 200));

	menu->content->add(controls::checkbox(STRS("Fast stop"), &settings->movement.fast_stop));
	menu->content->add(controls::dropbox(STRS("Legs movement"), &settings->movement.leg_movement, { STRS("Avoid slide"), STRS("Always slide") }, false));
	menu->content->add(controls::dropbox(STRS("Animation breaker"), (int*)&settings->movement.anim_changers, { STRS("Visual Lag") }, true));

	menu->content->add(controls::text(STRS("Other"), true));
	menu->content->add(controls::checkbox(STRS("Hit sound"), &settings->misc.hitsound));

	if (settings->misc.hitsound)
		menu->content->add(controls::slider(STRS("Hit sound volume"), &settings->misc.hitsound_volume, 1, 100, [](int v) { return std::format("{}%", v); }));

	menu->content->add(controls::checkbox(STRS("Unlock inventory"), &settings->misc.unlock_inventory));
	menu->content->add(controls::checkbox(STRS("Unlock hidden convars"), &settings->misc.unlock_cvars));
	menu->content->add(controls::checkbox(STRS("Preserve killfeed"), &settings->misc.preserve_killfeed));
	menu->content->add(controls::checkbox(STRS("Knife-bot"), &settings->misc.knife_bot));
	menu->content->add(controls::dropbox(STRS("Ragebot logs"), (int*)&settings->misc.log_filter, { STRS("Aimbot info"), STRS("Damage received"), STRS("Damage dealt"), STRS("Missed shots") }, true));

	menu->content->add(controls::text(STRS("Auto-buy"), true));
	menu->content->add(controls::checkbox(STRS("Enable"), &settings->misc.autobuy.enable));

	if (settings->misc.autobuy.enable) {
		menu->content->add(controls::dropbox(STRS("Main weapon"), &settings->misc.autobuy.main, {
			STRS("None"), STRS("SSG 08"), STRS("AWP"), STRS("G3SG1"), STRS("SCAR-20"),
			STRS("Galil AR"), STRS("FAMAS"), STRS("AK-47"), STRS("M4A4"), STRS("M4A1-S"),
			STRS("SG 553"), STRS("AUG"), STRS("MAC-10"), STRS("MP9"), STRS("MP7"),
			STRS("MP5-SD"), STRS("UMP-45"), STRS("P90"), STRS("PP-Bizon")
		}, false));

		menu->content->add(controls::dropbox(STRS("Pistol"), &settings->misc.autobuy.pistol, {
			STRS("None"), STRS("Glock-18"), STRS("P2000"), STRS("USP-S"), STRS("Dual Berettas"),
			STRS("P250"), STRS("Tec-9"), STRS("Five-SeveN"), STRS("CZ75 Auto"),
			STRS("Desert Eagle"), STRS("R8 Revolver")
		}, false));

		menu->content->add(controls::dropbox(STRS("Additional"), (int*)&settings->misc.autobuy.additional, {
			STRS("Armor"), STRS("Helmet"), STRS("Defuse kit"), STRS("Zeus")
		}, true));
	}

	menu->content->add(controls::text(STRS("Peek assist"), true));
	menu->content->add(controls::checkbox(STRS("Retreat on key"), &settings->movement.peek_assist_retreat_on_key));
}

static void ragebot_aimbot_tab() {
	static int weapon_group = 0;
	static int weapon_slot = 0;

	menu->content->add(controls::text(STRS("Aimbot"), true));
	menu->content->add(controls::checkbox(STRS("Enable"), &settings->ragebot.enable));
	menu->content->add(controls::checkbox(STRS("Auto-fire"), &settings->ragebot.autofire));
	menu->content->add(controls::checkbox(STRS("Silent aim"), &settings->ragebot.silent));
	menu->content->add(controls::slider(STRS("Computing limit"), &settings->ragebot.computing_limit, 1, 6));

	menu->content->add(controls::text(STRS("Weapon config"), true));

	const std::vector<std::string> group_names = {
		STRS("Default"), STRS("Snipers"), STRS("Auto-snipers"), STRS("Heavy pistols"),
		STRS("Pistols"), STRS("Rifles"), STRS("Heavies"), STRS("Shotguns"), STRS("SMGs")
	};

	const std::vector<std::string> slot_names = {
		STRS("General"), STRS("Slot 1"), STRS("Slot 2"), STRS("Slot 3"),
		STRS("Slot 4"), STRS("Slot 5"), STRS("Slot 6"), STRS("Slot 7"), STRS("Slot 8"), STRS("Slot 9")
	};

	menu->content->add(controls::dropbox(STRS("Group"), &weapon_group, group_names, false));
	menu->content->add(controls::dropbox(STRS("Weapon"), &weapon_slot, slot_names, false));

	weapon_group = std::clamp(weapon_group, 0, 8);
	weapon_slot = std::clamp(weapon_slot, 0, 9);

	auto& wpn = settings->ragebot.weapons[weapon_group].settings[weapon_slot];

	menu->content->add(controls::checkbox(STRS("Override default"), &wpn.override_default));
	menu->content->add(controls::checkbox(STRS("Quick stop"), &wpn.quick_stop));

	if (wpn.quick_stop)
		menu->content->add(controls::dropbox(STRS("Quick stop options"), (int*)&wpn.quick_stop_options, { STRS("Between shots"), STRS("Early") }, true));

	menu->content->add(controls::checkbox(STRS("Visible only"), &wpn.visible_only));
	menu->content->add(controls::slider(STRS("Minimum damage"), &wpn.mindamage, 0, 120));
	menu->content->add(controls::slider(STRS("Minimum damage override"), &wpn.mindamage_override, 0, 120));
	menu->content->add(controls::slider(STRS("Hitchance"), &wpn.hitchance, 0, 100));
	menu->content->add(controls::checkbox(STRS("Strict hitchance"), &wpn.strict_hitchance));

	menu->content->add(controls::dropbox(STRS("Hitboxes"), (int*)&wpn.hitboxes, {
		STRS("Head"), STRS("Chest"), STRS("Stomach"), STRS("Pelvis"),
		STRS("Arms"), STRS("Legs")
	}, true));

	menu->content->add(controls::dropbox(STRS("Force safepoint"), (int*)&wpn.force_safepoint, {
		STRS("Head"), STRS("Chest"), STRS("Stomach"), STRS("Pelvis")
	}, true));

	menu->content->add(controls::checkbox(STRS("Prefer safepoint"), &wpn.prefer_safepoint));
	menu->content->add(controls::checkbox(STRS("Auto-scope"), &wpn.autoscope));
	menu->content->add(controls::slider(STRS("Head scale"), &wpn.head_scale, 0, 100));
	menu->content->add(controls::slider(STRS("Body scale"), &wpn.body_scale, 0, 100));
	menu->content->add(controls::dropbox(STRS("Priority hitgroup"), &wpn.priority_hitgroup, {
		STRS("None"), STRS("Head"), STRS("Chest"), STRS("Stomach")
	}, false));
}

static void antiaim_tab() {
	static int antiaim_trigger = 0;

	menu->content->add(controls::text(STRS("Anti-aim"), true));
	menu->content->add(controls::checkbox(STRS("Enable"), &settings->antiaim.enable));
	menu->content->add(controls::slider(STRS("Fake-lag"), &settings->antiaim.fakelag, 0, 16));

	menu->content->add(controls::text(STRS("Trigger config"), true));

	const std::vector<std::string> trigger_names = {
		STRS("Default"), STRS("Standing"), STRS("Moving"), STRS("Jumping"),
		STRS("Slow-walk"), STRS("Crouching"), STRS("Other")
	};

	menu->content->add(controls::dropbox(STRS("Trigger"), &antiaim_trigger, trigger_names, false));
	antiaim_trigger = std::clamp(antiaim_trigger, 0, 6);

	auto& aa = settings->antiaim.triggers[antiaim_trigger];

	menu->content->add(controls::checkbox(STRS("Override default"), &aa.override_default));
	menu->content->add(controls::checkbox(STRS("Force off"), &aa.force_off));
	menu->content->add(controls::dropbox(STRS("Pitch"), &aa.pitch, {
		STRS("None"), STRS("Down"), STRS("Up"), STRS("Zero"), STRS("Custom")
	}, false));

	menu->content->add(controls::checkbox(STRS("Ignore freestand"), &aa.ignore_freestand));
	menu->content->add(controls::checkbox(STRS("Ignore manuals"), &aa.ignore_manuals));
	menu->content->add(controls::slider(STRS("Yaw add"), &aa.yaw_add, -180, 180));
	menu->content->add(controls::checkbox(STRS("Spin"), &aa.spin));

	if (aa.spin) {
		menu->content->add(controls::slider(STRS("Spin speed"), &aa.spin_speed, 1, 50));
		menu->content->add(controls::slider(STRS("Spin range"), &aa.spin_range, 1, 360));
	}

	menu->content->add(controls::checkbox(STRS("Edge yaw on peek"), &aa.edge_yaw_on_peek));
	menu->content->add(controls::slider(STRS("Jitter angle"), &aa.jitter_angle, 0, 180));
	menu->content->add(controls::checkbox(STRS("Align desync"), &aa.align_desync));
	menu->content->add(controls::checkbox(STRS("Randomize jitter"), &aa.randomize_jitter));
	menu->content->add(controls::slider(STRS("Desync amount"), &aa.desync_amount, 0, 100));
	menu->content->add(controls::checkbox(STRS("Desync jitter"), &aa.desync_jitter));
	menu->content->add(controls::dropbox(STRS("Desync direction"), &aa.desync_direction, {
		STRS("Opposite"), STRS("Static left"), STRS("Static right")
	}, false));

	menu->content->add(controls::dropbox(STRS("Align by target"), &aa.align_by_target, {
		STRS("Off"), STRS("On"), STRS("Adaptive")
	}, false));
}

static void ragebot_misc_tab() {
	menu->content->add(controls::text(STRS("Exploits"), true));
	menu->content->add(controls::dropbox(STRS("Defensive"), (int*)&settings->exploits.defensive_flags, {
		STRS("Double-tap"), STRS("Hide shots")
	}, true));

	menu->content->add(controls::checkbox(STRS("Immediate teleport"), &settings->exploits.immediate_teleport));
}

static void visuals_players_tab() {
	menu->content->add(controls::text(STRS("Player ESP"), true));
	menu->content->add(controls::checkbox(STRS("Enable"), &settings->player_esp.enable));
	menu->content->add(controls::checkbox(STRS("Box"), &settings->player_esp.box));

	if (settings->player_esp.box)
		menu->content->add(controls::text(STRS("Box color: ") + std::to_string(settings->player_esp.box_color.r()) + STRS(",") + std::to_string(settings->player_esp.box_color.g()) + STRS(",") + std::to_string(settings->player_esp.box_color.b())));

	menu->content->add(controls::checkbox(STRS("Name"), &settings->player_esp.name.value));

	if (settings->player_esp.name.value) {
		menu->content->add(controls::text(STRS("Name color: ") + std::to_string(settings->player_esp.name.colors[0].r()) + STRS(",") + std::to_string(settings->player_esp.name.colors[0].g()) + STRS(",") + std::to_string(settings->player_esp.name.colors[0].b())));
		menu->content->add(controls::dropbox(STRS("Name position"), &settings->player_esp.name.position, { STRS("Top"), STRS("Center"), STRS("Bottom") }, false));
	}

	menu->content->add(controls::checkbox(STRS("Health bar"), &settings->player_esp.health.value));
	menu->content->add(controls::checkbox(STRS("Override health color"), &settings->player_esp.override_health_color));

	if (settings->player_esp.health.value) {
		menu->content->add(controls::dropbox(STRS("Health position"), &settings->player_esp.health.position, { STRS("Left"), STRS("Right"), STRS("Top"), STRS("Bottom") }, false));
	}

	menu->content->add(controls::checkbox(STRS("Ammo bar"), &settings->player_esp.ammo.value));
	menu->content->add(controls::dropbox(STRS("Weapon"), (int*)&settings->player_esp.weapon.value, {
		STRS("Icon"), STRS("Text"), STRS("Ammo")
	}, true));

	menu->content->add(controls::dropbox(STRS("Flags"), (int*)&settings->player_esp.flags.value, {
		STRS("Armor"), STRS("Scoped"), STRS("Flashed"), STRS("Health")
	}, true));

	auto add_chams = [](const std::string& label, int index) {
		auto& chams = settings->player_esp.chams[index];
		menu->content->add(controls::text(STRS(label)));
		menu->content->add(controls::checkbox(STRS("Enable"), &chams.enable));

		if (chams.enable) {
			menu->content->add(controls::dropbox(STRS("Material"), &chams.material, {
				STRS("Normal"), STRS("Flat"), STRS("Glow"), STRS("Gold"), STRS("Glass"), STRS("Chrome")
			}, false));

			menu->content->add(controls::slider(STRS("Phong amount"), &chams.phong_amount, 0, 100));
			menu->content->add(controls::slider(STRS("Rim amount"), &chams.rim_amount, 0, 100));

			menu->content->add(controls::dropbox(STRS("Overlays"), (int*)&chams.overlays, {
				STRS("Glow"), STRS("Reflective"), STRS("Wireframe"), STRS("Metallic"), STRS("Transparent")
			}, true));
		}
	};

	menu->content->add(controls::text(STRS("Player chams"), true));
	add_chams(STRS("Visible"), 0);
	add_chams(STRS("Behind walls"), 1);
	add_chams(STRS("Backtrack"), 4);
	add_chams(STRS("Shot"), 5);
	menu->content->add(controls::checkbox(STRS("Shot chams last only"), &settings->player_esp.shot_chams_last_only));

	menu->content->add(controls::text(STRS("Glow"), true));
	menu->content->add(controls::checkbox(STRS("Glow"), &settings->player_esp.glow.enable));
}

static void visuals_local_tab() {
	menu->content->add(controls::text(STRS("Local chams"), true));

	auto add_chams = [](const std::string& label, int index) {
		auto& chams = settings->player_esp.chams[index];
		menu->content->add(controls::checkbox(STRS(label), &chams.enable));

		if (chams.enable) {
			menu->content->add(controls::dropbox(STRS("Material"), &chams.material, {
				STRS("Normal"), STRS("Flat"), STRS("Glow"), STRS("Gold"), STRS("Glass"), STRS("Chrome")
			}, false));

			menu->content->add(controls::slider(STRS("Phong amount"), &chams.phong_amount, 0, 100));
			menu->content->add(controls::slider(STRS("Rim amount"), &chams.rim_amount, 0, 100));
		}
	};

	add_chams(STRS("Local"), 2);
	add_chams(STRS("Desync"), 3);
	add_chams(STRS("Arms"), 6);
	add_chams(STRS("Weapon"), 7);
	add_chams(STRS("Attachments"), 8);

	menu->content->add(controls::text(STRS("Local player"), true));
	menu->content->add(controls::checkbox(STRS("Blend in scope"), &settings->player_esp.local_blend_in_scope));

	if (settings->player_esp.local_blend_in_scope)
		menu->content->add(controls::slider(STRS("Blend amount"), &settings->player_esp.local_blend_in_scope_amount, 0, 100));

	menu->content->add(controls::checkbox(STRS("Local glow"), &settings->player_esp.local_glow.enable));
}

static void visuals_world_tab() {
	menu->content->add(controls::text(STRS("Weapon ESP"), true));
	menu->content->add(controls::checkbox(STRS("Enable"), &settings->weapon_esp.enable));

	if (settings->weapon_esp.enable) {
		menu->content->add(controls::checkbox(STRS("Box"), &settings->weapon_esp.box));

			menu->content->add(controls::dropbox(STRS("Name"), (int*)&settings->weapon_esp.name.value, {
			STRS("Icon"), STRS("Text")
		}, true));

		menu->content->add(controls::checkbox(STRS("Ammo bar"), &settings->weapon_esp.ammo.value));

		menu->content->add(controls::checkbox(STRS("Glow"), &settings->weapon_esp.glow.enable));
	}

	menu->content->add(controls::text(STRS("Grenade ESP"), true));
	menu->content->add(controls::checkbox(STRS("Enable"), &settings->grenade_esp.enable));

	if (settings->grenade_esp.enable)
		menu->content->add(controls::checkbox(STRS("Prediction"), &settings->grenade_esp.prediction));

	menu->content->add(controls::text(STRS("Bomb ESP"), true));
	menu->content->add(controls::checkbox(STRS("Enable"), &settings->bomb_esp.enable));

	menu->content->add(controls::text(STRS("Bullets"), true));
	menu->content->add(controls::dropbox(STRS("Tracer"), &settings->bullets.tracer, {
		STRS("Off"), STRS("Line"), STRS("Beam")
	}, false));

	menu->content->add(controls::checkbox(STRS("Server impacts"), &settings->bullets.server_impacts));
	menu->content->add(controls::checkbox(STRS("Client impacts"), &settings->bullets.client_impacts));

	if (settings->bullets.server_impacts || settings->bullets.client_impacts)
		menu->content->add(controls::slider(STRS("Impacts size"), &settings->bullets.impacts_size, 1, 50));

	menu->content->add(controls::text(STRS("Night mode"), true));
	menu->content->add(controls::checkbox(STRS("Enable"), &settings->visuals.nightmode.enable));

	if (settings->visuals.nightmode.enable) {
		menu->content->add(controls::text(STRS("World, props, skybox colors editable via config")));
	}
}

static void visuals_other_tab() {
	menu->content->add(controls::text(STRS("Removals"), true));
	menu->content->add(controls::dropbox(STRS("Remove"), (int*)&settings->visuals.removals, {
		STRS("Smoke"), STRS("Flash"), STRS("Scope overlay"), STRS("Post-processing"), STRS("Fog")
	}, true));

	menu->content->add(controls::text(STRS("Hitmarker"), true));
	menu->content->add(controls::dropbox(STRS("Hitmarker"), (int*)&settings->visuals.hitmarker, {
		STRS("World"), STRS("Screen")
	}, true));

	menu->content->add(controls::text(STRS("Scope"), true));
	menu->content->add(controls::dropbox(STRS("Override scope"), &settings->visuals.override_scope, {
		STRS("Off"), STRS("Cross"), STRS("Circle"), STRS("Custom")
	}, false));

	if (settings->visuals.override_scope) {
		menu->content->add(controls::slider(STRS("Scope gap"), &settings->visuals.scope_gap, 0, 50));
		menu->content->add(controls::slider(STRS("Scope thickness"), &settings->visuals.scope_thickness, 1, 10));
		menu->content->add(controls::slider(STRS("Scope size"), &settings->visuals.scope_size, 10, 300));
	}

	menu->content->add(controls::text(STRS("Field of view"), true));
	menu->content->add(controls::slider(STRS("World FOV"), &settings->visuals.world_fov, 0, 150));
	menu->content->add(controls::slider(STRS("Zoom FOV"), &settings->visuals.zoom_fov, 0, 90));
	menu->content->add(controls::slider(STRS("Viewmodel FOV"), &settings->visuals.viewmodel_fov, 0, 180));

	menu->content->add(controls::checkbox(STRS("Hotkeys list"), &settings->misc.hotkeys_list));
}

static void skins_tab() {
	menu->content->add(controls::text(STRS("Knife"), true));

	const std::vector<std::string> knife_names = {
		STRS("Default"), STRS("Bayonet"), STRS("Bowie"), STRS("Butterfly"),
		STRS("Falchion"), STRS("Flip"), STRS("Gut"), STRS("Huntsman"),
		STRS("Karambit"), STRS("M9 Bayonet"), STRS("Navaja"), STRS("Nomad"),
		STRS("Stiletto"), STRS("Ursus"), STRS("Widowmaker"), STRS("Classic"),
		STRS("Skeleton"), STRS("Outdoor"), STRS("Canis"), STRS("Cord")
	};

	menu->content->add(controls::dropbox(STRS("Knife model"), &settings->skins.knife_model, knife_names, false));

	menu->content->add(controls::text(STRS("Agents"), true));
	menu->content->add(controls::dropbox(STRS("T agent"), &settings->skins.agent_t, {
		STRS("Default"), STRS("Agent 1"), STRS("Agent 2"), STRS("Agent 3"), STRS("Agent 4"),
		STRS("Agent 5"), STRS("Agent 6"), STRS("Agent 7"), STRS("Agent 8"), STRS("Agent 9"),
		STRS("Agent 10"), STRS("Agent 11"), STRS("Agent 12"), STRS("Agent 13"), STRS("Agent 14"),
		STRS("Agent 15"), STRS("Agent 16"), STRS("Agent 17"), STRS("Agent 18"), STRS("Agent 19"),
		STRS("Agent 20"), STRS("Agent 21"), STRS("Agent 22"), STRS("Agent 23"), STRS("Agent 24"),
		STRS("Agent 25"), STRS("Agent 26"), STRS("Agent 27"), STRS("Agent 28"), STRS("Agent 29"),
		STRS("Agent 30")
	}, false));

	menu->content->add(controls::dropbox(STRS("CT agent"), &settings->skins.agent_ct, {
		STRS("Default"), STRS("Agent 1"), STRS("Agent 2"), STRS("Agent 3"), STRS("Agent 4"),
		STRS("Agent 5"), STRS("Agent 6"), STRS("Agent 7"), STRS("Agent 8"), STRS("Agent 9"),
		STRS("Agent 10"), STRS("Agent 11"), STRS("Agent 12"), STRS("Agent 13"), STRS("Agent 14"),
		STRS("Agent 15"), STRS("Agent 16"), STRS("Agent 17"), STRS("Agent 18"), STRS("Agent 19"),
		STRS("Agent 20"), STRS("Agent 21")
	}, false));
}

static void tab_callback() {
	if (menu->m_in_search) {
		menu->set_tab_attributes(STRS("Search"), nullptr);
	} else {
		switch (menu->m_active_tab) {
			case -1: menu->set_tab_attributes(LOCALIZE(("tab.profile")), nullptr); break;
			case 0: menu->set_tab_attributes(STRS("Ragebot"), nullptr, STRS("Aimbot")); break;
			case 1: menu->set_tab_attributes(STRS("Ragebot"), nullptr, STRS("Anti-aim")); break;
			case 2: menu->set_tab_attributes(STRS("Ragebot"), nullptr, STRS("Misc")); break;
			case 3: menu->set_tab_attributes(STRS("Visuals"), nullptr, STRS("Players")); break;
			case 4: menu->set_tab_attributes(STRS("Visuals"), nullptr, STRS("Local")); break;
			case 5: menu->set_tab_attributes(STRS("Visuals"), nullptr, STRS("World")); break;
			case 6: menu->set_tab_attributes(STRS("Visuals"), nullptr, STRS("Other")); break;
			case 7: menu->set_tab_attributes(STRS("Misc"), nullptr); break;
			case 8: menu->set_tab_attributes(STRS("Skins"), nullptr); break;
			case 9: menu->set_tab_attributes(STRS("Scripts"), nullptr); break;
			case 10: menu->set_tab_attributes(STRS("Settings"), nullptr); break;
			case 11: menu->set_tab_attributes(STRS("Hotkeys"), nullptr); break;
		}
	}

	switch (menu->m_active_tab) {
		case 0: ragebot_aimbot_tab(); break;
		case 1: antiaim_tab(); break;
		case 2: ragebot_misc_tab(); break;
		case 3: visuals_players_tab(); break;
		case 4: visuals_local_tab(); break;
		case 5: visuals_world_tab(); break;
		case 6: visuals_other_tab(); break;
		case 7: misc_tab(); break;
		case 8: skins_tab(); break;
		case 9: scripts_tab(); break;
		case 10: settings_tab(); break;
		case 11: hotkeys_tab(); break;
	}
}

void __menu::on_tab_change() {
	static std::atomic_bool balls;

	if (balls == true)
		return;

	balls = true;

	menu->content->override_style().disable_scroll = false;
	menu->content->override_style().scroll_fade = true;

	menu->window->lock();

	menu->content->clear();
	menu->content->set_loading();

	std::thread([]() {
		tab_callback();

		menu->window->unlock();
		menu->content->stop_loading();
		balls = false;
	}).detach();
}

void __menu::create_header() {
	auto column = new containers::column_t{};
	auto row = new containers::row_t{};
	column->override_style().window_padding = {};

	if (menu->m_active_tab_icon != nullptr)
		row->add(controls::image(menu->m_active_tab_icon, vec2d{ 12.f, 12.f } * dpi::_get_actual_scale()));

	auto tab = controls::text(menu->m_active_tab_name);
	c_style style{};
	style.small_text = true;
	style.text_color = { 127, 127, 127 };
	style.container_padding.x = -4.f;
	style.window_padding.y = 0.f;

	tab->set_style(style);

	row->add(tab);

	if (!menu->m_active_subtab_name.empty()) {
		auto subtab = controls::text(STRS("> ") + menu->m_active_subtab_name);
		subtab->set_style(style);
		row->add(subtab);
	}

	row->add(controls::loading([]() { return menu->window->is_locked(); }));

	column->add(row);

	auto search = controls::text_input(&search_phrase, STRS("\xef\x80\x82   ") + LOCALIZE("search"));

	search->on_change_value([]() {
		menu->m_in_search = !search_phrase.empty();
	});

	search->on_change_state([](bool active) {
		if (!active) {
			menu->m_in_search = !search_phrase.empty();
			if (!menu->m_in_search)
				menu->change_tab(menu->m_active_tab, true);
		}
	});

	c_style search_style{};
	search_style.item_width = 160.f;
	search_style.small_text = true;
	search_style.text_color = { 127, 127, 127 };
	search_style.container_padding.y = 4.f;
	search_style.window_padding.y = 12.f;

	search->set_style(search_style);

	column->add(search);

	menu->header->add(column);
}
