#include "cbf.hpp"

#include "../includes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace cbf {

Engine* Engine::get() {
    static Engine instance;
    return &instance;
}

void Engine::prepare(
    uint32_t frame,
    Macro const& macro,
    size_t currentAction
) {
    // A flag left over from a previous tick must never swallow this tick's P2 update.
    m_p2Handled = false;

    if (frame == m_preparedFrame)
        return;

    if (!m_armed.empty() && m_tickFrame != frame)
        this->clearArmed();

    m_preparedFrame = frame;

    for (size_t i = currentAction; i < macro.inputs.size(); ++i) {
        auto const& input = macro.inputs[i];

        if (input.frame > frame)
            break;
        if (input.frame < frame)
            continue;

        if (!(input.subframe > 0.0 && input.subframe < 1.0))
            continue;
        if (input.button < 1 || input.button > 3)
            continue;

        m_armed.push_back({
            input.frame,
            input.subframe,
            input.button,
            input.down,
            input.player2,
            i
        });
    }

    std::stable_sort(
        m_armed.begin(),
        m_armed.end(),
        [](ArmedInput const& a, ArmedInput const& b) {
            if (a.frame != b.frame)
                return a.frame < b.frame;
            if (a.fraction != b.fraction)
                return a.fraction < b.fraction;
            return a.actionIndex < b.actionIndex;
        }
    );
}

bool Engine::beginTick() {
    if (m_armed.empty() || m_armed.front().frame != m_preparedFrame)
        return false;

    m_tickFrame = m_preparedFrame;
    m_cursor = 0;
    m_firedActions.clear();
    return true;
}

bool Engine::hasNext() const {
    return m_cursor < m_armed.size();
}

double Engine::nextFraction() const {
    if (!this->hasNext())
        return 1.0;

    return m_armed[m_cursor].fraction;
}

void Engine::fireCurrent() {
    if (!this->hasNext())
        return;

    double const fraction = m_armed[m_cursor].fraction;
    auto* layer = GJBaseGameLayer::get();

    while (
        m_cursor < m_armed.size() &&
        std::abs(m_armed[m_cursor].fraction - fraction) < 0.000001
    ) {
        auto const armed = m_armed[m_cursor++];

        if (layer) {
            bool player2 = armed.player2;
            if (Macro::flipControls())
                player2 = !player2;

            layer->handleButton(
                armed.holding,
                armed.button,
                player2
            );
        }

        m_firedActions.push_back(armed.actionIndex);
    }
}

void Engine::endTick() {
    m_armed.clear();
    m_tickFrame = UINT32_MAX;
    m_cursor = 0;
    m_midStep = false;
    m_p1Split = false;
    m_p2Split = false;
    // m_p2Handled is deliberately kept: PlayerObject::update for P2 consumes it.
    m_rotationDelta = 0.f;
    m_shipRotAccum = 0.f;
    m_shipRotAccumP2 = 0.f;
    m_shipRotHeld = false;
}

bool Engine::consumeFired(size_t actionIndex) {
    auto it = std::find(
        m_firedActions.begin(),
        m_firedActions.end(),
        actionIndex
    );

    if (it == m_firedActions.end())
        return false;

    m_firedActions.erase(it);
    return true;
}

void Engine::clearArmed() {
    m_armed.clear();
    m_tickFrame = UINT32_MAX;
    m_cursor = 0;
}

void Engine::clearFired() {
    m_firedActions.clear();
}

void Engine::reset() {
    m_armed.clear();
    m_firedActions.clear();
    m_preparedFrame = UINT32_MAX;
    m_tickFrame = UINT32_MAX;
    m_cursor = 0;
    m_midStep = false;
    m_p1Split = false;
    m_p2Split = false;
    m_p2Handled = false;
    m_rotationDelta = 0.f;
    m_shipRotAccum = 0.f;
    m_shipRotAccumP2 = 0.f;
    m_shipRotHeld = false;
}

bool canSplit(PlayerObject* player) {
    if (!player)
        return false;

    if (player->m_isOnGround)
        return true;
    if (player->m_touchingRings && player->m_touchingRings->count())
        return true;
    if (player->m_isDashing)
        return true;

    return player->m_isDart ||
           player->m_isBird ||
           player->m_isShip ||
           player->m_isSwing;
}

class $modify(FrameBotPlayerObject, PlayerObject) {
    bool cbfSplitUpdate(float stepDelta) {
        auto* eng = Engine::get();
        auto* layer = GJBaseGameLayer::get();

        if (!layer || this != layer->m_player1)
            return false;
        if (eng->m_midStep)
            return false;

        if (!eng->beginTick())
            return false;

        PlayerObject* p2 =
            layer->m_gameState.m_isDualMode ? layer->m_player2 : nullptr;

        eng->m_p1Split = canSplit(this);
        eng->m_p2Split = canSplit(p2);

        eng->m_p1Pos = this->getPosition();
        eng->m_p2Pos = p2 ? p2->getPosition() : cocos2d::CCPoint{};

        bool const p1OnGround = this->m_isOnGround;
        bool const p2OnGround = p2 && p2->m_isOnGround;

        double previousFraction = 0.0;

        eng->m_midStep = true;
        eng->m_shipRotAccum = 0.f;
        eng->m_shipRotAccumP2 = 0.f;
        eng->m_shipRotHeld = true;

        auto const settle = [&](
            PlayerObject* player,
            bool wasOnGround,
            float delta
        ) {
            if (!player)
                return;

            if ((player->m_yVelocity < 0) ^ player->m_isUpsideDown)
                player->m_isOnGround = wasOnGround;

            bool const slope =
                player->m_isOnSlope && !player->m_isDart;

            layer->checkCollisions(
                player,
                slope ? stepDelta : 0.f,
                true
            );

            player->updateRotation(delta);
        };

        while (eng->hasNext()) {
            double const fraction = std::clamp(
                eng->nextFraction(),
                previousFraction,
                1.0
            );

            double const segment = fraction - previousFraction;

            if (segment > 0.0) {
                float const delta =
                    stepDelta * static_cast<float>(segment);

                eng->m_rotationDelta = delta;

                if (eng->m_p1Split) {
                    PlayerObject::update(delta);
                    settle(this, p1OnGround, delta);
                }

                if (eng->m_p2Split) {
                    p2->update(delta);
                    settle(p2, p2OnGround, delta);
                }
            }

            eng->fireCurrent();
            previousFraction = fraction;
        }

        float const rest =
            stepDelta * static_cast<float>(1.0 - previousFraction);

        eng->m_rotationDelta = rest;

        if (eng->m_p1Split)
            PlayerObject::update(rest);
        else
            PlayerObject::update(stepDelta);

        if (p2) {
            if (eng->m_p2Split)
                p2->update(rest);
            else
                p2->update(stepDelta);
        }

        eng->m_shipRotHeld = false;

        if (eng->m_shipRotAccum != 0.f) {
            PlayerObject::updateShipRotation(eng->m_shipRotAccum);
            eng->m_shipRotAccum = 0.f;
        }

        if (p2 && eng->m_shipRotAccumP2 != 0.f) {
            p2->updateShipRotation(eng->m_shipRotAccumP2);
            eng->m_shipRotAccumP2 = 0.f;
        }

        eng->endTick();

        // P2 was already advanced above; stop the game from stepping it a second time.
        eng->m_p2Handled = (p2 != nullptr);
        return true;
    }

    void update(float dt) {
        auto* eng = Engine::get();

        if (eng->m_p2Handled && !eng->m_midStep) {
            auto* layer = PlayLayer::get();
            if (layer && this == layer->m_player2) {
                eng->m_p2Handled = false;
                return;
            }
        }

        if (this->cbfSplitUpdate(dt))
            return;

        PlayerObject::update(dt);
    }

    void updateRotation(float delta) {
        auto* eng = Engine::get();
        auto* layer = PlayLayer::get();

        if (layer && !eng->m_midStep &&
            eng->m_p1Split &&
            this == layer->m_player1) {
            PlayerObject::updateRotation(eng->m_rotationDelta);
            this->m_lastPosition = eng->m_p1Pos;
            return;
        }

        if (layer && !eng->m_midStep &&
            eng->m_p2Split &&
            this == layer->m_player2) {
            PlayerObject::updateRotation(eng->m_rotationDelta);
            this->m_lastPosition = eng->m_p2Pos;
            return;
        }

        PlayerObject::updateRotation(delta);
    }

    void updateShipRotation(float delta) {
        auto* eng = Engine::get();

        if (eng->m_shipRotHeld) {
            auto* layer = PlayLayer::get();

            if (layer && this == layer->m_player2)
                eng->m_shipRotAccumP2 += delta;
            else
                eng->m_shipRotAccum += delta;

            return;
        }

        PlayerObject::updateShipRotation(delta);
    }
};

} // namespace cbf
