// reshade_panel.cpp — SF-14: "SenseForge" tab in ReShade's overlay.
//
// Layout (top to bottom), modelled on RenoDX's add-on tab:
//   * Preset: Desligado | Preset 1 | Preset 2 | Preset 3 — "Desligado" is
//     the master switch ([SenseForge] enabled=false: neutral controller,
//     no flashes, muted audio); each preset slot keeps its own copy of
//     every setting in [Preset.N].
//   * Perfil: Fraco | Medio | Forte (the RenoDX "tone mapper" slot) fills
//     the base intensities of the preset; any hand edit turns it into
//     "Personalizado".
//   * Status ao vivo (RuntimeHub snapshot from the worker).
//   * Ajustes globais (the Sony overlay's settings, SF-6).
//   * Ajuste por efeito ([States] / [EventEffects] overrides).
//   * Criador de MOD ([Dev] creator_tools=1): live event feed, bindings,
//     manifest reload/problems, fire any event, copy status (SF-17, the
//     RenoDX DevKit counterpart).
//
// SF-17: every game-specific string comes from the MOD itself (the "ui"
// section of {game}.sense.json: names, tooltips, order, hidden entries,
// profile bases, guide link). Nothing here knows about any game.
//
// Persistence: the panel writes senseforge_user.ini (fresh load, only its
// own keys, then save) and the worker's hot-reload watcher applies the
// file in <10ms — the same contract as the Sony overlay and Studio.

#include "reshade_imgui.hpp"

#include "reshade_panel.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "ini_config.hpp"
#include "overlay_settings.hpp"
#include "runtime_hub.hpp"
#include "sf_debug_logger.hpp"

namespace senseforge {

namespace {

const ImVec4 kOk(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(0.95f, 0.75f, 0.30f, 1.0f);
const ImVec4 kError(0.95f, 0.40f, 0.40f, 1.0f);

// Panel state — touched only from ReShade's render thread.
struct PanelState {
    bool loaded = false;
    std::filesystem::path ini_path;
    int active_preset = 1;      // kPresetOff or 1-kPresetCount
    int last_preset = 1;        // slot to return to from "Desligado"
    int base_profile = kProfileStrong;  // what "Restaurar perfil" restores
    OverlaySettings settings;
    OverlaySettings saved;
    bool saved_enabled = true;
    int saved_preset = 1;
    std::vector<std::string> category_ids;
    std::vector<std::string> state_ids;
    std::vector<std::string> event_ids;
    bool creator_tools = false;  // [Dev] creator_tools
    ManifestUi ui;               // the MOD's panel description
};

PanelState& Panel() {
    static PanelState state;
    return state;
}

const UiEntry* Find(const std::map<std::string, UiEntry>& entries,
                   const std::string& id) {
    const auto it = entries.find(id);
    return it != entries.end() ? &it->second : nullptr;
}

// Display name declared by the MOD ("ui" section), else the raw id.
const char* NameOf(const std::map<std::string, UiEntry>& entries,
                   const std::string& id) {
    const UiEntry* entry = Find(entries, id);
    return entry != nullptr && !entry->name.empty() ? entry->name.c_str()
                                                    : id.c_str();
}

void TooltipOf(const std::map<std::string, UiEntry>& entries,
               const std::string& id) {
    const UiEntry* entry = Find(entries, id);
    if (entry != nullptr && !entry->description.empty()) {
        ImGui::SetItemTooltip("%s", entry->description.c_str());
    }
}

// Ids in panel order: declared "order" ascending, ties keep the manifest
// order; "hidden" entries are skipped.
std::vector<std::string> Ordered(const std::vector<std::string>& ids,
                                 const std::map<std::string, UiEntry>& entries) {
    std::vector<std::string> visible;
    for (const std::string& id : ids) {
        const UiEntry* entry = Find(entries, id);
        if (entry == nullptr || !entry->hidden) {
            visible.push_back(id);
        }
    }
    std::stable_sort(visible.begin(), visible.end(),
                     [&](const std::string& a, const std::string& b) {
                         const UiEntry* ea = Find(entries, a);
                         const UiEntry* eb = Find(entries, b);
                         return (ea ? ea->order : 0) < (eb ? eb->order : 0);
                     });
    return visible;
}

const UiProfile* ProfileBase(const PanelState& panel, int profile_id) {
    if (profile_id < kProfileWeak || profile_id > kProfileStrong) {
        return nullptr;
    }
    return &panel.ui.profiles[profile_id];
}

std::vector<std::string> UniqueCategories(const RuntimeStatus& status) {
    std::vector<std::string> ids;
    for (const std::string& category : status.state_categories) {
        if (std::find(ids.begin(), ids.end(), category) == ids.end()) {
            ids.push_back(category);
        }
    }
    return ids;
}

// Loads the slot (or, for a slot never saved, the defaults of the Forte
// profile) into the panel model.
void LoadSlot(PanelState& panel, const IniDocument& ini, int slot) {
    OverlaySettings fresh;
    if (!fresh.LoadPreset(ini, slot, panel.category_ids, panel.state_ids,
                          panel.event_ids)) {
        IniDocument empty;
        fresh.Load(empty, panel.category_ids, panel.state_ids,
                   panel.event_ids);
        fresh.ApplyProfile(kProfileStrong,
                           ProfileBase(panel, kProfileStrong));
    }
    panel.settings = fresh;
    panel.base_profile =
        fresh.profile == kProfileCustom ? kProfileStrong : fresh.profile;
}

void Commit(PanelState& panel) {
    IniDocument ini;
    ini.Load(panel.ini_path);  // keep foreign keys (Studio, hand edits)
    const bool enabled = panel.active_preset != kPresetOff;
    ini.SetBool("SenseForge", "enabled", enabled);
    ini.SetInt("SenseForge", "active_preset", panel.active_preset);
    if (enabled) {
        panel.settings.ApplyTo(ini);
        panel.settings.SavePreset(ini, panel.active_preset);
    }
    if (ini.Save(panel.ini_path)) {
        panel.saved = panel.settings;
        panel.saved_enabled = enabled;
        panel.saved_preset = panel.active_preset;
    } else {
        DebugLog("panel: failed to save %s", panel.ini_path.string().c_str());
    }
}

// First frame after the worker published its status: read the ini, pick
// the active slot. An ini from before SF-14 (no [Preset.1]) is migrated:
// its live sections become Preset 1, so the user keeps their values.
void EnsureLoaded(PanelState& panel, const RuntimeStatus& status) {
    const std::vector<std::string> categories = UniqueCategories(status);
    const bool ids_changed = categories != panel.category_ids ||
                             status.state_ids != panel.state_ids ||
                             status.event_ids != panel.event_ids;
    if (panel.loaded && !ids_changed) {
        return;
    }
    panel.ini_path = status.ini_path;
    panel.ui = status.ui;
    panel.category_ids = categories;
    panel.state_ids = status.state_ids;
    panel.event_ids = status.event_ids;

    IniDocument ini;
    ini.Load(panel.ini_path);
    panel.creator_tools = ini.GetBool("Dev", "creator_tools", false);
    if (!ini.GetBool("SenseForge", "enabled", true)) {
        panel.active_preset = kPresetOff;
    } else {
        panel.active_preset = std::clamp(
            static_cast<int>(ini.GetInt("SenseForge", "active_preset", 1)), 1,
            kPresetCount);
    }
    const int slot = panel.active_preset == kPresetOff
                         ? std::clamp(static_cast<int>(ini.GetInt(
                                          "SenseForge", "last_preset", 1)),
                                      1, kPresetCount)
                         : panel.active_preset;
    panel.last_preset = slot;
    if (!ini.HasSection(PresetSection(slot))) {
        panel.settings.Load(ini, panel.category_ids, panel.state_ids,
                            panel.event_ids);
        panel.base_profile = panel.settings.profile == kProfileCustom
                                 ? kProfileStrong
                                 : panel.settings.profile;
    } else {
        LoadSlot(panel, ini, slot);
    }
    panel.saved = panel.settings;
    panel.saved_enabled = panel.active_preset != kPresetOff;
    panel.saved_preset = panel.active_preset;
    if (!panel.loaded && !ini.HasSection(PresetSection(slot))) {
        Commit(panel);  // migration: write Preset 1 once
    }
    panel.loaded = true;
}

bool ComboIndex(const char* label, int* value, const char* const* items,
                int count) {
    return ImGui::Combo(label, value, items, count);
}

// Force slider 0-8 where 0 means "keep the manifest value".
bool ForceSlider(const char* id, int* force) {
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderInt(id, force, 0, 8,
                            *force == 0 ? "manifesto" : "força %d");
}

void DrawStatus(const RuntimeStatus& status, const ManifestUi& ui) {
    if (!status.worker_ready) {
        ImGui::TextColored(kWarn,
                           "Aguardando o runtime (aquecimento do jogo)...");
        return;
    }
    if (status.hid_connected) {
        ImGui::TextColored(kOk, "Controle: conectado (%s)",
                           status.transport == "direct_hid" ? "HID direto"
                                                            : "DSX UDP");
    } else {
        ImGui::TextColored(kError, "Controle: não encontrado");
    }
    ImGui::Text("Estado ativo: %s",
                status.active_state == "off"
                    ? "desligado (preset Desligado)"
                    : NameOf(ui.states, status.active_state));
    if (status.last_flash_id.empty()) {
        ImGui::TextDisabled("Último evento: nenhum ainda");
    } else {
        const double seconds =
            static_cast<double>(GetTickCount64() - status.last_flash_ms) /
            1000.0;
        ImGui::Text("Último evento: %s (há %.1f s) · total %llu",
                    NameOf(ui.events, status.last_flash_id), seconds,
                    static_cast<unsigned long long>(status.flash_count));
    }
    if (status.audio_ok) {
        ImGui::TextColored(kOk, "Áudio do controle: %s",
                           status.audio_device.c_str());
    } else {
        ImGui::TextColored(kWarn, "Áudio do controle: indisponível %s",
                           status.audio_error.c_str());
    }
    if (!status.manifest_ok) {
        ImGui::TextColored(kError, "Perfil do jogo: erro %s",
                           status.manifest_error.c_str());
    } else if (!status.manifest_problems.empty()) {
        ImGui::TextColored(kWarn,
                           "Perfil do jogo: %zu aviso(s), veja em Criador "
                           "de MOD",
                           status.manifest_problems.size());
    } else {
        ImGui::TextColored(kOk, "Perfil do jogo: %s",
                           status.manifest_path.filename().string().c_str());
    }
}

// Returns true when an intensity-type value changed (profile -> custom).
bool DrawGlobal(OverlaySettings& s, const ManifestUi& ui) {
    bool intensity_edit = false;
    static const char* const kHaptic[] = {"Desligado", "Fraca (40%)",
                                          "Média (70%)", "Forte (100%)",
                                          "Boost (130%)"};
    intensity_edit |= ComboIndex("HD Haptics", &s.haptic_intensity, kHaptic, 5);
    ImGui::SetItemTooltip("Vibração de alta definição pelas bobinas do "
                          "controle (USB).");
    static const char* const kTriggers[] = {"Desligado", "Fraco", "Médio",
                                            "Forte", "Máximo"};
    intensity_edit |=
        ComboIndex("Gatilho L2", &s.left_trigger_intensity, kTriggers, 5);
    ImGui::SetItemTooltip("Escala a força de todos os efeitos do L2. "
                          "Máximo = 1,25x o valor do MOD.");
    intensity_edit |=
        ComboIndex("Gatilho R2", &s.right_trigger_intensity, kTriggers, 5);
    ImGui::SetItemTooltip("Escala a força de todos os efeitos do R2. "
                          "Máximo = 1,25x o valor do MOD.");
    ImGui::Checkbox("Desligar gatilhos em menus e mapa",
                    &s.disable_triggers_in_menus);

    ImGui::SeparatorText("Alto-falante do controle");
    ImGui::Checkbox("Ativado##speaker", &s.speaker_enabled);
    ImGui::BeginDisabled(!s.speaker_enabled);
    float volume = static_cast<float>(s.speaker_volume);
    if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f")) {
        s.speaker_volume = volume;
    }
    ImGui::Checkbox("Parry / crítico", &s.sound_parry);
    ImGui::SameLine();
    ImGui::Checkbox("Montaria", &s.sound_mount);
    ImGui::SameLine();
    ImGui::Checkbox("Interface", &s.sound_ui);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Lightbar e LEDs");
    // ini semantics: 0=Off 1=Dynamic 2=Static.
    static const char* const kLightbar[] = {"Desligada", "Dinâmica",
                                            "Cor fixa"};
    ComboIndex("Lightbar", &s.lightbar_mode, kLightbar, 3);
    static const char* const kBrightness[] = {"Escuro (OLED)", "Médio",
                                              "Alto"};
    ComboIndex("Brilho", &s.lightbar_brightness, kBrightness, 3);
    static const char* const kLeds[] = {"Desligados", "Número do jogador",
                                        "Segmentos"};
    ComboIndex("Player LEDs", &s.player_leds_mode, kLeds, 3);

    bool header = false;
    for (auto& [id, enabled] : s.categories) {
        const UiEntry* entry = Find(ui.categories, id);
        if (entry != nullptr && entry->hidden) {
            continue;
        }
        if (!header) {
            ImGui::SeparatorText("Categorias do jogo");
            header = true;
        }
        ImGui::Checkbox(NameOf(ui.categories, id), &enabled);
        TooltipOf(ui.categories, id);
    }
    return intensity_edit;
}

bool DrawPerEffect(OverlaySettings& s, const PanelState& panel) {
    const ManifestUi& ui = panel.ui;
    bool intensity_edit = false;
    const std::vector<std::string> states = Ordered(panel.state_ids, ui.states);
    if (!states.empty()) {
        ImGui::SeparatorText("Estados (gatilhos contínuos)");
        if (ImGui::BeginTable("##states", 4,
                              ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Estado");
            ImGui::TableSetupColumn("Ativo", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("L2");
            ImGui::TableSetupColumn("R2");
            ImGui::TableHeadersRow();
            for (const std::string& id : states) {
                StateOverride& state = s.states[id];
                ImGui::PushID(id.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(NameOf(ui.states, id));
                TooltipOf(ui.states, id);
                ImGui::TableNextColumn();
                ImGui::Checkbox("##on", &state.enabled);
                ImGui::TableNextColumn();
                intensity_edit |= ForceSlider("##l2", &state.left_force);
                ImGui::TableNextColumn();
                intensity_edit |= ForceSlider("##r2", &state.right_force);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    const std::vector<std::string> events = Ordered(panel.event_ids, ui.events);
    if (!events.empty()) {
        ImGui::SeparatorText("Eventos (flashes)");
        if (ImGui::BeginTable("##events", 5,
                              ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Evento");
            ImGui::TableSetupColumn("Ativo", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Intensidade");
            ImGui::TableSetupColumn("Lightbar",
                                    ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            for (const std::string& id : events) {
                EventOverride& event = s.events[id];
                ImGui::PushID(id.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(NameOf(ui.events, id));
                TooltipOf(ui.events, id);
                ImGui::TableNextColumn();
                ImGui::Checkbox("##on", &event.enabled);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                intensity_edit |= ImGui::SliderInt("##intensity",
                                                   &event.intensity, 0, 150,
                                                   "%d%%");
                ImGui::TableNextColumn();
                ImGui::Checkbox("##lightbar", &event.lightbar);
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Testar")) {
                    RuntimeHub::Instance().RequestTestFlash(id);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    return intensity_edit;
}

std::string StatusReport(const RuntimeStatus& status) {
    std::string out = "SenseForge " + status.mod_version + " | " +
                      status.game_title + "\n";
    out += "controle=" + status.transport + " estado=" + status.active_state +
           " prioridade=" + std::to_string(status.active_priority) +
           " bindings live/dead=" + std::to_string(status.bindings_live) +
           "/" + std::to_string(status.bindings_dead) + "\n";
    out += "manifesto=" + status.manifest_path.filename().string() +
           (status.manifest_ok ? std::string(" ok")
                               : " ERRO " + status.manifest_error) +
           "\n";
    for (const std::string& problem : status.manifest_problems) {
        out += "  aviso: " + problem + "\n";
    }
    for (const auto& [id, value] : status.binding_values) {
        char line[160];
        std::snprintf(line, sizeof(line), "  %s = %.3f\n", id.c_str(), value);
        out += line;
    }
    for (const EventLogEntry& entry : status.recent_events) {
        char line[256];
        std::snprintf(line, sizeof(line), "  [%llu] %s %s %.2f %s %s\n",
                      static_cast<unsigned long long>(entry.ms),
                      entry.kind.c_str(), entry.id.c_str(), entry.value,
                      entry.applied ? "aplicado" : "ignorado",
                      entry.note.c_str());
        out += line;
    }
    return out;
}

// SF-17: the RenoDX DevKit counterpart, for people creating a MOD.
void DrawCreatorTools(const RuntimeStatus& status, const ManifestUi& ui) {
    ImGui::TextDisabled("Visível porque [Dev] creator_tools=1 no "
                        "senseforge_user.ini.");
    if (ImGui::Button("Recarregar manifesto")) {
        RuntimeHub::Instance().RequestReloadManifest();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copiar status")) {
        ImGui::SetClipboardText(StatusReport(status).c_str());
    }
    if (status.manifest_problems.empty()) {
        ImGui::TextColored(kOk, "Manifesto: sem problemas");
    } else {
        for (const std::string& problem : status.manifest_problems) {
            ImGui::TextColored(kWarn, "Manifesto: %s", problem.c_str());
        }
    }

    static char fire_id[64] = "";
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##fire", fire_id, sizeof(fire_id));
    ImGui::SameLine();
    if (ImGui::Button("Disparar evento") && fire_id[0] != '\0') {
        RuntimeHub::Instance().RequestTestFlash(fire_id);
    }
    ImGui::SetItemTooltip("Dispara qualquer id, mesmo fora do manifesto. "
                          "O feed abaixo mostra se houve efeito.");

    ImGui::SeparatorText("Estado e bindings");
    ImGui::Text("Estado vencedor: %s (prioridade %d)",
                NameOf(ui.states, status.active_state),
                status.active_priority);
    if (status.binding_values.empty()) {
        ImGui::TextDisabled("Nenhum binding com valor ainda.");
    }
    for (const auto& [id, value] : status.binding_values) {
        ImGui::BulletText("%s = %.3f", id.c_str(), value);
    }

    ImGui::SeparatorText("Últimos eventos");
    if (ImGui::BeginTable("##feed", 4,
                          ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 220.0f))) {
        ImGui::TableSetupColumn("Há");
        ImGui::TableSetupColumn("Tipo");
        ImGui::TableSetupColumn("Id");
        ImGui::TableSetupColumn("Resultado");
        ImGui::TableHeadersRow();
        const std::uint64_t now = GetTickCount64();
        for (auto it = status.recent_events.rbegin();
             it != status.recent_events.rend(); ++it) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%.1f s", static_cast<double>(now - it->ms) / 1000.0);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(it->kind.c_str());
            ImGui::TableNextColumn();
            if (it->kind == "state") {
                ImGui::Text("%s = %.2f", it->id.c_str(), it->value);
            } else {
                ImGui::TextUnformatted(it->id.c_str());
            }
            ImGui::TableNextColumn();
            if (it->applied) {
                ImGui::TextColored(kOk, "aplicado");
            } else {
                ImGui::TextColored(kWarn, "%s",
                                   it->note.empty() ? "ignorado"
                                                    : it->note.c_str());
            }
        }
        ImGui::EndTable();
    }
}

}  // namespace

void DrawReshadePanel(reshade::api::effect_runtime* /*runtime*/) {
    PanelState& panel = Panel();
    const RuntimeStatus status = RuntimeHub::Instance().Snapshot();
    if (status.worker_ready) {
        EnsureLoaded(panel, status);
    }
    panel.ui = status.ui;  // follows manifest hot-reloads

    // ---- MOD header ----------------------------------------------------
    ImGui::Text("SenseForge%s%s · %s",
                status.mod_version.empty() ? "" : " v",
                status.mod_version.c_str(),
                status.game_title.empty() ? "DualSense para PC"
                                          : status.game_title.c_str());
    if (!panel.ui.guide_url.empty()) {
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("Guia do MOD", panel.ui.guide_url.c_str());
    }
    if (!panel.ui.credits.empty()) {
        ImGui::TextDisabled("%s", panel.ui.credits.c_str());
    }
    if (!panel.loaded) {
        ImGui::Separator();
        DrawStatus(status, panel.ui);
        return;
    }
    if (ImGui::GetFrameCount() % 120 == 0) {  // flag may be edited by hand
        IniDocument ini;
        ini.Load(panel.ini_path);
        panel.creator_tools = ini.GetBool("Dev", "creator_tools", false);
    }

    // ---- Preset (RenoDX style) ------------------------------------------
    static const char* const kPresets[] = {"Desligado", "Preset 1",
                                           "Preset 2", "Preset 3"};
    int preset = panel.active_preset;
    if (ImGui::Combo("Preset", &preset, kPresets, kPresetCount + 1) &&
        preset != panel.active_preset) {
        IniDocument ini;
        ini.Load(panel.ini_path);
        if (preset == kPresetOff) {
            ini.SetInt("SenseForge", "last_preset", panel.active_preset);
            ini.Save(panel.ini_path);
        } else {
            LoadSlot(panel, ini, preset);
            panel.last_preset = preset;
        }
        panel.active_preset = preset;
        Commit(panel);
    }
    ImGui::SetItemTooltip("Desligado deixa o controle como no jogo original. "
                          "Cada preset guarda seus próprios ajustes.");
    const bool off = panel.active_preset == kPresetOff;
    if (off) {
        ImGui::TextColored(kWarn,
                           "SenseForge desligado: o controle fica como no "
                           "jogo original.");
    }

    ImGui::BeginDisabled(off);
    // ---- Perfil de intensidade (the RenoDX "tone mapper" slot) -----------
    static const char* const kProfiles[] = {"Personalizado", "Fraco", "Médio",
                                            "Forte"};
    int profile = panel.settings.profile;
    if (ImGui::Combo("Perfil", &profile, kProfiles, 4) &&
        profile != kProfileCustom) {
        panel.settings.ApplyProfile(profile, ProfileBase(panel, profile));
        panel.base_profile = profile;
    }
    ImGui::SetItemTooltip("Preenche as intensidades do preset. Mexer em "
                          "qualquer intensidade torna o perfil "
                          "Personalizado.");
    ImGui::SameLine();
    if (ImGui::Button("Restaurar perfil")) {
        panel.settings.ApplyProfile(panel.base_profile,
                                    ProfileBase(panel, panel.base_profile));
    }
    ImGui::SameLine();
    if (ImGui::Button("Padrões do MOD")) {
        // Every switch back on, every override cleared, profile Forte.
        OverlaySettings defaults;
        IniDocument empty;
        defaults.Load(empty, panel.category_ids, panel.state_ids,
                      panel.event_ids);
        defaults.ApplyProfile(kProfileStrong,
                              ProfileBase(panel, kProfileStrong));
        panel.settings = defaults;
        panel.base_profile = kProfileStrong;
    }
    ImGui::SetItemTooltip("Volta este preset aos valores originais do MOD.");
    ImGui::EndDisabled();

    if (ImGui::CollapsingHeader("Status ao vivo",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawStatus(status, panel.ui);
        if (ImGui::Button("Testar efeito")) {
            RuntimeHub::Instance().RequestTestEffect();
        }
    }

    ImGui::BeginDisabled(off);
    bool intensity_edit = false;
    if (ImGui::CollapsingHeader("Ajustes globais",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        intensity_edit |= DrawGlobal(panel.settings, panel.ui);
    }
    if (ImGui::CollapsingHeader("Ajuste por efeito")) {
        intensity_edit |= DrawPerEffect(panel.settings, panel);
    }
    ImGui::EndDisabled();
    if (intensity_edit) {
        panel.settings.profile = kProfileCustom;
    }

    if (panel.creator_tools && ImGui::CollapsingHeader("Criador de MOD")) {
        DrawCreatorTools(status, panel.ui);
    }

    ImGui::Separator();
    ImGui::TextDisabled("Salvo em %s · overlay: tecla Home",
                        panel.ini_path.filename().string().c_str());

    // Save when something changed, but never mid-drag: a slider writes the
    // file once, on release.
    if (!off && !ImGui::IsAnyItemActive() &&
        !panel.settings.Equals(panel.saved)) {
        Commit(panel);
    }
}

}  // namespace senseforge
