#pragma once

#include <Geode/Geode.hpp>

namespace bt {

class TrainerPopup : public geode::Popup {
public:
    static TrainerPopup* create(bool fromPause);

protected:
    bool init(bool fromPause);
    void rebuild();
    void buildNoProfile();
    void buildMismatch();
    void buildProfile();
    void buildRangeList();

    void onPrevStage();
    void onNextStage();
    void onGoRange(int from);
    void onGoTarget();
    void onUndo();
    void onExport();
    void onImport();
    void onReset();
    void createProfile(bool lite);
    void rebuildKeepingProgress();

    CCMenuItemSpriteExtra* textButton(char const* text, float scale, geode::Function<void(CCMenuItemSpriteExtra*)> cb, char const* bg = "GJ_button_01.png");

    bool m_fromPause = false;
    int m_viewStage = -1;
    cocos2d::CCNode* m_content = nullptr;
};

class HeatmapPopup : public geode::Popup {
public:
    static HeatmapPopup* create();

protected:
    bool init();
};

}
