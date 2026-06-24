#include "localization.hpp"

#include "../utils/hotkeys.hpp"
#include "../utils/encoding.hpp"

#include <format>
#include <stdexcept>
#include <unordered_map>

static network::localization_t strings{};

static const std::unordered_map<std::string, std::string>& fallback_strings() {
	static const std::unordered_map<std::string, std::string> map = {
		{ "tab.ragebot", "Ragebot" },
		{ "tab.ragebot.aimbot", "Aimbot" },
		{ "tab.ragebot.antiaim", "Anti-aim" },
		{ "tab.ragebot.misc", "Misc" },
		{ "tab.visuals", "Visuals" },
		{ "tab.visuals.players", "Players" },
		{ "tab.visuals.local", "Local" },
		{ "tab.visuals.world", "World" },
		{ "tab.visuals.other", "Other" },
		{ "tab.misc", "Misc" },
		{ "tab.skins", "Skins" },
		{ "tab.scripts", "Scripts" },
		{ "tab.settings", "Settings" },
		{ "tab.hotkeys", "Hotkeys" },
		{ "tab.profile", "Profile" },

		{ "config.save_confirmation", "Are you sure you want to save?" },
		{ "config.delete_confirmation", "Are you sure you want to delete?" },
		{ "config.load", "Load" },
		{ "config.save", "Save" },
		{ "config.delete", "Delete" },
		{ "config.name_input", "Config name" },
		{ "config.create", "Create" },
		{ "config.set_name", "Set name" },

		{ "script.load", "Load" },
		{ "script.unload", "Unload" },
		{ "script.delete", "Delete" },

		{ "hotkey.doubletap", "Doubletap" },
		{ "hotkey.hide_shot", "Hide Shot" },
		{ "hotkey.override_damage", "Override Damage" },
		{ "hotkey.fake_duck", "Fake Duck" },
		{ "hotkey.desync_inverter", "Desync Inverter" },
		{ "hotkey.slow_walk", "Slow Walk" },
		{ "hotkey.freestand", "Freestand" },
		{ "hotkey.manual_right", "Manual Right" },
		{ "hotkey.manual_left", "Manual Left" },
		{ "hotkey.manual_back", "Manual Back" },
		{ "hotkey.manual_forward", "Manual Forward" },
		{ "hotkey.peek_assist", "Peek Assist" },
		{ "hotkey.thirdperson", "Thirdperson" },

		{ "yes", "Yes" },
		{ "no", "No" },
		{ "search", "Search..." },
	};
	return map;
}

void localization::apply(const network::localization_t& l) {
	strings = l;

	for (auto hotkey: hotkeys->m_hotkeys)
		hotkey->translate();
}

std::string localization::get(const std::string& key) {
	if (strings.contains(key))
		return strings.at(key);

	auto it = fallback_strings().find(key);
	if (it != fallback_strings().end())
		return it->second;

	return key;
}