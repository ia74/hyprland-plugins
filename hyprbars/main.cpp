#define WLR_USE_UNSTABLE

#include <unistd.h>

#include <any>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Window.hpp>
#include <hyprland/src/config/ConfigManager.hpp>

#include "barDeco.hpp"
#include "globals.hpp"

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static void onNewWindow(void* self, std::any data) {
    // data is guaranteed
    const auto PWINDOW = std::any_cast<PHLWINDOW>(data);

    if (!PWINDOW->m_bX11DoesntWantBorders) {
        std::unique_ptr<CHyprBar> bar = std::make_unique<CHyprBar>(PWINDOW);
        g_pGlobalState->bars.push_back(bar.get());
        HyprlandAPI::addWindowDecoration(PHANDLE, PWINDOW, std::move(bar));
    }
}

static void onCloseWindow(void* self, std::any data) {
    // data is guaranteed
    const auto PWINDOW = std::any_cast<PHLWINDOW>(data);

    const auto BARIT = std::find_if(g_pGlobalState->bars.begin(), g_pGlobalState->bars.end(), [PWINDOW](const auto& bar) { return bar->getOwner() == PWINDOW; });

    if (BARIT == g_pGlobalState->bars.end())
        return;

    // we could use the API but this is faster + it doesn't matter here that much.
    PWINDOW->removeWindowDeco(*BARIT);
}

static void onPreConfigReload() {
    g_pGlobalState->buttons.clear();
}

static void onUpdateWindowRules(PHLWINDOW window) {
    const auto BARIT = std::find_if(g_pGlobalState->bars.begin(), g_pGlobalState->bars.end(), [window](const auto& bar) { return bar->getOwner() == window; });

    if (BARIT == g_pGlobalState->bars.end())
        return;

    (*BARIT)->updateRules();
    window->updateWindowDecos();
}

Hyprlang::CParseResult onNewButton(const char* K, const char* V) {
    std::string            v = V;
    CVarList               vars(v);

    Hyprlang::CParseResult result;

    // hyprbars-button = color, size, icon, action

    if (vars[0].empty() || vars[1].empty()) {
        result.setError("var 1 and 2 cannot be empty");
        return result;
    }

    float size = 10;
    try {
        size = std::stof(vars[1]);
    } catch (std::exception& e) {
        result.setError("failed parsing var 2 as int");
        return result;
    }

    g_pGlobalState->buttons.push_back(SHyprButton{vars[3], configStringToInt(vars[0]), size, vars[2]});

    for (auto& b : g_pGlobalState->bars) {
        b->m_bButtonsDirty = true;
    }

    return result;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    if (HASH != GIT_COMMIT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprbars] Failure in initialization: Version mismatch (headers ver is not equal to running hyprland ver)",
                                     CColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hb] Version mismatch");
    }

    g_pGlobalState = std::make_unique<SGlobalState>();

    static auto P  = HyprlandAPI::registerCallbackDynamic(PHANDLE, "openWindow", [&](void* self, SCallbackInfo& info, std::any data) { onNewWindow(self, data); });
    static auto P2 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "closeWindow", [&](void* self, SCallbackInfo& info, std::any data) { onCloseWindow(self, data); });
    static auto P3 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "windowUpdateRules",
                                                          [&](void* self, SCallbackInfo& info, std::any data) { onUpdateWindowRules(std::any_cast<PHLWINDOW>(data)); });

    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_color", Hyprlang::INT{configStringToInt("rgba(33333388)")});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_height", Hyprlang::INT{15});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_text_color", Hyprlang::INT{configStringToInt("rgba(ffffffff)")});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_text_size", Hyprlang::INT{10});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_title_enabled", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_text_font", Hyprlang::STRING{"Sans"});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_text_align", Hyprlang::STRING{"center"});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_part_of_window", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_precedence_over_border", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_buttons_alignment", Hyprlang::STRING{"right"});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_padding", Hyprlang::INT{7});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprbars:bar_button_padding", Hyprlang::INT{5});

    HyprlandAPI::addConfigKeyword(PHANDLE, "hyprbars-button", onNewButton, Hyprlang::SHandlerOptions{});
    static auto P4 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "preConfigReload", [&](void* self, SCallbackInfo& info, std::any data) { onPreConfigReload(); });

    // add deco to existing windows
    for (auto& w : g_pCompositor->m_vWindows) {
        if (w->isHidden() || !w->m_bIsMapped)
            continue;

        onNewWindow(nullptr /* unused */, std::any(w));
    }

    HyprlandAPI::reloadConfig();

    HyprlandAPI::addNotification(PHANDLE, "[hyprbars] Initialized successfully!", CColor{0.2, 1.0, 0.2, 1.0}, 5000);

    return {"hyprbars", "A plugin to add title bars to windows.", "Vaxry", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    for (auto& m : g_pCompositor->m_vMonitors)
        m->scheduledRecalc = true;
}
