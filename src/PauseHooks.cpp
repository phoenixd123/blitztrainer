#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "Game.hpp"
#include "Store.hpp"
#include "ui/TrainerPopup.hpp"

using namespace geode::prelude;

class $modify(BTPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (!PlayLayer::get() || PlayLayer::get()->m_level->isPlatformer()) return;

        auto spr = CircleButtonSprite::createWithSpriteFrameName("GJ_practiceBtn_001.png", 0.9f, CircleBaseColor::Pink);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](auto) { bt::TrainerPopup::create(true)->show(); });
        btn->setID("blitz-trainer-button"_spr);

        // node-ids gives the pause menu a left-side menu; fall back to our own if it's missing
        if (auto menu = this->getChildByID("left-button-menu")) {
            spr->setScale(0.75f);
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto own = CCMenu::create();
            own->setID("blitz-trainer-menu"_spr);
            auto win = CCDirector::get()->getWinSize();
            own->setPosition({30.f, win.height / 2});
            own->addChild(btn);
            this->addChild(own, 10);
        }

        // quick status line above the practice bar area
        if (Mod::get()->getSettingValue<bool>("pause-summary")) {
            if (auto p = bt::Store::get().profile(); p && !bt::game::profileMismatch()) {
                int si = p->currentStage();
                auto text = si < 0
                    ? std::string("Blitz Trainer: complete!")
                    : fmt::format("Blitz Trainer: stage {}/{}, {}/{} ranges", si + 1, p->stages.size(),
                        p->stages[si].doneCount(), p->stages[si].ranges.size());
                auto label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
                label->setScale(0.45f);
                label->setID("blitz-trainer-summary"_spr);
                auto win = CCDirector::get()->getWinSize();
                label->setPosition({win.width / 2, 22.f});
                this->addChild(label, 10);
            }
        }
    }
};
