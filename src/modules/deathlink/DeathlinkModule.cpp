#include "DeathlinkModule.hpp"
#include <globed/core/game/RemotePlayer.hpp>
#include <globed/core/RoomManager.hpp>
#include <core/hooks/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace globed {

// Maximum value for player progress percentage (uint16_t max = 100%)
constexpr float MAX_PROGRESS_VALUE = 65535.0f;

namespace {

class DelayedDeathSchedule : public CCObject {
public:
    static DelayedDeathSchedule* create(GlobedGJBGL* gjbgl, float delay) {
        if (!gjbgl) return nullptr;
        
        auto ret = new DelayedDeathSchedule;
        ret->m_gjbgl = gjbgl;
        ret->autorelease();
        // CCScheduler will retain this object until the callback executes or is unscheduled
        // The 'false' parameter means the callback should only execute once (not repeat)
        CCScheduler::get()->scheduleSelector(
            schedule_selector(DelayedDeathSchedule::invoke), 
            ret, 
            delay, 
            false
        );
        return ret;
    }

private:
    GlobedGJBGL* m_gjbgl;

    DelayedDeathSchedule() {}

    void invoke(float dt) {
        // Unschedule to prevent multiple invocations
        CCScheduler::get()->unscheduleSelector(
            schedule_selector(DelayedDeathSchedule::invoke), 
            this
        );
        
        // Kill the local player if the game layer is still valid and active
        if (m_gjbgl && m_gjbgl->active()) {
            m_gjbgl->killLocalPlayer();
        }
    }
};

}

DeathlinkModule::DeathlinkModule() = default;

void DeathlinkModule::onModuleInit() {
    this->setAutoEnableMode(AutoEnableMode::Level);
}

void DeathlinkModule::onJoinLevel(GlobedGJBGL* gjbgl, GJGameLevel* level, bool editor) {
    // if deathlink is disabled, disable the module for this level
    if (!RoomManager::get().getSettings().deathlink) {
        (void) this->disable();
    }
}

void DeathlinkModule::onPlayerDeath(GlobedGJBGL* gjbgl, RemotePlayer* player, const PlayerDeath& death) {
    if (!death.isReal || !player || !player->isTeammate()) return;

    // Get local player's progress
    auto localState = gjbgl->getPlayerState();
    auto localProgress = localState.percentage;
    
    // Get remote player's progress
    auto& remoteState = player->getState();
    auto remoteProgress = remoteState.percentage;
    
    // Check if the remote player is ahead of the local player
    if (remoteProgress > localProgress) {
        // Player is ahead, calculate delay based on progress difference
        // Percentage is stored as uint16_t with MAX_PROGRESS_VALUE representing 100% progress
        float progressDiff = (remoteProgress - localProgress) / MAX_PROGRESS_VALUE; // Normalize to 0-1
        
        // Scale delay: 0.5 to 1.5 seconds based on progress difference
        // progressDiff * 2.0 means 2 seconds per 100% difference (e.g., 0.1 difference = 0.2s added)
        float delay = 0.5f + (progressDiff * 2.0f);
        delay = std::min(delay, 1.5f);
        
        // Schedule delayed death
        DelayedDeathSchedule::create(gjbgl, delay);
        return;
    }
    
    // If player is not ahead, kill immediately
    gjbgl->killLocalPlayer();
}

struct GLOBED_MODIFY_ATTR DLPlayLayer : geode::Modify<DLPlayLayer, PlayLayer> {
    static void onModify(auto& self) {
        (void) self.setHookPriority("PlayLayer::resetLevel", -999);

        GLOBED_CLAIM_HOOKS(DeathlinkModule::get(), self,
            "PlayLayer::resetLevel",
        );
    }

    $override
    void resetLevel() {
        auto gameLayer = GlobedGJBGL::get();

        if (gameLayer && gameLayer->active() && gameLayer->isManuallyResetting()) {
            // if the user hit R or otherwise manually reset the level, just play it as a normal death instead
            gameLayer->killLocalPlayer();
            return;
        }

        PlayLayer::resetLevel();
    }
};

}
