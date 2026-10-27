#include <Geode/Geode.hpp>

#include "Game.hpp"
#include "Store.hpp"

using namespace geode::prelude;

// Re-evaluate finished ranges when the training rules change, so raising the
// pass requirement mid-way reopens ranges instead of silently ignoring it.
$on_mod(Loaded) {
    auto refresh = [] {
        auto& store = bt::Store::get();
        if (auto p = store.profile()) {
            bt::refreshDone(*p, store.rules());
            store.save();
        }
        bt::game::refreshHud();
    };
    listenForSettingChanges<int64_t>("required-passes", [refresh](int64_t) { refresh(); });
    listenForSettingChanges<bool>("require-consecutive", [refresh](bool) { refresh(); });

    listenForSettingChanges<bool>("show-hud", [](bool) { bt::game::refreshHud(); });
    listenForSettingChanges<std::string>("hud-position", [](std::string) { bt::game::refreshHud(); });
    listenForSettingChanges<double>("hud-scale", [](double) { bt::game::refreshHud(); });
    listenForSettingChanges<double>("hud-opacity", [](double) { bt::game::refreshHud(); });
}
