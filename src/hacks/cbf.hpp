#pragma once

#include <cocos2d.h>

#include <cstdint>
#include <vector>

class PlayerObject;
struct Macro;

namespace cbf {

struct ArmedInput {
    uint32_t frame = 0;
    double fraction = 0.0;
    int button = 1;
    bool holding = false;
    bool player2 = false;
    size_t actionIndex = 0;
};

class Engine {
public:
    static Engine* get();

    void prepare(uint32_t frame, Macro const& macro, size_t currentAction);

    bool beginTick();
    bool hasNext() const;
    double nextFraction() const;
    void fireCurrent();
    void endTick();

    bool consumeFired(size_t actionIndex);
    void clearArmed();
    void clearFired();
    void reset();

    bool m_midStep = false;
    bool m_p1Split = false;
    bool m_p2Split = false;
    bool m_p2Handled = false;

    float m_rotationDelta = 0.f;
    cocos2d::CCPoint m_p1Pos{};
    cocos2d::CCPoint m_p2Pos{};

    float m_shipRotAccum = 0.f;
    float m_shipRotAccumP2 = 0.f;
    bool m_shipRotHeld = false;

private:
    std::vector<ArmedInput> m_armed;
    std::vector<size_t> m_firedActions;

    uint32_t m_preparedFrame = UINT32_MAX;
    uint32_t m_tickFrame = UINT32_MAX;
    size_t m_cursor = 0;
};

bool canSplit(PlayerObject* player);

} // namespace cbf
