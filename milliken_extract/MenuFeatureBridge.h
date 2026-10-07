#pragma once
/**
 * MenuFeatureBridge — Sprint 5
 * Connects GameMenuOverlay ↔ FeatureHub (discovery + weather).
 */
#include "GameMenuOverlay.h"
#include "FeatureHub.h"
#include "ServerDiscovery.h"
#include <cstdio>

namespace ks {
namespace sim {

inline void wireMenuToFeatures(GameMenuOverlay& menu, FeatureHub& features) {
    // LAN-only fallback. Prefer MenuNetworkBridge.h + NetworkManager when ksnet
    // is available (LAN discovery + optional HTTP lobby via KS_LOBBY_URL).
    menu.onRefreshServerListRequested = [&]() {
        features.discovery.queryLan();
        // Pull entries after tick — caller should tick discovery first
        std::vector<BrowserServerEntry> rows;
        for (const auto& e : features.discovery.servers()) {
            BrowserServerEntry b;
            b.name = e.name;
            b.track = e.track;
            b.host = e.host;
            b.port = e.port;
            b.players = e.players;
            b.maxPlayers = e.maxPlayers;
            b.sessionType = e.sessionType;
            b.ageSec = 0.f; // lastSeen handled by discovery prune
            rows.push_back(b);
        }
        menu.setServerList(rows);
        std::fprintf(stderr, "MenuBridge: %zu LAN servers (use MenuNetworkBridge for lobby)\n",
                     rows.size());
    };

    menu.onWeatherPresetRequested = [&](const std::string& preset) {
        features.weatherCtrl.applyPreset(preset);
        auto& w = features.weatherCtrl.weather();
        menu.setWeatherSummary(w.name, features.weatherCtrl.time().hours);
        if (features.onSetWeather) features.onSetWeather(preset);
    };

    menu.onTimeOfDayRequested = [&](float hours) {
        features.weatherCtrl.setTimeOfDay(hours);
        menu.setWeatherSummary(features.weatherCtrl.weather().name, hours);
        if (features.onSetTimeOfDay) features.onSetTimeOfDay(hours);
    };

    menu.onTimeOfDayAdjustRequested = [&](float delta) {
        float h = features.weatherCtrl.time().hours + delta;
        while (h < 0.f) h += 24.f;
        while (h >= 24.f) h -= 24.f;
        features.weatherCtrl.setTimeOfDay(h);
        menu.setWeatherSummary(features.weatherCtrl.weather().name, h);
        if (features.onSetTimeOfDay) features.onSetTimeOfDay(h);
    };

    menu.onOpenServerBrowserRequested = [&]() {
        features.discovery.queryLan();
    };

    // Sync weather summary once
    menu.setWeatherSummary(features.weatherCtrl.weather().name,
                           features.weatherCtrl.time().hours);
}

} // namespace sim
} // namespace ks
