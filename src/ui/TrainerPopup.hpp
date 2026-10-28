#pragma once

#include <Geode/Geode.hpp>
#include <functional>

namespace bt {

class TrainerPopup : public geode::Popup {
public:
    static TrainerPopup* create(bool fromPause);
    void rebuild();

protected:
    bool init(bool fromPause);
    void buildNoProfile();
    void buildMismatch();
    void buildProfile();
    void buildRangeList();

    void onPrevStage();
    void onNextStage();
    void onGoRange(int from);
    void onGoTarget();
    void onUndo();
    void onImport();
    void createProfile(bool lite);
    void rebuildKeepingProgress();

    bool m_fromPause = false;
    int m_viewStage = -1;
    cocos2d::CCNode* m_content = nullptr;
};

// Export, import, reset and daily backups.
class ManagePopup : public geode::Popup {
public:
    static ManagePopup* create(std::function<void()> onChange);

protected:
    bool init(std::function<void()> onChange);
    void build();
    void changed();

    std::function<void()> m_onChange;
    cocos2d::CCNode* m_content = nullptr;
};

// When each stage was cleared and what it took, plus a time-left estimate.
class HistoryPopup : public geode::Popup {
public:
    static HistoryPopup* create();

protected:
    bool init();
};

class HeatmapPopup : public geode::Popup {
public:
    static HeatmapPopup* create();

protected:
    bool init();
};

}
