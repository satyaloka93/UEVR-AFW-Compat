#define NOMINMAX

#include <filesystem>
#include <fstream>
#include <optional>
#include <algorithm>
#include <cwctype>
#include <cmath>
#include <unordered_set>
#include <sstream>
#include <format>
#include <nlohmann/json.hpp>

#include <utility/Config.hpp>
#include <utility/String.hpp>
#include <utility/Module.hpp>

#include <sdk/CVar.hpp>
#include <sdk/threading/GameThreadWorker.hpp>
#include <sdk/ConsoleManager.hpp>
#include <sdk/ConsoleRegistryValidation.hpp>
#include <sdk/UGameplayStatics.hpp>

#include "Framework.hpp"

#include "CVarManager.hpp"
#include "CVarScriptPolicy.hpp"

#include <tracy/Tracy.hpp>

constexpr std::string_view cvars_standard_txt_name = "cvars_standard.txt";
constexpr std::string_view cvars_data_txt_name = "cvars_data.txt";
constexpr std::string_view user_script_txt_name = "user_script.txt";

namespace {
bool is_validated_cvar_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        if (!exe_path.has_value()) {
            return false;
        }

        auto filename = std::filesystem::path(*exe_path).filename().wstring();
        std::transform(filename.begin(), filename.end(), filename.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
        });
        return filename == L"theouterworlds2-win64-shipping.exe" || filename == L"shf-win64-shipping.exe";
    }();

    return result;
}

const sdk::console_validation::Snapshot& validated_registry() {
    static const auto objects = sdk::console_validation::snapshot(sdk::FConsoleManager::get_validated());
    return objects;
}

sdk::IConsoleVariable* validated_variable(const std::wstring& name) {
    const auto& objects = validated_registry();
    const auto it = objects.find(name);
    if (it == objects.end()) return nullptr;
    auto* variable = reinterpret_cast<sdk::IConsoleVariable*>(it->second);
    if (!sdk::console_validation::object(variable)) return nullptr;
    static std::unordered_set<uintptr_t> rejected_vtables;
    uintptr_t table{};
    if (!sdk::console_validation::read(variable, &table, sizeof(table)) || rejected_vtables.contains(table)) return nullptr;
    if (!variable->validate_access()) { rejected_vtables.insert(table); return nullptr; }
    return variable;
}
}

bool CVarManager::uses_validated_access() { return is_validated_cvar_game(); }

CVarManager::CVarManager() {
    ZoneScopedN(__FUNCTION__);

    m_displayed_cvars.insert(m_displayed_cvars.end(), s_default_standard_cvars.begin(), s_default_standard_cvars.end());
    m_displayed_cvars.insert(m_displayed_cvars.end(), s_default_data_cvars.begin(), s_default_data_cvars.end());

    if (is_validated_cvar_game()) {
        for (auto& cvar : m_displayed_cvars) {
            const bool data = dynamic_cast<CVarData*>(cvar.get()) != nullptr;
            cvar = std::make_shared<CVarValidated>(*cvar, data);
        }
    }

    // Sort first by name, then by bool/int/float type. Bools get displayed first.
    std::sort(m_displayed_cvars.begin(), m_displayed_cvars.end(), [](const auto& a, const auto& b) {
        return a->get_name() < b->get_name();
    });

    std::sort(m_displayed_cvars.begin(), m_displayed_cvars.end(), [](const auto& a, const auto& b) {
        return (int)a->get_type() < (int)b->get_type();
    });

    m_all_cvars.insert(m_all_cvars.end(), m_displayed_cvars.begin(), m_displayed_cvars.end());

    for (const auto& entry : performance_cvars::entries) {
        std::shared_ptr<CVarStandard> source;
        if (entry.kind == performance_cvars::Kind::floating) {
            source = std::make_shared<CVarStandard>(L"Renderer", entry.name, CVar::Type::FLOAT, entry.minimum, entry.maximum);
        } else {
            source = std::make_shared<CVarStandard>(L"Renderer", entry.name,
                entry.kind == performance_cvars::Kind::toggle ? CVar::Type::BOOL : CVar::Type::INT,
                (int)entry.minimum, (int)entry.maximum);
        }
        auto cvar = std::make_shared<CVarValidated>(*source, &entry);
        m_performance_cvars.push_back(cvar);
        m_all_cvars.push_back(cvar);
    }

    // set m_hzbo (shared ptr) to the r.HZBOcclusion cvar in m_all_cvars
    for (auto& cvar : m_all_cvars) {
        if (cvar->get_name() == L"r.HZBOcclusion") {
            m_hzbo = cvar;
            break;
        }
    }
}

CVarManager::~CVarManager() {
    ZoneScopedN(__FUNCTION__);

    /*for (auto& cvar : m_cvars) {
        cvar->save();
    }*/
}

void CVarManager::spawn_console() {
    if (m_native_console_spawned) {
        return;
    }

    // Find Engine object and add the Console
    const auto engine = sdk::UGameEngine::get();

    if (engine != nullptr) {
        const auto console_class = engine->get_property<sdk::UClass*>(L"ConsoleClass");
        auto game_viewport = engine->get_property<sdk::UObject*>(L"GameViewport");

        if (console_class != nullptr && game_viewport != nullptr) {
            const auto console = sdk::UGameplayStatics::get()->spawn_object(console_class, game_viewport);

            if (console != nullptr) {
                game_viewport->get_property<sdk::UObject*>(L"ViewportConsole") = console;
                m_native_console_spawned = true;
            }
        }
    }
}

void CVarManager::on_pre_engine_tick(sdk::UGameEngine* engine, float delta) {
    ZoneScopedN(__FUNCTION__);

    const auto now = std::chrono::steady_clock::now();
    if (m_validated_start == std::chrono::steady_clock::time_point{}) {
        m_validated_start = now;
        spdlog::info("[Validated CVar] Optional performance discovery deferred for five seconds");
    }

    if (is_validated_cvar_game()) {
        if (now - m_validated_start < std::chrono::seconds(5)) return;
        if (m_validated_next < m_displayed_cvars.size()) {
            auto& cvar = m_displayed_cvars[m_validated_next++];
            cvar->update();
            cvar->freeze();
        } else if (now - m_validated_poll >= std::chrono::milliseconds(250)) {
            m_validated_poll = now;
            for (auto& cvar : m_displayed_cvars) { cvar->update(); cvar->freeze(); }
        }
    } else {
        for (auto& cvar : m_all_cvars) {
            if (cvar->is_performance_entry()) continue;
            cvar->update();
            cvar->freeze();
        }
    }

    // Optional controls use bounded registry lookup in every game. Never invoke
    // legacy fallback scans for a missing performance variable.
    if (now - m_validated_start >= std::chrono::seconds(5)) {
        if (m_performance_next < m_performance_cvars.size()) {
            auto& cvar = m_performance_cvars[m_performance_next++];
            cvar->update(); cvar->freeze();
        } else if (now - m_performance_poll >= std::chrono::milliseconds(250)) {
            m_performance_poll = now;
            for (auto& cvar : m_performance_cvars) { cvar->update(); cvar->freeze(); }
        }
    }

    if (m_should_execute_console_script) {
        execute_console_script(engine, user_script_txt_name.data());
        m_should_execute_console_script = false;
    }
    // One entry per tick limits batch stalls; a single engine setter can still
    // recreate render resources. Its elapsed time is logged separately.
    process_script_line(engine);
}

void CVarManager::on_draw_ui() {
    ZoneScopedN(__FUNCTION__);

    ImGui::SetNextItemOpen(true, ImGuiCond_::ImGuiCond_Once);
    if (ImGui::TreeNode("CVars")) {
        ImGui::TextWrapped("Note: Any changes here will be frozen.");
        m_auto_user_script->draw("Automatically apply user_script.txt on profile load");
        ImGui::TextWrapped("Off bypasses automatic application. Save Config to keep this choice. Bypass does not undo values already applied; restart for a clean baseline. Independent Lua/INI overrides are not controlled here.");
        if (ImGui::Button("Apply user_script.txt once")) {
            GameThreadWorker::get().enqueue([this]() { m_should_execute_console_script = true; });
        }
        ImGui::SameLine();
        if (ImGui::Button("Bypass / cancel pending script")) {
            m_auto_user_script->value() = false;
            GameThreadWorker::get().enqueue([this]() {
                m_should_execute_console_script = false;
                m_script_lines.clear();
                m_script_remaining = 0;
                spdlog::info("[CVar script] Pending script cancelled; already-applied values unchanged");
            });
        }
        ImGui::Text("Pending script lines: %llu", (unsigned long long)m_script_remaining.load());
        if (is_validated_cvar_game()) {
            ImGui::TextWrapped("Validated numeric access (TOW2/SHf): discovery starts after 5 seconds. Unverified variables are unavailable. Frozen menu values take priority over conflicting script lines; unchanged values are not written again.");
        }

        uint32_t frozen_cvars = 0;

        for (auto& cvar : m_all_cvars) {
            if (cvar->is_frozen()) {
                ++frozen_cvars;
            }
        }

        ImGui::TextWrapped("Frozen CVars: %i", frozen_cvars);

        ImGui::Checkbox("Display Console", &m_wants_display_console);
        
        if (!m_native_console_spawned) {
            if (ImGui::Button("Spawn Native Console")) {
                spawn_console();
            }
        }

        if (ImGui::Button("Dump All CVars")) {
            GameThreadWorker::get().enqueue([this]() {
                dump_commands();
            });
        }

        ImGui::SameLine();

        if (ImGui::Button("Clear Frozen CVars")) {
            for (auto& cvar : m_all_cvars) {
                cvar->unfreeze();
            }

            const auto cvars_txt = Framework::get_persistent_dir(cvars_standard_txt_name.data());

            try {
                if (std::filesystem::exists(cvars_txt)) {
                    std::filesystem::remove(cvars_txt);
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to remove {}: {}", cvars_standard_txt_name.data(), e.what());
            }

            const auto cvars_data_txt = Framework::get_persistent_dir(cvars_data_txt_name.data());

            try {
                if (std::filesystem::exists(cvars_data_txt)) {
                    std::filesystem::remove(cvars_data_txt);
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to remove {}: {}", cvars_data_txt_name.data(), e.what());
            }
        }
        
        if (ImGui::TreeNode("Performance controls (detected in this game)")) {
            ImGui::TextWrapped("Only registered variables are listed; verified interfaces are editable. Presence does not prove a feature is active. No preset is applied. Changes are saved/frozen after readback. Hover for details; sliders apply on release.");
            size_t pending{}, missing{}, supported{};
            const char* group = nullptr;
            for (auto& cvar : m_performance_cvars) {
                const auto status = cvar->status();
                if (status == 0) { ++pending; continue; }
                if (status == 4) { ++missing; continue; }
                if (status == 1) ++supported;
                const auto* entry = cvar->performance_entry();
                if (!group || std::string_view(group) != entry->group) {
                    group = entry->group;
                    ImGui::Separator(); ImGui::TextUnformatted(group);
                }
                cvar->draw_ui();
            }
            ImGui::Text("Verified: %llu | Not present: %llu | Pending: %llu",
                (unsigned long long)supported, (unsigned long long)missing, (unsigned long long)pending);
            ImGui::TextWrapped("Resolution, VSM enable, AO, motion blur and depth of field remain in the existing list below. Leave the working VSM setting unchanged while testing other controls.");
            ImGui::TreePop();
        }

        for (auto& cvar : m_displayed_cvars) {
            cvar->draw_ui();
        }

        ImGui::TreePop();
    }
}

void CVarManager::on_frame() {
    if (m_wants_display_console) {
        display_console();
    }
}

void CVarManager::on_config_load(const utility::Config& cfg, bool set_defaults) {
    ZoneScopedN(__FUNCTION__);
    m_auto_user_script->config_load(cfg, set_defaults);
    m_script_lines.clear();
    m_script_remaining = 0;

    for (auto& cvar : m_all_cvars) {
        cvar->load(set_defaults);
    }

    // TODO: Add arbitrary cvars from the other configs the user can add.

    // calling UEngine::exec here causes a crash, defer to on_pre_engine_tick()
    m_should_execute_console_script = !set_defaults && m_auto_user_script->value();
    spdlog::info("[CVar script] Profile loaded: automatic={} validated={}", m_should_execute_console_script, is_validated_cvar_game());
}

void CVarManager::on_config_save(utility::Config& cfg) {
    m_auto_user_script->config_save(cfg);
}

void CVarManager::dump_commands() {
    if (is_validated_cvar_game()) {
        nlohmann::json json;
        for (const auto& [name, address] : validated_registry()) {
            json[utility::narrow(name)] = {{"registry_present", true}, {"access", "not invoked during dump"}};
        }
        std::ofstream file(g_framework->get_persistent_dir() / "cvardump.json");
        if (file) file << json.dump(4);
        spdlog::info("[Validated CVar] Dumped {} registry names (not a value/type dump)", validated_registry().size());
        return;
    }
    const auto console_manager = sdk::FConsoleManager::get();

    if (console_manager == nullptr) {
        return;
    }

    nlohmann::json json;

    for (auto obj : console_manager->get_console_objects()) {
        if (obj.value == nullptr || obj.key == nullptr || IsBadReadPtr(obj.key, sizeof(wchar_t))) {
            continue;
        }

        auto& entry = json[utility::narrow(obj.key)];
        
        entry["description"] = "";
        //entry["address"] = (std::stringstream{} << std::hex << (uintptr_t)obj.value).str();
        //entry["vtable"] = (std::stringstream{} << std::hex << *(uintptr_t*)obj.value).str();

        bool is_command = false;

        try {
            is_command = obj.value->AsCommand() != nullptr;
            if (is_command) {
                entry["command"] = true;
            } else {
                entry["value"] = ((sdk::IConsoleVariable*)obj.value)->GetFloat();
            }
        } catch(...) {
            SPDLOG_WARN("Failed to check if CVar is a command: {}", utility::narrow(obj.key));
        }

        const auto help_string = obj.value->GetHelp();

        if (help_string != nullptr && !IsBadReadPtr(help_string, sizeof(wchar_t))) {
            try {
                SPDLOG_INFO("Found CVar: {} {}", utility::narrow(obj.key), utility::narrow(help_string));
                entry["description"] = utility::narrow(help_string);
            } catch(...) {

            }
        }
        
        SPDLOG_INFO("Found CVar: {}", utility::narrow(obj.key));
    }

    const auto persistent_dir = g_framework->get_persistent_dir();

    // Dump all CVars to a JSON file.
    std::ofstream file(persistent_dir / "cvardump.json");

    if (file.is_open()) {
        file << json.dump(4);
        file.close();

        SPDLOG_INFO("Dumped CVars to {}", (persistent_dir / "cvardump.json").string());
    }
}

// Use ImGui to display a homebrew console.
void CVarManager::display_console() {
    if (!g_framework->is_drawing_ui()) {
        return;
    }
    if (is_validated_cvar_game()) {
        if (ImGui::Begin("UEVRConsole", &m_wants_display_console)) {
            ImGui::TextWrapped("Legacy arbitrary-command dispatch is disabled for this game. Use the validated CVar menu, registry dump, or numeric script button.");
        }
        ImGui::End();
        return;
    }

    bool open = true;

    ImGui::SetNextWindowSize(ImVec2(800, 512), ImGuiCond_::ImGuiCond_Once);
    if (ImGui::Begin("UEVRConsole", &open)) {
        const auto console_manager = sdk::FConsoleManager::get();

        if (console_manager == nullptr) {
            ImGui::TextWrapped("Failed to get FConsoleManager.");
            ImGui::End();
            return;
        }


        ImGui::TextWrapped("Note: This is a homebrew console. It is not the same as the in-game console.");

        ImGui::Separator();

        ImGui::Text("> ");
        ImGui::SameLine();

        ImGui::PushItemWidth(-1);

        std::scoped_lock _{m_console.autocomplete_mutex};

        // Do a preliminary parse of the input buffer to see if we can autocomplete.
        {
            const auto entire_command = std::string_view{ m_console.input_buffer.data() };

            if (entire_command != m_console.last_parsed_buffer) {
                std::vector<std::string> args{};

                // Use getline
                std::stringstream ss{ entire_command.data() };
                while (ss.good()) {
                    std::string arg{};
                    std::getline(ss, arg, ' ');
                    args.push_back(arg);
                }

                if (!args.empty()) {
                    GameThreadWorker::get().enqueue([console_manager, args, this]() {
                        std::scoped_lock _{m_console.autocomplete_mutex};
                        m_console.autocomplete.clear();

                        const auto possible_commands = console_manager->fuzzy_find(utility::widen(args[0]));

                        for (const auto& command : possible_commands) {
                            std::string value = "Command";
                            std::string description = "";

                            try {
                                if (command.value->AsCommand() == nullptr) {
                                    value = std::format("{}", ((sdk::IConsoleVariable*)command.value)->GetFloat());
                                }
                            } catch(...) {
                                value = "Failed to get value.";
                            }

                            try {
                                const auto help_string = command.value->GetHelp();

                                if (help_string != nullptr && !IsBadReadPtr(help_string, sizeof(wchar_t))) {
                                    description = utility::narrow(help_string);
                                }
                            } catch(...) {
                                description = "Failed to get description.";
                            }

                            m_console.autocomplete.emplace_back(AutoComplete{
                                command.value, 
                                utility::narrow(command.key),
                                value,
                                description
                            });
                        }
                    });
                }

                m_console.last_parsed_buffer = entire_command;
            }
        }

        if (ImGui::InputText("##UEVRConsoleInput", m_console.input_buffer.data(), m_console.input_buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_console.input_buffer[m_console.input_buffer.size() - 1] = '\0';

            if (m_console.input_buffer[0] != '\0') {
                const auto entire_command = std::string_view{ m_console.input_buffer.data() };

                // Split the command into the arguments via ' ' (space).
                std::vector<std::string> args{};

                // Use getline
                std::stringstream ss{ entire_command.data() };
                while (ss.good()) {
                    std::string arg{};
                    std::getline(ss, arg, ' ');
                    args.push_back(arg);
                }

                // Execute the command.
                if (args.size() >= 2) {
                    auto object = console_manager->find(utility::widen(args[0]));
                    const auto is_command = object != nullptr && object->AsCommand() != nullptr;

                    if (object != nullptr && !is_command) {
                        auto var = (sdk::IConsoleVariable*)object;
                        
                        GameThreadWorker::get().enqueue([var, value = utility::widen(args[1])]() {
                            var->Set(value.c_str());
                        });
                    } else if (object != nullptr && is_command) {
                        auto command = (sdk::IConsoleCommand*)object;

                        std::vector<std::wstring> widened_args{};
                        for (auto i = 1; i < args.size(); ++i) {
                            widened_args.push_back(utility::widen(args[i]));
                        }

                        GameThreadWorker::get().enqueue([command, widened_args]() {
                            command->Execute(widened_args);
                        });
                    } else if (object == nullptr) {
                        // Try UEngine::Exec
                        std::string entire_command_str{entire_command.data()};
                        GameThreadWorker::get().enqueue([entire_command_str]() {
                            auto engine = sdk::UGameEngine::get();
                            if (engine != nullptr) {
                                engine->exec(utility::widen(entire_command_str).data());
                            }
                        });
                    }
                }

                m_console.history.push_back(m_console.input_buffer.data());
                m_console.history_index = m_console.history.size();

                m_console.input_buffer.fill('\0');
            }
        }

        // Display autocomplete
        if (!m_console.autocomplete.empty()) {
            // Create a table of all the possible commands.
            if (ImGui::BeginTable("##UEVRAutocomplete", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthFixed, 300.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto& command : m_console.autocomplete) {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(command.name.c_str());

                    if (ImGui::IsItemClicked()) {
                        // Copy the command to the input buffer.
                        std::copy(command.name.begin(), command.name.end(), m_console.input_buffer.begin());
                        m_console.input_buffer[command.name.size()] = '\0';
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(command.current_value.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped(command.description.c_str());
                }

                ImGui::EndTable();
            }
        }

        ImGui::End();
    }
}

std::string CVarManager::CVar::get_key_name() {
    ZoneScopedN(__FUNCTION__);

    return std::format("{}_{}", utility::narrow(m_module), utility::narrow(m_name));
}

void CVarManager::CVar::load_internal(const std::string& filename, bool set_defaults) try {
    ZoneScopedN(__FUNCTION__);

    spdlog::info("[CVarManager] Loading {}...", filename);

    const auto cvars_txt = Framework::get_persistent_dir(filename);

    if (!std::filesystem::exists(cvars_txt)) {
        return;
    }

    auto cfg = utility::Config{cvars_txt.string()};
    auto value = cfg.get(get_key_name());

    if (!value) {
        // No need to freeze.
        return;
    }
    
    switch (m_type) {
    case Type::BOOL:
    case Type::INT:
        try {
            m_frozen_int_value = *cfg.get<int>(get_key_name());
        } catch(...) {
            m_frozen_int_value = (int)*cfg.get<float>(get_key_name());
        }
        break;
    case Type::FLOAT:
        try {
            m_frozen_float_value = *cfg.get<float>(get_key_name());
        } catch(...) {
            m_frozen_float_value = (float)*cfg.get<int>(get_key_name());
        }
        break;
    }

    m_frozen = true;
} catch(const std::exception& e) {
    spdlog::error("Failed to load {}: {}", filename, e.what());
}

void CVarManager::CVar::save_internal(const std::string& filename) try {
    ZoneScopedN(__FUNCTION__);
    
    spdlog::info("[CVarManager] Saving {}...", filename);

    const auto cvars_txt = Framework::get_persistent_dir(filename);

    auto cfg = utility::Config{cvars_txt.string()};

    switch (m_type) {
    case Type::BOOL:
    case Type::INT:
        cfg.set<int>(get_key_name(), m_frozen_int_value);
        break;
    case Type::FLOAT:
        cfg.set<float>(get_key_name(), m_frozen_float_value);
        break;
    };

    cfg.save(cvars_txt.string());
    m_frozen = true;
} catch (const std::exception& e) {
    spdlog::error("Failed to save {}: {}", filename, e.what());
}

void CVarManager::CVarStandard::load(bool set_defaults) {
    ZoneScopedN(__FUNCTION__);

    load_internal(cvars_standard_txt_name.data(), set_defaults);
}

void CVarManager::CVarStandard::save() {
    ZoneScopedN(__FUNCTION__);

    if (m_cvar == nullptr || *m_cvar == nullptr) {
        // CVar not found, don't save.
        return;
    }

    auto cvar = *m_cvar;

    switch (m_type) {
    case Type::BOOL:
        m_frozen_int_value = cvar->GetInt();
        break;
    case Type::INT:
        m_frozen_int_value = cvar->GetInt();
        break;
    case Type::FLOAT:
        m_frozen_float_value = cvar->GetFloat();
        break;
    default:
        break;
    }

    save_internal(cvars_standard_txt_name.data());
}

void CVarManager::CVarStandard::freeze() {
    ZoneScopedN(__FUNCTION__);

    if (!m_frozen) {
        return;
    }

    if (m_cvar == nullptr || *m_cvar == nullptr) {
        return;
    }

    if (!m_ever_frozen) {
        m_ever_frozen = true;
        SPDLOG_INFO("[CVarManager] (Standard) First time freezing \"{}\"...", utility::narrow(m_name));
    }

    switch(m_type) {
    case Type::BOOL:
        // Limiting the amount of times Set gets called with string conversions.
        if ((*m_cvar)->GetInt() != m_frozen_int_value) {
            (*m_cvar)->Set(std::to_wstring(m_frozen_int_value).c_str());
        }
        break;
    case Type::INT:
        if ((*m_cvar)->GetInt() != m_frozen_int_value) {
            (*m_cvar)->Set(std::to_wstring(m_frozen_int_value).c_str());
        }
        break;
    case Type::FLOAT:
        if ((*m_cvar)->GetFloat() != m_frozen_float_value) {
            (*m_cvar)->Set(std::to_wstring(m_frozen_float_value).c_str());
        }
        break;
    default:
        break;
    };
}

void CVarManager::CVarStandard::update() {
    ZoneScopedN(__FUNCTION__);

    if (m_cvar == nullptr) {
        m_cvar = sdk::find_cvar_cached(m_module, m_name);
    }
}

void CVarManager::CVarStandard::draw_ui() try {
    ZoneScopedN(__FUNCTION__);

    if (m_cvar == nullptr || *m_cvar == nullptr) {
        ImGui::TextWrapped("Failed to find cvar: %s", utility::narrow(m_name).c_str());
        return;
    }

    auto cvar = *m_cvar;
    const auto narrow_name = utility::narrow(m_name);
    
    switch (m_type) {
    case Type::BOOL: {
        auto value = (bool)cvar->GetInt();

        if (ImGui::Checkbox(narrow_name.c_str(), &value)) {
            GameThreadWorker::get().enqueue([sft = shared_from_this(), cvar, value]() {
                try {
                    cvar->Set(std::to_wstring(value).c_str());
                    sft->save();
                } catch (...) {
                    spdlog::error("Failed to set cvar: {}", utility::narrow(sft->get_name()));
                }
            });
        }
        break;
    }
    case Type::INT: {
        auto value = cvar->GetInt();

        if (ImGui::SliderInt(narrow_name.c_str(), &value, m_min_int_value, m_max_int_value)) {
            GameThreadWorker::get().enqueue([sft = shared_from_this(), cvar, value]() {
                try {
                    cvar->Set(std::to_wstring(value).c_str());
                    sft->save();
                } catch(...) {
                    spdlog::error("Failed to set cvar: {}", utility::narrow(sft->get_name()));
                }
            });
        }
        break;
    }
    case Type::FLOAT: {
        auto value = cvar->GetFloat();

        if (ImGui::SliderFloat(narrow_name.c_str(), &value, m_min_float_value, m_max_float_value)) {
            GameThreadWorker::get().enqueue([sft = shared_from_this(), cvar, value]() {
                try {
                    cvar->Set(std::to_wstring(value).c_str());
                    sft->save();
                } catch(...) {
                    spdlog::error("Failed to set cvar: {}", utility::narrow(sft->get_name()));
                }
            });
        }
        break;
    }
    default:
        ImGui::TextWrapped("Unimplemented cvar type: %s", utility::narrow(m_name).c_str());
        break;
    };
} catch(...) {
    ImGui::TextWrapped("Failed to read cvar: %s", utility::narrow(m_name).c_str());
}

void CVarManager::CVarData::load(bool set_defaults) {
    ZoneScopedN(__FUNCTION__);

    load_internal(cvars_data_txt_name.data(), set_defaults);
}

void CVarManager::CVarData::save() {
    ZoneScopedN(__FUNCTION__);

    if (!m_cvar_data) {
        return;
    }

    // Points to the same thing, just different data internally.
    auto cvar_int = m_cvar_data->get<int>();
    auto cvar_float = m_cvar_data->get<float>();

    if (cvar_int == nullptr) {
        return;
    }

    switch (m_type) {
    case Type::BOOL:
        m_frozen_int_value = cvar_int->get();
        break;
    case Type::INT:
        m_frozen_int_value = cvar_int->get();
        break;
    case Type::FLOAT:
        m_frozen_float_value = cvar_float->get();
        break;
    default:
        break;
    };

    save_internal(cvars_data_txt_name.data());
}

void CVarManager::CVarData::freeze() {
    ZoneScopedN(__FUNCTION__);

    if (!m_frozen) {
        return;
    }

    if (!m_cvar_data) {
        return;
    }

    if (!m_ever_frozen) {
        m_ever_frozen = true;
        SPDLOG_INFO("[CVarManager] (Data) First time freezing \"{}\"...", utility::narrow(m_name));
    }

    // Points to the same thing, just different data internally.
    auto cvar_int = m_cvar_data->get<int>();
    auto cvar_float = m_cvar_data->get<float>();

    if (cvar_int == nullptr) {
        return;
    }

    switch (m_type) {
    case Type::BOOL:
        cvar_int->set(m_frozen_int_value);
        break;
    case Type::INT:
        cvar_int->set(m_frozen_int_value);
        break;
    case Type::FLOAT:
        cvar_float->set(m_frozen_float_value);
        break;
    default:
        break;
    };
}

void CVarManager::CVarData::update() {
    ZoneScopedN(__FUNCTION__);

    if (!m_cvar_data) {
        m_cvar_data = sdk::find_cvar_data_cached(m_module, m_name);
    }
}

void CVarManager::CVarData::draw_ui() try {
    ZoneScopedN(__FUNCTION__);

    if (!m_cvar_data) {
        ImGui::TextWrapped("Failed to find cvar data: %s", utility::narrow(m_name).c_str());
        return;
    }

    // Points to the same thing, just different data internally.
    auto cvar_int = m_cvar_data->get<int>();
    auto cvar_float = m_cvar_data->get<float>();

    if (cvar_int == nullptr) {
        ImGui::TextWrapped("Failed to read cvar data: %s", utility::narrow(m_name).c_str());
        return;
    }

    const auto narrow_name = utility::narrow(m_name);

    switch (m_type) {
    case Type::BOOL: {
        auto value = (bool)cvar_int->get();

        if (ImGui::Checkbox(narrow_name.c_str(), &value)) {
            cvar_int->set((int)value); // no need to run on game thread, direct access
            this->save();
        }
        break;
    }
    case Type::INT: {
        auto value = cvar_int->get();

        if (ImGui::SliderInt(narrow_name.c_str(), &value, m_min_int_value, m_max_int_value)) {
            cvar_int->set(value); // no need to run on game thread, direct access
            this->save();
        }
        break;
    }
    case Type::FLOAT: {
        auto value = cvar_float->get();

        if (ImGui::SliderFloat(narrow_name.c_str(), &value, m_min_float_value, m_max_float_value)) {
            cvar_float->set(value); // no need to run on game thread, direct access
            this->save();
        }
        break;
    }
    default:
        ImGui::TextWrapped("Unimplemented cvar type: %s", narrow_name.c_str());
        break;
    }
} catch (...) {
    ImGui::TextWrapped("Failed to read cvar data: %s", utility::narrow(m_name).c_str());
}

void CVarManager::CVarValidated::load(bool defaults) {
    if (defaults) { m_frozen = false; return; }
    load_internal(config_name(), defaults);
    // TOW2 stereo bisection identified VSM. SHf uses this as a candidate,
    // not a proven diagnosis. Honor explicit saved choices in both games.
    if (m_name == L"r.Shadow.Virtual.Enable" && !m_frozen) {
        m_frozen_int_value = 0;
        m_frozen = true;
    }
}

void CVarManager::CVarValidated::update() try {
    if (!m_attempted) {
        m_attempted = true;
        const auto started = std::chrono::steady_clock::now();
        if (is_performance_entry() && !validated_registry().contains(m_name)) {
            // A failed registry discovery is not proof that this variable is absent.
            m_status = validated_registry().empty() ? 2 : 4;
            return;
        }
        m_variable = validated_variable(m_name);
        m_status = m_variable ? 1 : 2;
        spdlog::info("[Validated CVar] {} access={} discovery_ms={:.3f}", utility::narrow(m_name),
            m_variable ? "verified" : "unavailable", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
    if (m_status != 1) return;
    if (!sdk::console_validation::object(m_variable)) { m_status = 3; return; }
    if (m_type == Type::FLOAT) m_float = m_variable->GetFloat();
    else m_int = m_variable->GetInt();
} catch (...) {
    m_status = 3;
    spdlog::error("[Validated CVar] {} read failed; disabled for this session", utility::narrow(m_name));
}

void CVarManager::CVarValidated::save() {
    if (m_status != 1) return;
    if (m_type == Type::FLOAT) m_frozen_float_value = m_float.load();
    else m_frozen_int_value = m_int.load();
    save_internal(config_name());
}

void CVarManager::CVarValidated::freeze() try {
    if (!m_frozen || m_status != 1) return;
    const double desired = m_type == Type::FLOAT ? m_frozen_float_value : m_frozen_int_value;
    if (!std::isfinite(desired)) { m_status = 3; return; }
    const double before = m_type == Type::FLOAT ? m_float.load() : m_int.load();
    if (before != desired) {
        // UE5.5+ SetByConsole. Old UEVR's 0x08000000 is not console priority
        // on this engine. Never touch flags or render-thread shadow data.
        const auto text = m_type == Type::FLOAT ? std::format(L"{:.9g}", m_frozen_float_value) : std::to_wstring(m_frozen_int_value);
        m_variable->Set(text.c_str(), is_validated_cvar_game() ? 0x0E000000 : 0x08000000);
        update();
    }
    const double actual = m_type == Type::FLOAT ? m_float.load() : m_int.load();
    if (!m_ever_frozen || before != desired) {
        // One-time/change-only warning-level evidence survives SHf's usual
        // warning-only logger without enabling noisy per-frame info output.
        spdlog::warn("[Validated CVar] {} requested={} before={} readback={} verified={}",
            utility::narrow(m_name), desired, before, actual, m_status == 1 && actual == desired);
        m_ever_frozen = true;
    }
    if (m_status != 1 || actual != desired) {
        m_status = 3; // Fail closed, rather than hammering a rejected setter.
        spdlog::error("[Validated CVar] {} setter/readback mismatch; disabled", utility::narrow(m_name));
    }
} catch (...) {
    m_status = 3;
    spdlog::error("[Validated CVar] {} write failed; disabled", utility::narrow(m_name));
}

void CVarManager::CVarValidated::draw_ui() {
    const auto name = utility::narrow(m_name);
    const auto label = m_performance_entry ? std::string(m_performance_entry->label) + "##" + name : name;
    const auto status = m_status.load();
    if (status != 1) {
        ImGui::TextWrapped("%s: %s", name.c_str(), status == 0 ? "discovery pending" : status == 2 ? "unavailable (validation failed)" : "access/readback failed");
        return;
    }
    int iv = m_int.load();
    float fv = m_float.load();
    if (m_editing) { iv = m_edit_int; fv = m_edit_float; }
    bool changed{};
    if (m_type == Type::BOOL) { bool value = iv != 0; changed = ImGui::Checkbox(label.c_str(), &value); iv = value; }
    else if (m_type == Type::INT) changed = ImGui::SliderInt(label.c_str(), &iv, m_min_int_value, m_max_int_value);
    else changed = ImGui::SliderFloat(label.c_str(), &fv, m_min_float_value, m_max_float_value);
    if (is_performance_entry() && m_type != Type::BOOL) {
        if (changed) { m_editing = true; m_edit_int = iv; m_edit_float = fv; }
        changed = m_editing && ImGui::IsItemDeactivatedAfterEdit();
        if (changed) m_editing = false;
    }
    if (m_performance_entry && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(name.c_str());
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32);
        ImGui::TextUnformatted(m_performance_entry->help);
        ImGui::PopTextWrapPos(); ImGui::EndTooltip();
    }
    if (changed) {
        if (is_performance_entry()) {
            if (m_type == Type::FLOAT) fv = std::clamp(fv, m_min_float_value, m_max_float_value);
            else iv = std::clamp(iv, m_min_int_value, m_max_int_value);
        }
        GameThreadWorker::get().enqueue([self = std::static_pointer_cast<CVarValidated>(shared_from_this()), iv, fv]() {
            if (self->m_status != 1) return;
            if (self->m_type == Type::FLOAT) self->m_frozen_float_value = fv;
            else self->m_frozen_int_value = iv;
            self->m_frozen = true;
            self->freeze();
            if (self->m_status == 1) self->save();
        });
    }
}

static inline void trim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));

    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

void CVarManager::execute_console_script(sdk::UGameEngine* engine, const std::string& filename) {
    ZoneScopedN(__FUNCTION__);

    if (engine == nullptr && !is_validated_cvar_game()) {
        spdlog::error("[execute_console_script] engine is null");
        return;
    }

    spdlog::info("[execute_console_script] Loading {}...", filename);

    const auto cscript_txt = Framework::get_persistent_dir(filename);

    if (!std::filesystem::exists(cscript_txt)) {
        return;
    }

    std::ifstream cscript_file(utility::widen(cscript_txt.string()));

    if (!cscript_file) {
        spdlog::error("[execute_console_script] Failed to open file {}...", filename);
        return;
    }

    m_script_lines.clear();

    for (std::string line{}; getline(cscript_file, line); ) {
        trim(line);

        // handle comments
        if (line.starts_with('#') || line.starts_with(';')) {
            continue;
        }

        if (line.contains('#')) {
            line = line.substr(0, line.find_first_of('#'));
            trim(line);
        }

        if (line.contains(';')) {
            line = line.substr(0, line.find_first_of(';'));
            trim(line);
        }

        if (line.length() == 0) {
            continue;
        }

        if (!line.starts_with("//")) m_script_lines.push_back(std::move(line));
    }
    m_script_remaining = m_script_lines.size();
    spdlog::info("[CVar script] Queued {} lines; one entry per engine tick", m_script_lines.size());
}

void CVarManager::process_script_line(sdk::UGameEngine* engine) {
    if (m_script_lines.empty()) return;
    const auto line = std::move(m_script_lines.front());
    m_script_lines.pop_front();
    m_script_remaining = m_script_lines.size();
    const auto started = std::chrono::steady_clock::now();
    if (is_validated_cvar_game()) {
            // Explicit-only script path: numeric variables, never arbitrary
            // engine commands or the unresolved UEngine::Exec interface.
            std::istringstream input(line);
            std::string name, value, extra;
            input >> name >> value;
            if (value.empty() || (input >> extra)) return;
            try {
                size_t consumed{};
                const double number = std::stod(value, &consumed);
                if (consumed != value.size() || !std::isfinite(number) || !std::isfinite((float)number)) return;
                for (const auto& cvar : m_all_cvars) {
                    if (cvar->get_name() != utility::widen(name) || !cvar->is_frozen()) continue;
                    const double frozen = cvar->get_type() == CVar::Type::FLOAT ? cvar->get_frozen_float_value() : cvar->get_frozen_int_value();
                    if (cvar_script::action((float)number, 0, true, (float)frozen) == cvar_script::Action::frozen_conflict) {
                        spdlog::warn("[CVar script] {} requested={} conflicts with frozen menu={}; skipped (change/unfreeze menu first)", name, value, frozen);
                        return;
                    }
                }
                auto* variable = validated_variable(utility::widen(name));
                if (!variable) { spdlog::warn("[CVar script] {} unavailable; skipped", name); return; }
                const float before = variable->GetFloat();
                const bool unchanged = cvar_script::action((float)number, before, false, 0) == cvar_script::Action::unchanged;
                if (!unchanged) variable->Set(utility::widen(value).c_str(), 0x0E000000);
                const float actual = variable->GetFloat();
                spdlog::warn("[CVar script] {} requested={} before={} readback={} unchanged={} verified={} elapsed_ms={:.3f}",
                    name, value, before, actual, unchanged, actual == (float)number,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
            } catch (...) { spdlog::warn("[CVar script] Rejected/failed: {}", line); }
    } else if (engine != nullptr) {
        engine->exec(utility::widen(line));
        spdlog::info("[CVar script] Legacy exec '{}' elapsed_ms={:.3f} (no readback)", line,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
}
