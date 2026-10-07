#include "macro_timeline_layer.hpp"

#include <limits>

#include "../hacks/cbf.hpp"
#include "../practice_fixes/practice_fixes.hpp"


namespace {
    enum class FrameWindowKind {
        Normal,
        Shared,
        Optimal,
        Recovery,
        Alternating,
        Disconnected,
        Impossible
    };

    struct FrameTaskResult {
        int frame = 0;
        double subframe = 0.0;
        bool passed = false;
        cocos2d::CCPoint position = {0, 0};
    };

    struct FrameTask {
        int eventIndex = -1;
        int frame = 0;
        double subframe = 0.0;
        int button = 1;
        bool player2 = false;
        bool down = true;
        std::vector<FrameTaskResult> results;
        FrameWindowKind kind = FrameWindowKind::Normal;
        int windowCount = 0;
        int windowLow = 0;
        int windowHigh = 0;
        bool cbfOnly = false;
        bool recoveryMode = false;
        bool finished = false;
        bool baselineFailed = false;
        double spanLow = 0.0;      // passing span relative to the original click, in frames
        double spanHigh = 0.0;
        double windowWidth = 0.0;  // how many frames the click can move and still pass
        int grid = 10;             // substeps per frame this result was measured at
        int tested = 0;
        int passedCount = 0;
    };

    struct FrameTaskProbe {
        int frame = 0;
        double subframe = 0.0;
        int contextShift = 0;
        int pairedFrame = 0;
        double pairedSubframe = 0.0;
        bool paired = false;
        bool alternating = false;
    };

    class FastFrameWindowSimulator final {
    public:
        struct Snapshot {
            GJGameState gameState;
            PlayerData p1;
            PlayerData p2;
            bool hasP2 = false;
            int frame = 0;
        };

        struct Result {
            int frame = 0;
            double subframe = 0.0;
            bool passed = false;
            CCPoint position = {0, 0};
        };

        explicit FastFrameWindowSimulator(PlayLayer* pl) : m_pl(pl) {
            if (!m_pl)
                return;

            m_fakeP1 = createFakePlayer("framebot-window-p1");
            if (m_pl->m_player2)
                m_fakeP2 = createFakePlayer("framebot-window-p2");
        }

        ~FastFrameWindowSimulator() {
            if (m_fakeP1)
                m_fakeP1->removeFromParentAndCleanup(true);
            if (m_fakeP2)
                m_fakeP2->removeFromParentAndCleanup(true);
        }

        bool valid() const {
            return m_pl && m_fakeP1;
        }

        Snapshot capture(int frame) const {
            Snapshot snapshot;
            snapshot.frame = frame;

            if (!m_pl || !m_pl->m_player1)
                return snapshot;

            snapshot.gameState = m_pl->m_gameState;
            snapshot.p1 = PlayerPracticeFixes::saveData(m_pl->m_player1);
            snapshot.hasP2 = m_pl->m_player2 != nullptr;

            if (snapshot.hasP2)
                snapshot.p2 = PlayerPracticeFixes::saveData(m_pl->m_player2);

            return snapshot;
        }

        std::vector<Result> analyze(
            const Snapshot& snapshot,
            const Macro& baseMacro,
            const FrameTask& task,
            int lowFrame,
            int highFrame
        ) {
            std::vector<Result> results;
            if (!valid() || lowFrame > highFrame)
                return results;

            results.reserve(static_cast<size_t>(highFrame - lowFrame + 1));

            for (int candidateFrame = lowFrame;
                 candidateFrame <= highFrame;
                 ++candidateFrame) {
                Macro candidateMacro = baseMacro;

                if (task.eventIndex < 0 ||
                    task.eventIndex >= static_cast<int>(candidateMacro.inputs.size()))
                    break;

                candidateMacro.inputs[task.eventIndex].frame = candidateFrame;
                candidateMacro.inputs[task.eventIndex].subframe = task.subframe;
                std::sort(candidateMacro.inputs.begin(), candidateMacro.inputs.end());

                int passFrame = candidateFrame + 12;
                const double candidateTime =
                    static_cast<double>(candidateFrame) + task.subframe;

                for (const auto& event : candidateMacro.inputs) {
                    if (event.player2 == task.player2 &&
                        event.button <= 3 &&
                        event.getPreciseFrame() > candidateTime + 0.0001) {
                        passFrame = event.frame;
                        break;
                    }
                }

                results.push_back(simulateCandidate(
                    snapshot,
                    candidateMacro,
                    task,
                    candidateFrame,
                    task.subframe,
                    passFrame
                ));
            }

            m_pl->m_gameState = snapshot.gameState;
            return results;
        }

    private:
        PlayLayer* m_pl = nullptr;
        PlayerObject* m_fakeP1 = nullptr;
        PlayerObject* m_fakeP2 = nullptr;

        PlayerObject* createFakePlayer(const char* id) {
            if (!m_pl->m_objectLayer)
                return nullptr;

            auto* player = PlayerObject::create(1, 1, m_pl, m_pl, true);
            if (!player)
                return nullptr;

            player->setID(id);
            player->setVisible(false);
            m_pl->m_objectLayer->addChild(player);
            return player;
        }

        static void clearCollisionLogs(PlayerObject* player) {
            if (!player)
                return;

            if (player->m_collisionLogTop)
                player->m_collisionLogTop->removeAllObjects();
            if (player->m_collisionLogBottom)
                player->m_collisionLogBottom->removeAllObjects();
            if (player->m_collisionLogLeft)
                player->m_collisionLogLeft->removeAllObjects();
            if (player->m_collisionLogRight)
                player->m_collisionLogRight->removeAllObjects();
        }

        static void applyInput(PlayerObject* player, const input& event) {
            if (!player)
                return;

            const auto button = static_cast<PlayerButton>(event.button);
            if (event.down)
                player->pushButton(button);
            else
                player->releaseButton(button);
        }

        void restorePlayer(
            PlayerObject* fake,
            PlayerObject* real,
            const PlayerData& data,
            bool player2
        ) {
            if (!fake || !real)
                return;

            fake->copyAttributes(real);
            PlayerPracticeFixes::applyData(fake, data, player2, true);
            fake->setVisible(false);
            fake->m_isDead = false;
        }

        Result simulateCandidate(
            const Snapshot& snapshot,
            const Macro& macro,
            const FrameTask& task,
            int targetFrame,
            double targetSubframe,
            int passFrame
        ) {
            Result result;
            result.frame = targetFrame;
            result.subframe = targetSubframe;

            m_pl->m_gameState = snapshot.gameState;

            restorePlayer(m_fakeP1, m_pl->m_player1, snapshot.p1, false);
            if (snapshot.hasP2 && m_fakeP2 && m_pl->m_player2)
                restorePlayer(m_fakeP2, m_pl->m_player2, snapshot.p2, true);

            int inputIndex = 0;
            while (inputIndex < static_cast<int>(macro.inputs.size()) &&
                   macro.inputs[inputIndex].frame <= snapshot.frame)
                ++inputIndex;

            const float tps = std::max(1.0f, Global::getTPS());
            const float physicsDt = 1.0f / tps;
            const float delta = physicsDt * 60.0f;
            bool targetSeen = false;

            for (int frame = snapshot.frame + 1; frame <= passFrame; ++frame) {
                while (inputIndex < static_cast<int>(macro.inputs.size()) &&
                       macro.inputs[inputIndex].frame <= frame) {
                    auto event = macro.inputs[inputIndex];
                    bool player2 = event.player2;

                    if (Macro::flipControls())
                        player2 = !player2;

                    applyInput(
                        player2 && m_fakeP2 ? m_fakeP2 : m_fakeP1,
                        event
                    );

                    ++inputIndex;
                }

                m_pl->m_gameState.m_totalTime += physicsDt;
                m_pl->m_gameState.m_unkDouble3 +=
                    physicsDt / std::max(0.0001f, m_pl->m_gameState.m_timeWarp);
                ++m_pl->m_gameState.m_currentProgress;

                if (m_fakeP1) {
                    m_fakeP1->m_totalTime += physicsDt;
                    clearCollisionLogs(m_fakeP1);
                    m_fakeP1->update(delta);
                    if (m_pl->checkCollisions(m_fakeP1, delta, false) == 1)
                        m_fakeP1->m_isDead = true;
                }

                if (m_fakeP2 && m_pl->m_gameState.m_isDualMode) {
                    m_fakeP2->m_totalTime += physicsDt;
                    clearCollisionLogs(m_fakeP2);
                    m_fakeP2->update(delta);
                    if (m_pl->checkCollisions(m_fakeP2, delta, false) == 1)
                        m_fakeP2->m_isDead = true;
                }

                if (!targetSeen && frame >= targetFrame) {
                    auto* targetPlayer =
                        task.player2 && m_fakeP2 ? m_fakeP2 : m_fakeP1;

                    if (!targetPlayer)
                        break;

                    targetSeen = true;
                    result.position = targetPlayer->getPosition();
                }

                if ((m_fakeP1 && m_fakeP1->m_isDead) ||
                    (m_fakeP2 && m_pl->m_gameState.m_isDualMode &&
                     m_fakeP2->m_isDead))
                    return result;

                if (m_pl->m_levelEndAnimationStarted) {
                    result.passed = true;
                    return result;
                }

                if (targetSeen && frame >= passFrame) {
                    result.passed = true;
                    return result;
                }
            }

            result.passed = targetSeen;
            return result;
        }
    };


    class FrameTaskPopup final : public framebot::Popup<>, public TextInputDelegate {
    public:
        static FrameTaskPopup* create(MacroTimeline* timeline) {
            auto ret = new FrameTaskPopup();
            ret->m_timeline = timeline;
            if (ret->initAnchored(520, 320, Utils::getTexture().c_str())) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

    protected:
        bool setup() override {
            this->setTitle("Frame Tasks");

            m_actionMenu = CCMenu::create();
            m_actionMenu->setPosition({0, 0});
            m_mainLayer->addChild(m_actionMenu);

            m_list = CCMenu::create();
            m_list->setPosition({0, 0});
            m_mainLayer->addChild(m_list);

            auto addLabel = [&](const char* text, float x, float y, SEL_MenuHandler callback) {
                auto label = CCLabelBMFont::create(text, "bigFont.fnt");
                label->setScale(0.42f);
                auto item = CCMenuItemLabel::create(label, this, callback);
                item->setPosition({x, y});
                m_actionMenu->addChild(item);
            };

            addLabel("Test", 36, 20, menu_selector(FrameTaskPopup::onTestSelected));
            addLabel("Analyze", 100, 20, menu_selector(FrameTaskPopup::onAnalyze));
            addLabel("Stop", 159, 20, menu_selector(FrameTaskPopup::onStop));
            addLabel("Add Selected", 235, 20, menu_selector(FrameTaskPopup::onAddSelected));
            addLabel("Apply", 307, 20, menu_selector(FrameTaskPopup::onApplyWindow));
            addLabel("Subdivide", 378, 20, menu_selector(FrameTaskPopup::onSubdivide));

            auto addInput = [&](const char* placeholder, float x, CCTextInputNode*& target) {
                auto* bg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
                bg->setContentSize({72, 30});
                bg->setScale(0.55f);
                bg->setColor({0, 0, 0});
                bg->setOpacity(90);
                bg->setPosition({x, 52});
                m_mainLayer->addChild(bg);

                target = CCTextInputNode::create(70, 24, placeholder, "chatFont.fnt");
                target->setPosition({x, 52});
                target->m_textField->setAnchorPoint({0.5f, 0.5f});
                target->ignoreAnchorPointForPosition(true);
                target->setMaxLabelScale(0.7f);
                target->setMouseEnabled(true);
                target->setTouchEnabled(true);
                target->setContentSize({58, 20});
                target->setAllowedChars("-0123456789");
                target->setMaxLabelWidth(52.f);
                target->setMaxLabelLength(8);
                target->setDelegate(this);
                m_mainLayer->addChild(target);
            };

            auto* lowLabel = CCLabelBMFont::create("Low", "chatFont.fnt");
            lowLabel->setScale(0.36f);
            lowLabel->setPosition({335, 52});
            m_mainLayer->addChild(lowLabel);

            auto* highLabel = CCLabelBMFont::create("High", "chatFont.fnt");
            highLabel->setScale(0.36f);
            highLabel->setPosition({433, 52});
            m_mainLayer->addChild(highLabel);

            addInput("low", 369, m_lowInput);
            addInput("high", 467, m_highInput);

            m_status = CCLabelBMFont::create("Select a task or input", "chatFont.fnt");
            m_status->setScale(0.40f);
            m_status->setAnchorPoint({0.0f, 0.5f});
            m_status->setPosition({18, 52});
            m_mainLayer->addChild(m_status);

            if (auto* pl = PlayLayer::get())
                m_simulator = std::make_unique<FastFrameWindowSimulator>(pl);

            this->schedule(schedule_selector(FrameTaskPopup::updateTaskRunner), 0.016f);

            if (m_timeline) {
                int const selected = m_timeline->getSelectedEventIndex();
                if (selected >= 0) {
                    s_tasks.push_back(makeTask(selected));
                    m_selectedTaskIndex = static_cast<int>(s_tasks.size()) - 1;
                }
            }

            refreshList();
            if (m_selectedTaskIndex >= 0)
                selectTask(m_selectedTaskIndex);
            else if (!s_tasks.empty())
                selectTask(0);

            return true;
        }

        void onClose(CCObject* sender) override {
            cancelTesting();
            Popup::onClose(sender);
        }

        void textChanged(CCTextInputNode* node) override {
            if (node != m_lowInput && node != m_highInput)
                return;
        }

        void selectTask(int index) {
            if (index < 0 || index >= static_cast<int>(s_tasks.size()))
                return;

            m_selectedTaskIndex = index;
            const auto& task = s_tasks[index];
            const int low = task.finished ? task.windowLow : task.frame;
            const int high = task.finished ? task.windowHigh : task.frame;

            if (m_lowInput)
                m_lowInput->setString(std::to_string(low).c_str());
            if (m_highInput)
                m_highInput->setString(std::to_string(high).c_str());

            m_status->setString(
                fmt::format("Selected input {} | window {}..{}", index + 1, low, high).c_str()
            );
        }

        static bool parseFrame(CCTextInputNode* node, int& value) {
            if (!node)
                return false;

            std::string text = node->getString();
            if (text.empty() || text == "-")
                return false;

            try {
                size_t consumed = 0;
                int parsed = std::stoi(text, &consumed);
                if (consumed != text.size())
                    return false;
                value = parsed;
                return true;
            } catch (...) {
                return false;
            }
        }

        FrameTask makeTask(int index) const {
            FrameTask task;
            if (!m_timeline)
                return task;

            const input* event = m_timeline->getEvent(index);
            if (!event)
                return task;

            task.eventIndex = index;
            task.frame = event->frame;
            task.subframe = event->subframe;
            task.button = event->button;
            task.player2 = event->player2;
            task.down = event->down;
            return task;
        }

        void onTestSelected(CCObject*) {
            if (m_testing || !m_timeline)
                return;

            clearMarkers();

            FrameTask task;
            if (m_selectedTaskIndex >= 0 && m_selectedTaskIndex < static_cast<int>(s_tasks.size())) {
                task = s_tasks[m_selectedTaskIndex];
            } else {
                int index = m_timeline->getSelectedEventIndex();
                task = makeTask(index);
                if (task.eventIndex < 0) {
                    m_status->setString("Select an input first");
                    return;
                }
            }

            s_tasks.clear();
            s_tasks.push_back(task);
            m_selectedTaskIndex = 0;
            s_grid = 0;
            startTesting(0, false);
        }

        void onApplyWindow(CCObject*) {
            if (m_testing)
                return;

            int index = m_selectedTaskIndex;

            if (index < 0 || index >= static_cast<int>(s_tasks.size())) {
                if (!m_timeline) {
                    m_status->setString("Select an input first");
                    return;
                }

                FrameTask task = makeTask(m_timeline->getSelectedEventIndex());
                if (task.eventIndex < 0) {
                    m_status->setString("Select an input first");
                    return;
                }

                s_tasks.push_back(task);
                index = static_cast<int>(s_tasks.size()) - 1;
                m_selectedTaskIndex = index;
            }

            int low = 0;
            int high = 0;
            if (!parseFrame(m_lowInput, low) || !parseFrame(m_highInput, high)) {
                m_status->setString("Enter valid Low and High frames");
                return;
            }

            low = std::max(0, low);
            high = std::max(0, high);

            if (low > high) {
                m_status->setString("Low cannot be greater than High");
                return;
            }

            auto& task = s_tasks[index];
            task.windowLow = low;
            task.windowHigh = high;
            task.spanLow = static_cast<double>(low) - task.frame;
            task.spanHigh = static_cast<double>(high) - task.frame;
            task.windowWidth = static_cast<double>(high - low + 1);
            task.windowCount = high - low + 1;
            task.tested = 0;
            task.passedCount = task.windowCount;
            task.results.clear();
            task.baselineFailed = false;
            task.kind = FrameWindowKind::Normal;
            task.finished = true;

            refreshList();
            selectTask(index);
            m_status->setString(
                fmt::format("Manual window set: {}..{} ({} frames)", low, high, task.windowCount).c_str()
            );
        }

        void onAnalyze(CCObject*) {
            if (m_testing || !m_timeline)
                return;

            clearMarkers();
            generateAllTasks();
            m_selectedTaskIndex = -1;
            if (s_tasks.empty()) {
                m_status->setString("No inputs to analyze");
                return;
            }

            s_grid = 0;
            startTesting(0, true);
        }

        static constexpr int markerTag = 0xF7A5;

        void clearMarkers() {
            if (auto* pl = PlayLayer::get())
                if (auto* markerLayer = pl->getChildByTag(markerTag))
                    markerLayer->removeFromParentAndCleanup(true);
        }

        void onStop(CCObject*) {
            cancelTesting();
        }

        void onAddSelected(CCObject*) {
            if (!m_timeline)
                return;

            int index = m_timeline->getSelectedEventIndex();
            FrameTask task = makeTask(index);
            if (task.eventIndex < 0)
                return;

            s_tasks.push_back(task);
            m_selectedTaskIndex = static_cast<int>(s_tasks.size()) - 1;
            selectTask(m_selectedTaskIndex);
            refreshList();
            m_status->setString(fmt::format("Added task at {}", formatTime(task.frame, task.subframe)).c_str());
        }

        void onSelectTask(CCObject* sender) {
            auto item = static_cast<CCNode*>(sender);
            selectTask(item->getTag());
        }

        void generateAllTasks() {
            if (!m_timeline)
                return;

            auto const* events = m_timeline->getEvents();
            if (!events)
                return;

            s_tasks.clear();
            s_tasks.reserve(events->size());

            for (int i = 0; i < static_cast<int>(events->size()); ++i) {
                const auto& event = (*events)[i];

                FrameTask task;
                task.eventIndex = i;
                task.frame = event.frame;
                task.subframe = event.subframe;
                task.button = event.button;
                task.player2 = event.player2;
                task.down = event.down;
                s_tasks.push_back(task);
            }
        }

        // "How many more frames do I need to pass this gap?" Double the substeps per frame
        // and re-test, so a click that is impossible (or only 1 frame wide) on the current
        // grid can be found between its samples, e.g. widening 1.0f to 1.6f.
        void onSubdivide(CCObject*) {
            if (m_testing || s_tasks.empty())
                return;

            if (!cbf::enabled()) {
                m_status->setString("Enable CBF in the menu to test sub-frames");
                return;
            }

            constexpr int maxGrid = 80;
            int const next = activeGrid() * 2;

            if (next > maxGrid) {
                m_status->setString(fmt::format("Already at max subdivision (1/{})", activeGrid()).c_str());
                return;
            }

            s_grid = next;
            m_status->setString(fmt::format("Subdividing: 1/{} frame steps", next).c_str());
            startTesting(0, true);
        }

        void onStartAll(CCObject*) {
            onAnalyze(nullptr);
        }

        void refreshList() {
            if (!m_list)
                return;

            m_list->removeAllChildren();

            float y = 220.0f;
            for (int i = 0; i < static_cast<int>(s_tasks.size()); ++i) {
                auto& task = s_tasks[i];

                int successful = 0;
                for (const auto& result : task.results)
                    successful += result.passed ? 1 : 0;

                std::string resultText;
                if (task.finished) {
                    if (task.baselineFailed) {
                        resultText = "  [BASELINE FAILED]";
                    } else if (task.kind == FrameWindowKind::Impossible) {
                        resultText = "  [X]";
                    } else {
                        resultText = fmt::format(
                            "  [{}..{}] {:.0f}f",
                            static_cast<int>(std::round(task.spanLow)),
                            static_cast<int>(std::round(task.spanHigh)),
                            task.windowWidth
                        );
                    }
                }

                auto text = CCLabelBMFont::create(
                    fmt::format("{}: {}{}", i + 1, formatTime(task.frame, task.subframe), resultText).c_str(),
                    "chatFont.fnt"
                );
                text->setScale(0.38f);
                text->setAnchorPoint({0, 0.5f});
                text->setPosition({45, y});

                auto item = CCMenuItemLabel::create(text, this, menu_selector(FrameTaskPopup::onSelectTask));
                item->setTag(i);
                item->setPosition({250, y});
                m_list->addChild(item);

                y -= 22.0f;
                if (y < 78.0f)
                    break;
            }
        }

        static std::string formatTime(int frame, double subframe) {
            if (std::abs(subframe) < 0.0001)
                return std::to_string(frame);

            return fmt::format(
                "{}.{:02d}",
                frame,
                static_cast<int>(std::round(subframe * 100.0))
            );
        }

        static const char* formatWindowKind(FrameWindowKind kind) {
            switch (kind) {
                case FrameWindowKind::Shared: return "A+";
                case FrameWindowKind::Optimal: return "A~";
                case FrameWindowKind::Recovery: return "A^";
                case FrameWindowKind::Alternating: return "A/B";
                case FrameWindowKind::Disconnected: return "A-";
                case FrameWindowKind::Impossible: return "X";
                default: return "A";
            }
        }

        int findTaskEvent(const Macro& macro, const FrameTask& task) const {
            if (task.eventIndex >= 0 && task.eventIndex < static_cast<int>(macro.inputs.size())) {
                const auto& event = macro.inputs[task.eventIndex];
                if (event.button == task.button &&
                    event.player2 == task.player2 &&
                    event.down == task.down)
                    return task.eventIndex;
            }

            int best = -1;
            double bestDistance = std::numeric_limits<double>::max();
            const double precise = static_cast<double>(task.frame) + task.subframe;

            for (int i = 0; i < static_cast<int>(macro.inputs.size()); ++i) {
                const auto& event = macro.inputs[i];

                if (event.button != task.button ||
                    event.player2 != task.player2 ||
                    event.down != task.down)
                    continue;

                double distance = std::abs(event.getPreciseFrame() - precise);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = i;
                }
            }

            return best;
        }

        int findEventNear(const Macro& macro, const input& original, double precise) const {
            int best = -1;
            double bestDistance = std::numeric_limits<double>::max();

            for (int i = 0; i < static_cast<int>(macro.inputs.size()); ++i) {
                const auto& event = macro.inputs[i];

                if (event.button != original.button ||
                    event.player2 != original.player2 ||
                    event.down != original.down)
                    continue;

                double distance = std::abs(event.getPreciseFrame() - precise);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = i;
                }
            }

            return best;
        }

        int findAdjacentEvent(const Macro& macro, int eventIndex, bool previous) const {
            if (eventIndex < 0 || eventIndex >= static_cast<int>(macro.inputs.size()))
                return -1;

            const auto& target = macro.inputs[eventIndex];
            int best = -1;

            for (int i = 0; i < static_cast<int>(macro.inputs.size()); ++i) {
                if (i == eventIndex)
                    continue;

                const auto& event = macro.inputs[i];
                if (event.player2 != target.player2 || event.button > 3)
                    continue;

                if (previous) {
                    if (event.getPreciseFrame() >= target.getPreciseFrame())
                        continue;
                    if (best < 0 || event.getPreciseFrame() > macro.inputs[best].getPreciseFrame())
                        best = i;
                } else {
                    if (event.getPreciseFrame() <= target.getPreciseFrame())
                        continue;
                    if (best < 0 || event.getPreciseFrame() < macro.inputs[best].getPreciseFrame())
                        best = i;
                }
            }

            return best;
        }

        int findNextSamePlayer(
            const Macro& macro,
            bool player2,
            double after
        ) const {
            int best = -1;

            for (int i = 0; i < static_cast<int>(macro.inputs.size()); ++i) {
                const auto& event = macro.inputs[i];

                if (event.player2 != player2 ||
                    event.button > 3 ||
                    event.getPreciseFrame() <= after)
                    continue;

                if (best < 0 ||
                    event.getPreciseFrame() < macro.inputs[best].getPreciseFrame())
                    best = i;
            }

            return best;
        }

        std::vector<int> validOffsets(const FrameTask& task) const {
            std::vector<int> offsets;

            for (const auto& result : task.results) {
                if (result.passed && std::abs(result.subframe) < 0.0001)
                    offsets.push_back(result.frame - task.frame);
            }

            std::sort(offsets.begin(), offsets.end());
            offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
            return offsets;
        }

        static std::vector<int> sampleOffsets(
            const std::vector<int>& values,
            size_t count
        ) {
            if (values.size() <= count)
                return values;

            std::vector<int> result;
            result.reserve(count);

            for (size_t i = 0; i < count; ++i) {
                const size_t index =
                    i * (values.size() - 1) / std::max<size_t>(1, count - 1);
                result.push_back(values[index]);
            }

            return result;
        }

        // Substeps per frame used for testing. 0 follows the menu's Substep Divider;
        // Subdivide doubles it to look for passes in gaps the coarser grid steps over.
        static int activeGrid() {
            return s_grid > 0 ? s_grid : cbf::substepDivider();
        }

        void buildCandidates(const FrameTask& task) {
            m_candidates.clear();
            m_earlyDone = false;
            m_lateDone = false;

            m_minTestFrame = std::max(0, task.frame - m_maxShift);
            m_maxTestFrame = task.frame + m_maxShift;

            const int eventIndex = task.eventIndex;
            if (eventIndex >= 0 && eventIndex < static_cast<int>(m_backupMacro.inputs.size())) {
                if (eventIndex > 0)
                    m_minTestFrame = std::max(
                        m_minTestFrame,
                        static_cast<int>(m_backupMacro.inputs[eventIndex - 1].frame) + 1
                    );

                if (eventIndex + 1 < static_cast<int>(m_backupMacro.inputs.size()))
                    m_maxTestFrame = std::min(
                        m_maxTestFrame,
                        static_cast<int>(m_backupMacro.inputs[eventIndex + 1].frame) - 1
                    );
            }

            // Always establish the recorded input as the known-good baseline.
            m_candidates.push_back({
                task.frame,
                task.subframe,
                0,
                0,
                0.0,
                false,
                false
            });

            m_earlyDone = m_minTestFrame >= task.frame;
            m_lateDone = m_maxTestFrame <= task.frame;
        }

        void startTesting(int taskIndex, bool all) {
            if (m_testing || taskIndex < 0 || taskIndex >= static_cast<int>(s_tasks.size()))
                return;

            PlayLayer* pl = PlayLayer::get();
            if (!pl || !m_timeline)
                return;

            m_backupMacro = Global::get().macro;
            m_backupState = Global::get().state;
            m_backupCurrentAction = Global::get().currentAction;
            m_backupCurrentFrameFix = Global::get().currentFrameFix;
            m_backupRestart = Global::get().restart;
            m_backupFirstAttempt = Global::get().firstAttempt;
            m_backupRespawnFrame = Global::get().respawnFrame;
            m_taskIndex = taskIndex;
            m_testingAll = all;
            m_completedAll = false;
            m_fastMode = s_grid == 0 && m_simulator && m_simulator->valid();
            m_testing = true;
            cbf::setDividerOverride(activeGrid());

            if (m_testingAll) {
                for (auto& task : s_tasks)
                    task.finished = false;
            } else {
                s_tasks[m_taskIndex].finished = false;
            }

            auto& task = s_tasks[m_taskIndex];
            task.results.clear();
            task.baselineFailed = false;
            task.kind = FrameWindowKind::Normal;
            task.windowCount = 0;
            task.windowLow = 0;
            task.windowHigh = 0;
            task.cbfOnly = false;
            task.recoveryMode = false;

            m_probeStage = ProbeStage::Primary;
            m_alternatingRuns.clear();
            m_pairRuns.clear();
            m_alternatingOffsets.clear();

            buildCandidates(task);
            m_candidateIndex = 0;
            m_targetSeen = false;
            m_attemptStartFrame = 0;

            auto& g = Global::get();
            g.macro = m_backupMacro;
            g.state = state::playing;
            g.currentAction = 0;
            g.currentFrameFix = 0;
            g.restart = true;
            g.firstAttempt = true;
            g.respawnFrame = -1;

            if (m_fastMode) {
                m_prepareFrame = std::max(
                    0,
                    task.frame - m_maxShift - 2
                );
                pl->resetLevelFromStart();
                m_status->setString(
                    fmt::format(
                        "Physics analyze {} / {}",
                        m_taskIndex + 1,
                        s_tasks.size()
                    ).c_str()
                );
                return;
            }

            beginCandidate();
        }

        void beginCandidate() {
            if (!m_testing)
                return;

            if (m_taskIndex < 0 || m_taskIndex >= static_cast<int>(s_tasks.size())) {
                finishTesting();
                return;
            }

            if (m_candidateIndex >= static_cast<int>(m_candidates.size())) {
                finalizeTaskWindow(s_tasks[m_taskIndex]);
                s_tasks[m_taskIndex].finished = true;

                if (m_testingAll && m_taskIndex + 1 < static_cast<int>(s_tasks.size())) {
                    ++m_taskIndex;

                    auto& nextTask = s_tasks[m_taskIndex];
                    nextTask.results.clear();
                    nextTask.baselineFailed = false;
                    nextTask.kind = FrameWindowKind::Normal;
                    nextTask.windowCount = 0;
                    nextTask.windowLow = 0;
                    nextTask.windowHigh = 0;
                    nextTask.cbfOnly = false;
                    nextTask.recoveryMode = false;

                    m_probeStage = ProbeStage::Primary;
                    m_completedAll = false;
                    buildCandidates(nextTask);
                    m_candidateIndex = 0;
                    beginCandidate();
                    return;
                }

                m_completedAll = m_testingAll;
                finishTesting();
                return;
            }

            PlayLayer* pl = PlayLayer::get();
            if (!pl) {
                finishTesting();
                return;
            }

            auto& task = s_tasks[m_taskIndex];
            const auto candidate = m_candidates[m_candidateIndex];

            Macro candidateMacro = m_backupMacro;
            const int eventIndex = findTaskEvent(candidateMacro, task);
            if (eventIndex < 0) {
                ++m_candidateIndex;
                beginCandidate();
                return;
            }

            candidateMacro.inputs[eventIndex].setPreciseFrame(
                static_cast<double>(candidate.frame) + candidate.subframe
            );

            const double candidatePrecise =
                static_cast<double>(candidate.frame) + candidate.subframe;

            m_passFrame = -1;
            m_passSubframe = 0.0;

            for (const auto& input : candidateMacro.inputs) {
                if (input.player2 == task.player2 &&
                    input.button <= 3 &&
                    input.getPreciseFrame() > candidatePrecise + 0.0001) {
                    m_passFrame = input.frame;
                    m_passSubframe = input.subframe;
                    break;
                }
            }

            // Keep the trial bounded even for the final input in a macro.
            if (m_passFrame < 0)
                m_passFrame = candidate.frame + 12;

            auto& g = Global::get();
            g.macro = candidateMacro;
            g.state = state::playing;
            g.currentAction = 0;
            g.currentFrameFix = 0;
            g.restart = true;
            g.firstAttempt = true;
            g.respawnFrame = -1;

            m_targetFrame = candidate.frame;
            m_targetSubframe = candidate.subframe;
            m_targetSeen = false;
            m_targetPosition = CCPoint{0.0f, 0.0f};
            m_attemptStartFrame = 0;

            pl->resetLevelFromStart();

            m_status->setString(
                fmt::format(
                    "{} {}/{} | frame {}",
                    m_testingAll ? "Analyze" : "Test",
                    m_taskIndex + 1,
                    s_tasks.size(),
                    m_targetFrame
                ).c_str()
            );
        }

        void updateTaskRunner(float) {
            if (!m_testing) {
                if (m_timeline) {
                    int const selected = m_timeline->getSelectedEventIndex();
                    if (selected >= 0) {
                        int existing = -1;
                        for (int i = 0; i < static_cast<int>(s_tasks.size()); ++i) {
                            if (s_tasks[i].eventIndex == selected) {
                                existing = i;
                                break;
                            }
                        }

                        if (existing < 0) {
                            s_tasks.push_back(makeTask(selected));
                            existing = static_cast<int>(s_tasks.size()) - 1;
                            refreshList();
                        }

                        if (existing >= 0 && m_selectedTaskIndex != existing)
                            selectTask(existing);
                    }
                }
                return;
            }

            PlayLayer* pl = PlayLayer::get();
            if (!pl)
                return finishTesting();

            int frame = Global::getCurrentFrame();

            if (m_fastMode) {
                if (frame < m_prepareFrame)
                    return;

                if (m_taskIndex < 0 || m_taskIndex >= static_cast<int>(s_tasks.size()))
                    return finishTesting();

                auto& task = s_tasks[m_taskIndex];
                auto snapshot = m_simulator->capture(frame);

                int minFrame = std::max(0, task.frame - m_maxShift);
                int maxFrame = task.frame + m_maxShift;

                if (task.eventIndex > 0)
                    minFrame = std::max(
                        minFrame,
                        static_cast<int>(m_backupMacro.inputs[task.eventIndex - 1].frame) + 1
                    );

                if (task.eventIndex + 1 < static_cast<int>(m_backupMacro.inputs.size()))
                    maxFrame = std::min(
                        maxFrame,
                        static_cast<int>(m_backupMacro.inputs[task.eventIndex + 1].frame) - 1
                    );

                if (minFrame < snapshot.frame)
                    minFrame = snapshot.frame;

                auto results = m_simulator->analyze(
                    snapshot,
                    m_backupMacro,
                    task,
                    minFrame,
                    maxFrame
                );

                task.results.clear();
                task.results.reserve(results.size());

                bool baselinePassed = false;
                for (const auto& result : results) {
                    task.results.push_back({
                        result.frame,
                        result.subframe,
                        result.passed,
                        result.position
                    });

                    if (result.frame == task.frame &&
                        std::abs(result.subframe - task.subframe) < 0.0001)
                        baselinePassed = result.passed;
                }

                task.baselineFailed = !baselinePassed;
                finalizeTaskWindow(task);
                task.finished = true;

                if (task.baselineFailed && m_testingAll) {
                    m_status->setString(
                        fmt::format(
                            "Analyze stopped: baseline failed at {}",
                            task.frame
                        ).c_str()
                    );
                    m_completedAll = false;
                    finishTesting();
                    return;
                }

                if (m_testingAll &&
                    m_taskIndex + 1 < static_cast<int>(s_tasks.size())) {
                    ++m_taskIndex;

                    auto& nextTask = s_tasks[m_taskIndex];
                    nextTask.results.clear();
                    nextTask.baselineFailed = false;
                    nextTask.kind = FrameWindowKind::Normal;
                    nextTask.windowCount = 0;
                    nextTask.windowLow = 0;
                    nextTask.windowHigh = 0;
                    nextTask.windowWidth = 0.0;
                    nextTask.spanLow = 0.0;
                    nextTask.spanHigh = 0.0;
                    nextTask.tested = 0;
                    nextTask.passedCount = 0;
                    nextTask.finished = false;

                    m_prepareFrame = std::max(
                        frame,
                        std::max(0, nextTask.frame - m_maxShift - 2)
                    );

                    m_status->setString(
                        fmt::format(
                            "Physics analyze {} / {}",
                            m_taskIndex + 1,
                            s_tasks.size()
                        ).c_str()
                    );
                    return;
                }

                m_completedAll = m_testingAll;
                finishTesting();
                return;
            }

            if (m_attemptStartFrame == 0 && frame > 0)
                m_attemptStartFrame = frame;

            if (!m_targetSeen && frame >= m_targetFrame) {
                PlayerObject* player =
                    s_tasks[m_taskIndex].player2 ? pl->m_player2 : pl->m_player1;

                if (player) {
                    m_targetPosition = player->getPosition();
                    m_targetSeen = true;

                    if (player->m_isShip || player->m_isSwing) {
                        m_recoveryProbe = true;
                        s_tasks[m_taskIndex].recoveryMode = true;

                        if (m_passFrame >= 0)
                            m_passFrame += 8;
                    }
                }
            }

            if (pl->m_player1 && pl->m_player1->m_isDead) {
                recordResult(false);
                return;
            }

            if (pl->m_levelEndAnimationStarted) {
                recordResult(true);
                return;
            }

            const int passFrame = m_passFrame >= 0
                ? m_passFrame
                : m_targetFrame + 12;

            if (m_targetSeen && frame >= passFrame)
                recordResult(true);
        }

        void recordResult(bool passed) {
            if (!m_testing)
                return;

            auto& task = s_tasks[m_taskIndex];

            FrameTaskResult result;
            result.frame = m_targetFrame;
            result.subframe = m_targetSubframe;
            result.passed = passed;
            result.position = m_targetPosition;

            const int offset = result.frame - task.frame;

            if (m_probeStage == ProbeStage::Primary) {
                task.results.push_back(result);

                    // The original macro timing must survive. If it does not, probing this
                // input would only measure a broken baseline. For whole-level analysis,
                // skip this input and continue instead of aborting the entire run.
                if (offset == 0 && !passed) {
                    task.baselineFailed = true;
                    m_candidates.clear();
                    ++m_candidateIndex;
                    beginCandidate();
                    return;
                }

                if (offset == 0) {
                    if (!m_earlyDone)
                        m_candidates.push_back({
                            task.frame - 1,
                            task.subframe,
                            0,
                            0,
                            0.0,
                            false,
                            false
                        });

                    if (!m_lateDone)
                        m_candidates.push_back({
                            task.frame + 1,
                            task.subframe,
                            0,
                            0,
                            0.0,
                            false,
                            false
                        });
                } else if (offset < 0) {
                    if (!passed) {
                        m_earlyDone = true;
                    } else {
                        int nextFrame = result.frame - 1;
                        if (nextFrame < m_minTestFrame)
                            m_earlyDone = true;
                        else
                            m_candidates.push_back({
                                nextFrame,
                                task.subframe,
                                0,
                                0,
                                0.0,
                                false,
                                false
                            });
                    }
                } else {
                    if (!passed) {
                        m_lateDone = true;
                    } else {
                        int nextFrame = result.frame + 1;
                        if (nextFrame > m_maxTestFrame)
                            m_lateDone = true;
                        else
                            m_candidates.push_back({
                                nextFrame,
                                task.subframe,
                                0,
                                0,
                                0.0,
                                false,
                                false
                            });
                    }
                }
            }

            ++m_candidateIndex;
            beginCandidate();
        }

        bool hasAlternatingBehavior() const {
            if (m_alternatingRuns.empty() || m_alternatingOffsets.empty())
                return false;

            int oddChanges = 0;
            int evenChanges = 0;

            for (int targetShift : m_alternatingOffsets) {
                bool minus = false;
                bool zero = false;
                bool plus = false;

                for (const auto& run : m_alternatingRuns) {
                    if (run.targetShift != targetShift)
                        continue;

                    if (run.contextShift == -1)
                        minus = run.passed;
                    else if (run.contextShift == 0)
                        zero = run.passed;
                    else if (run.contextShift == 1)
                        plus = run.passed;
                }

                if (minus == zero && plus == zero)
                    continue;

                if (std::abs(targetShift) % 2)
                    ++oddChanges;
                else
                    ++evenChanges;
            }

            return oddChanges >= 2 && oddChanges > evenChanges + 1;
        }

        bool hasDependentRelationship(bool& shared) const {
            shared = false;

            if (m_pairRuns.empty())
                return false;

            struct Row {
                int a = 0;
                int minB = 1000000;
                int maxB = -1000000;
            };

            std::vector<Row> rows;

            for (const auto& run : m_pairRuns) {
                if (!run.passed)
                    continue;

                auto it = std::find_if(
                    rows.begin(),
                    rows.end(),
                    [&](const Row& row) {
                        return row.a == run.contextShift;
                    }
                );

                if (it == rows.end()) {
                    rows.push_back({
                        run.contextShift,
                        run.pairedShift,
                        run.pairedShift
                    });
                } else {
                    it->minB = std::min(it->minB, run.pairedShift);
                    it->maxB = std::max(it->maxB, run.pairedShift);
                }
            }

            if (rows.size() < 3)
                return false;

            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
                return a.a < b.a;
            });

            int negativeSteps = 0;
            int minWidth = 1000000;
            int maxWidth = 0;

            for (size_t i = 0; i < rows.size(); ++i) {
                const int width = rows[i].maxB - rows[i].minB + 1;
                minWidth = std::min(minWidth, width);
                maxWidth = std::max(maxWidth, width);

                if (i > 0 && rows[i].minB < rows[i - 1].minB)
                    ++negativeSteps;
            }

            if (negativeSteps < 2)
                return false;

            shared = maxWidth - minWidth <= 2;
            return true;
        }

        void finalizeTaskWindow(FrameTask& task) {
            std::vector<int> passedFrames;
            passedFrames.reserve(task.results.size());

            for (const auto& result : task.results) {
                if (result.passed && std::abs(result.subframe - task.subframe) < 0.0001)
                    passedFrames.push_back(result.frame);
            }

            std::sort(passedFrames.begin(), passedFrames.end());
            passedFrames.erase(
                std::unique(passedFrames.begin(), passedFrames.end()),
                passedFrames.end()
            );

            task.tested = static_cast<int>(task.results.size());
            task.passedCount = static_cast<int>(passedFrames.size());

            if (!passedFrames.empty()) {
                task.windowLow = passedFrames.front();
                task.windowHigh = passedFrames.back();

                bool hole = false;
                for (size_t i = 1; i < passedFrames.size(); ++i) {
                    if (passedFrames[i] != passedFrames[i - 1] + 1) {
                        hole = true;
                        break;
                    }
                }

                task.spanLow =
                    static_cast<double>(task.windowLow) -
                    static_cast<double>(task.frame);
                task.spanHigh =
                    static_cast<double>(task.windowHigh) -
                    static_cast<double>(task.frame);
                task.windowWidth =
                    static_cast<double>(task.windowHigh - task.windowLow + 1);

                task.windowCount = static_cast<int>(passedFrames.size());
                task.kind = hole
                    ? FrameWindowKind::Disconnected
                    : FrameWindowKind::Normal;
            } else {
                task.windowLow = task.windowHigh = task.frame;
                task.spanLow = task.spanHigh = 0.0;
                task.windowWidth = 0.0;
                task.windowCount = 0;
                task.kind = FrameWindowKind::Impossible;
            }

            for (const auto& result : task.results) {
                if (result.passed)
                    addMarker(result);
            }
        }

        void buildAlternatingProbes(const FrameTask& task) {
            m_candidates.clear();
            m_alternatingRuns.clear();

            const int eventIndex = findTaskEvent(m_backupMacro, task);
            const int previous =
                findAdjacentEvent(m_backupMacro, eventIndex, true);

            if (previous < 0)
                return;

            m_alternatingOffsets = sampleOffsets(validOffsets(task), 7);
            if (m_alternatingOffsets.empty())
                return;

            m_probeStage = ProbeStage::Alternating;

            for (int contextShift : {-1, 0, 1}) {
                for (int offset : m_alternatingOffsets) {
                    m_candidates.push_back({
                        task.frame + offset,
                        0.0,
                        contextShift,
                        0,
                        0.0,
                        false,
                        true
                    });
                }
            }
        }

        void buildPairProbes(const FrameTask& task) {
            m_candidates.clear();
            m_pairRuns.clear();

            const int eventIndex = findTaskEvent(m_backupMacro, task);
            const int next = findAdjacentEvent(m_backupMacro, eventIndex, false);

            if (next < 0)
                return;

            const auto offsets = sampleOffsets(validOffsets(task), 5);
            if (offsets.empty())
                return;

            m_probeStage = ProbeStage::Pair;
            m_pairBaseFrame = m_backupMacro.inputs[next].frame;

            for (int aOffset : offsets) {
                for (int bOffset = -5; bOffset <= 5; ++bOffset) {
                    m_candidates.push_back({
                        task.frame + aOffset,
                        0.0,
                        aOffset,
                        m_pairBaseFrame + bOffset,
                        m_backupMacro.inputs[next].subframe,
                        true,
                        false
                    });
                }
            }
        }

        void addMarker(const FrameTaskResult& result) {
            PlayLayer* pl = PlayLayer::get();
            if (!pl)
                return;

            auto* markerLayer = pl->getChildByTag(FrameTaskPopup::markerTag);
            if (!markerLayer) {
                markerLayer = CCLayer::create();
                markerLayer->setTag(markerTag);
                markerLayer->setZOrder(100000);
                pl->addChild(markerLayer);
            }

            auto* draw = CCDrawNode::create();
            draw->drawCircle(
                result.position,
                10.0f,
                ccc4f(0.1f, 1.0f, 0.2f, 0.85f),
                1.5f,
                ccc4f(0.1f, 1.0f, 0.2f, 0.95f),
                24
            );
            markerLayer->addChild(draw);

            auto label = CCLabelBMFont::create(
                formatTime(result.frame, result.subframe).c_str(),
                "chatFont.fnt"
            );
            label->setScale(0.34f);
            label->setAnchorPoint({0.5f, 0.0f});
            label->setPosition({result.position.x, result.position.y + 11.0f});
            markerLayer->addChild(label);
        }

        void finishTesting() {
            if (!m_testing)
                return;

            auto& g = Global::get();
            int const grid = activeGrid();
            bool const completedAll = m_completedAll;

            g.macro = m_backupMacro;
            g.state = m_backupState;
            g.currentAction = m_backupCurrentAction;
            g.currentFrameFix = m_backupCurrentFrameFix;
            g.restart = m_backupRestart;
            g.firstAttempt = m_backupFirstAttempt;
            g.respawnFrame = m_backupRespawnFrame;

            m_testing = false;
            m_fastMode = false;
            m_prepareFrame = 0;
            cbf::setDividerOverride(0);

            if (completedAll) {
                for (auto& task : s_tasks)
                    task.finished = true;
            } else if (m_taskIndex >= 0 && m_taskIndex < static_cast<int>(s_tasks.size())) {
                s_tasks[m_taskIndex].finished = true;
                m_selectedTaskIndex = m_taskIndex;
            }

            Macro::updateTPS();
            refreshList();

            int total = 0;
            for (const auto& task : s_tasks)
                total += task.passedCount;

            std::string note = fmt::format(
                "Finished: {} inputs tested, {} valid frames",
                s_tasks.size(),
                total
            );

            if (grid != cbf::substepDivider())
                note += fmt::format(" | grid 1/{}", grid);

            m_status->setString(note.c_str());
        }

        void cancelTesting() {
            if (!m_testing)
                return;

            auto& g = Global::get();
            g.macro = m_backupMacro;
            g.state = m_backupState;
            g.currentAction = m_backupCurrentAction;
            g.currentFrameFix = m_backupCurrentFrameFix;
            g.restart = m_backupRestart;
            g.firstAttempt = m_backupFirstAttempt;
            g.respawnFrame = m_backupRespawnFrame;

            m_testing = false;
            m_fastMode = false;
            m_prepareFrame = 0;
            cbf::setDividerOverride(0);
            m_status->setString("Testing stopped");
            refreshList();
            Macro::updateTPS();
        }

    private:
        MacroTimeline* m_timeline = nullptr;
        CCMenu* m_actionMenu = nullptr;
        CCMenu* m_list = nullptr;
        CCLabelBMFont* m_status = nullptr;

        bool m_testing = false;
        bool m_testingAll = false;
        bool m_completedAll = false;
        bool m_fastMode = false;
        int m_prepareFrame = 0;
        int m_selectedTaskIndex = -1;
        CCTextInputNode* m_lowInput = nullptr;
        CCTextInputNode* m_highInput = nullptr;
        bool m_earlyDone = false;
        bool m_lateDone = false;
        int m_minTestFrame = 0;
        int m_maxTestFrame = 0;
        static constexpr int m_maxShift = 48;
        int m_taskIndex = -1;
        int m_candidateIndex = 0;
        int m_targetFrame = 0;
        double m_targetSubframe = 0.0;
        int m_passFrame = -1;
        double m_passSubframe = 0.0;
        int m_attemptStartFrame = 0;
        bool m_targetSeen = false;
        CCPoint m_targetPosition = {0, 0};

        Macro m_backupMacro;
        state m_backupState = state::none;
        size_t m_backupCurrentAction = 0;
        size_t m_backupCurrentFrameFix = 0;
        bool m_backupRestart = false;
        bool m_backupFirstAttempt = false;
        int m_backupRespawnFrame = -1;

        enum class ProbeStage {
            Primary,
            Alternating,
            Pair
        };

        struct AlternatingRun {
            int contextShift = 0;
            int targetShift = 0;
            bool passed = false;
        };

        struct PairRun {
            int contextShift = 0;
            int pairedShift = 0;
            bool passed = false;
        };

        ProbeStage m_probeStage = ProbeStage::Primary;
        std::vector<FrameTaskProbe> m_candidates;
        std::vector<AlternatingRun> m_alternatingRuns;
        std::vector<PairRun> m_pairRuns;
        std::vector<int> m_alternatingOffsets;

        int m_probeContextShift = 0;
        int m_probePairedFrame = 0;
        double m_probePairedSubframe = 0.0;
        int m_pairBaseFrame = 0;
        bool m_recoveryProbe = false;

        std::unique_ptr<FastFrameWindowSimulator> m_simulator;

        static inline std::vector<FrameTask> s_tasks;
        static inline int s_grid = 0;
    };
}

MacroTimelineLayer* MacroTimelineLayer::create(Macro* macro) {
    auto* ret = new MacroTimelineLayer();
    ret->macro = macro;
    ret->timeline = std::make_unique<MacroTimeline>(macro);
    ret->inspector = std::make_unique<MacroEventInspector>(ret->timeline.get());

    auto win = CCDirector::sharedDirector()->getWinSize();
    float width = win.width - 24.0f;

    if (ret->initAnchored(width, 235.0f, macro, Utils::getTexture().c_str())) {
        // This is an in-game overlay, not a conventional popup.
        ret->m_bgSprite->setVisible(false);
        if (ret->m_closeBtn)
            ret->m_closeBtn->setVisible(false);
        if (ret->m_title)
            ret->m_title->setVisible(false);

        ret->autorelease();
        return ret;
    }

    delete ret;
    return nullptr;
}

void MacroTimelineLayer::restorePauseMenu() {
    if (!pauseMenuWasVisible || !hiddenPauseLayer)
        return;

    auto* pl = PlayLayer::get();

    if (gameWasPaused && pl && !pl->m_isPaused)
        pl->pauseGame(false);

    if (hiddenPauseLayer)
        hiddenPauseLayer->setVisible(true);

    pauseMenuWasVisible = false;
}

MacroTimelineLayer::~MacroTimelineLayer() {
    restorePauseMenu();
}

bool MacroTimelineLayer::setup(Macro* setupMacro) {
    macro = setupMacro;

    this->setTitle("Macro Timeline");

    // The timeline is opened from the pause menu, but it is an in-game overlay.
    // Temporarily hide the pause layer and resume the level so the player remains controllable.
    if (auto* pause = Global::getPauseLayer()) {
        hiddenPauseLayer = pause;
        pauseMenuWasVisible = pause->isVisible();

        if (auto* pl = PlayLayer::get())
            gameWasPaused = pl->m_isPaused;

        if (pauseMenuWasVisible)
            pause->setVisible(false);

        if (gameWasPaused) {
            if (auto* pl = PlayLayer::get())
                pl->pauseGame(true);
        }
    }

    // A popup's m_mainLayer has its origin at the popup's bottom-left, not the screen
    // centre, and this overlay is wider than the popup it was created from. Lay everything
    // out under our own root, centred horizontally in screen space and sitting just above
    // the bottom edge (content spans roughly -92 .. +27 around the root).
    {
        auto win = CCDirector::sharedDirector()->getWinSize();
        overlay = CCNode::create();
        overlay->setPosition({win.width / 2.0f, 92.0f + 8.0f});
        this->addChild(overlay, 10);
    }

    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);
    this->registerWithTouchDispatcher();

    // Set up main content
    initToolbar();
    initTimeline();
    initInspector();
    inspectorBg->setVisible(false);
    inspectorMenu->setVisible(false);
    for (auto* label : inspectorLabels)
        if (label) label->setVisible(false);

    bool const cbfEnabled = Mod::get()->getSavedValue<bool>("macro_cbf", true);
    timeline->setPlayhead(0, 0.0);
    if (cbfModeToggle)
        cbfModeToggle->setOpacity(cbfEnabled ? 255 : 140);

    scheduleUpdate();
    return true;
}

void MacroTimelineLayer::keyBackClicked() {
    restorePauseMenu();
    this->onClose(nullptr);
}

void MacroTimelineLayer::onTasksPressed(CCObject*) {
    if (!timeline)
        return;

    auto* timelinePtr = timeline.get();
    Loader::get()->queueInMainThread([timelinePtr] {
        if (auto* popup = FrameTaskPopup::create(timelinePtr))
            popup->show();
    }); 
}

void MacroTimelineLayer::initInspector() {
    constexpr float width = 180.0f;
    constexpr float height = 264.0f;

    inspectorBg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    inspectorBg->setContentSize({width, height});
    inspectorBg->setAnchorPoint({1.0f, 0.0f});
    inspectorBg->setPosition({CCDirector::sharedDirector()->getWinSize().width / 2.0f - 12.0f, 8.0f});
    inspectorBg->setColor({30, 30, 36});
    inspectorBg->setOpacity(235);
    inspectorBg->setZOrder(25);
    overlay->addChild(inspectorBg);

    auto* title = CCLabelBMFont::create("Event Inspector", "bigFont.fnt");
    title->setScale(0.40f);
    title->setPosition({width / 2.0f, height - 20.0f});
    inspectorBg->addChild(title);

    inspectorMenu = CCMenu::create();
    inspectorMenu->setPosition({0.0f, 0.0f});
    inspectorBg->addChild(inspectorMenu);

    auto addButton = [&](const char* text, float x, float y, SEL_MenuHandler cb) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setScale(0.34f);
        auto* item = CCMenuItemLabel::create(label, this, cb);
        item->setPosition({x, y});
        inspectorMenu->addChild(item);
    };

    addButton("-", 22.0f, 214.0f, menu_selector(MacroTimelineLayer::onFrameDown));
    addButton("+", 158.0f, 214.0f, menu_selector(MacroTimelineLayer::onFrameUp));
    addButton("-", 22.0f, 185.0f, menu_selector(MacroTimelineLayer::onSubframeDown));
    addButton("+", 158.0f, 185.0f, menu_selector(MacroTimelineLayer::onSubframeUp));
    addButton("Button", 90.0f, 151.0f, menu_selector(MacroTimelineLayer::onButtonCycle));
    addButton("Player", 47.0f, 125.0f, menu_selector(MacroTimelineLayer::onPlayerToggle));
    addButton("Action", 133.0f, 125.0f, menu_selector(MacroTimelineLayer::onActionToggle));

    for (int i = 0; i < 8; ++i) {
        inspectorLabels[i] = CCLabelBMFont::create("", "chatFont.fnt");
        inspectorLabels[i]->setScale(0.32f);
        inspectorLabels[i]->setAnchorPoint({0.0f, 0.5f});
        inspectorLabels[i]->setPosition({10.0f, 108.0f - i * 13.0f});
        inspectorBg->addChild(inspectorLabels[i]);
    }
}

void MacroTimelineLayer::initToolbar() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    float width = win.width - 24.0f;

    toolbarMenu = CCMenu::create();
    // overlay is centered on screen, so this menu's symmetric child positions
    // should use the overlay origin directly.
    toolbarMenu->setPosition({0.0f, 202.0f});
    toolbarMenu->setZOrder(100);
    overlay->addChild(toolbarMenu);

    struct Entry {
        char const* text;
        SEL_MenuHandler cb;
        CCMenuItemSpriteExtra** out;
        ButtonSprite* sprite = nullptr;
    };

    CCMenuItemSpriteExtra* undoBtn = nullptr;
    CCMenuItemSpriteExtra* redoBtn = nullptr;
    CCMenuItemSpriteExtra* taskBtn = nullptr;

    std::vector<Entry> entries = {
        { "Play",   menu_selector(MacroTimelineLayer::onPlayPressed),    &playBtn },
        { "Pause",  menu_selector(MacroTimelineLayer::onPausePressed),   &pauseBtn },
        { "Stop",   menu_selector(MacroTimelineLayer::onStopPressed),    &stopBtn },
        { "Step",   menu_selector(MacroTimelineLayer::onStepFramePressed), &stepFrameBtn },
        { "CBF",    menu_selector(MacroTimelineLayer::onCBFTogglePressed), &cbfModeToggle },
        { "Zoom -", menu_selector(MacroTimelineLayer::onZoomOutPressed), &zoomOutBtn },
        { "Zoom +", menu_selector(MacroTimelineLayer::onZoomInPressed),  &zoomInBtn },
        { "Undo",   menu_selector(MacroTimelineLayer::onUndoPressed),    &undoBtn },
        { "Redo",   menu_selector(MacroTimelineLayer::onRedoPressed),    &redoBtn },
        { "Tasks",  menu_selector(MacroTimelineLayer::onTasksPressed),   &taskBtn },
    };

    // Measure at full size, then pick one scale so the whole row fits the overlay.
    constexpr float maxScale = 0.50f;
    constexpr float gap = 6.0f;
    constexpr float margin = 12.0f;

    float natural = 0.0f;
    for (auto& e : entries) {
        e.sprite = ButtonSprite::create(e.text);
        natural += e.sprite->getContentSize().width;
    }

    float available = width - margin * 2.0f - gap * (entries.size() - 1);
    float scale = std::min(maxScale, available / natural);

    float total = natural * scale + gap * (entries.size() - 1);
    float x = -total / 2.0f;

    for (auto& e : entries) {
        e.sprite->setScale(scale);
        auto* item = CCMenuItemSpriteExtra::create(e.sprite, this, e.cb);
        float w = e.sprite->getContentSize().width * scale;
        item->setPositionX(x + w / 2.0f);
        x += w + gap;
        toolbarMenu->addChild(item);
        *e.out = item;
    }

    taskBtn->setID("tasks");
}

void MacroTimelineLayer::initTimeline() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    constexpr float bottom = 42.0f;
    float width = win.width - 24.0f;

    renderState.timelineSize = CCSizeMake(width, 96.0f);
    renderState.rulerHeight = 24.0f;
    renderState.trackHeight = 34.0f;
    renderState.eventHeight = 18.0f;

    timelineLayer = CCLayer::create();
    // CCLayer uses its bottom-left origin, so offset it by half the timeline
    // width to make its content centered under the overlay root.
    timelineLayer->setPosition({-width / 2.0f, bottom});
    timelineLayer->setContentSize(renderState.timelineSize);
    timelineLayer->setZOrder(20);
    overlay->addChild(timelineLayer);

    auto* bg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    bg->setContentSize(renderState.timelineSize);
    bg->setPosition(renderState.timelineSize / 2.0f);
    bg->setColor({0, 0, 0});
    bg->setOpacity(135);
    timelineLayer->addChild(bg);

    rulerLayer = CCLayer::create();
    rulerLayer->setContentSize({width, renderState.rulerHeight});
    rulerLayer->setPosition({0.0f, renderState.timelineSize.height - renderState.rulerHeight});
    timelineLayer->addChild(rulerLayer);

    eventsLayer = CCLayer::create();
    eventsLayer->setContentSize({
        width,
        renderState.timelineSize.height - renderState.rulerHeight
    });
    eventsLayer->setPosition({0.0f, 0.0f});
    timelineLayer->addChild(eventsLayer);

    cursorLayer = CCLayer::create();
    cursorLayer->setContentSize(eventsLayer->getContentSize());
    cursorLayer->setZOrder(10);
    timelineLayer->addChild(cursorLayer);

    frameCounterLabel = CCLabelBMFont::create("Frame: 0", "chatFont.fnt");
    frameCounterLabel->setScale(0.50f);
    frameCounterLabel->setAnchorPoint({0.0f, 0.5f});
    frameCounterLabel->setPosition({-width / 2.0f + 16.0f, -60.0f});
    overlay->addChild(frameCounterLabel);

    subframeLabel = CCLabelBMFont::create("0.00", "chatFont.fnt");
    subframeLabel->setScale(0.50f);
    subframeLabel->setAnchorPoint({0.0f, 0.5f});
    subframeLabel->setPosition({-width / 2.0f + 105.0f, -60.0f});
    overlay->addChild(subframeLabel);

    timelineInfoLabel = CCLabelBMFont::create("", "chatFont.fnt");
    timelineInfoLabel->setScale(0.34f);
    timelineInfoLabel->setAnchorPoint({0.0f, 0.5f});
    timelineInfoLabel->setOpacity(185);
    timelineInfoLabel->setPosition({-width / 2.0f + 18.0f, -39.0f});
    overlay->addChild(timelineInfoLabel);
}

void MacroTimelineLayer::updateTimeline(float dt) {
    if (!timeline) return;

    if (Global::get().state == state::playing) {
        int frame = Global::getCurrentFrame();
        timeline->setPlayhead(frame, 0.0);
    }

    inspector->updateFromTimeline();
    updateFrameCounter();
    updateSubframeCounter();
    renderRuler();
    renderEvents();
    renderPlayhead();
    renderInspector();
    updateInspectorPanel();
    updateScrollBounds();
}

void MacroTimelineLayer::renderEvents() {
    eventsLayer->removeAllChildren();

    // Subtle track lines.
    for (int i = 0; i <= 2; ++i) {
        auto* line = CCDrawNode::create();
        float y = i * renderState.trackHeight;
        line->drawRect(
            CCRectMake(0.0f, y, renderState.timelineSize.width, 1.0f),
            ccc4f(1.0f, 1.0f, 1.0f, i == 0 ? 0.72f : 0.20f),
            0.0f,
            ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
        );
        eventsLayer->addChild(line);
    }

    if (!macro || macro->inputs.empty())
        return;

    const float p1Y = renderState.trackHeight * 0.5f;
    const float p2Y = renderState.trackHeight * 1.5f;

    for (int i = 0; i < static_cast<int>(macro->inputs.size()); ++i) {
        const auto& evt = macro->inputs[i];
        float x = frameToPixels(evt.frame, evt.subframe);
        if (x < 0.0f || x > renderState.timelineSize.width)
            continue;

        float y = evt.player2 ? p2Y : p1Y;
        ccColor3B color = getActionColor(evt.down);
        bool selected = timeline->isEventSelected(i);

        auto* marker = CCDrawNode::create();
        marker->drawRect(
            CCRectMake(
                x - (selected ? 6.0f : 4.0f),
                y - renderState.eventHeight / 2.0f,
                selected ? 12.0f : 8.0f,
                renderState.eventHeight
            ),
            ccc4f(
                static_cast<float>(color.r) / 255.0f,
                static_cast<float>(color.g) / 255.0f,
                static_cast<float>(color.b) / 255.0f,
                selected ? 1.0f : 0.78f
            ),
            0.0f,
            ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
        );
        marker->setZOrder(selected ? 20 : 10);
        eventsLayer->addChild(marker);

        auto* frame = CCLabelBMFont::create(
            std::to_string(evt.frame).c_str(),
            "chatFont.fnt"
        );
        frame->setScale(0.22f);
        frame->setAnchorPoint({0.5f, 1.0f});
        frame->setPosition({x, y - renderState.eventHeight * 0.65f});
        frame->setOpacity(160);
        frame->setZOrder(12);
        eventsLayer->addChild(frame);
    }
}

void MacroTimelineLayer::renderRuler() {
    rulerLayer->removeAllChildren();

    // Draw frame numbers
    int frameStep = 60;  // Draw numbers every 60 frames

    for (int f = renderState.firstVisibleFrame; f <= renderState.lastVisibleFrame; f += frameStep) {
        float x = frameToPixels(f);

        if (x < 0.0f || x > renderState.timelineSize.width) continue;

        auto* tick = CCDrawNode::create();
        tick->drawRect(
            CCRectMake(x - 0.5f, 4.0f, 1.0f, 14.0f),
            ccc4f(0.78f, 0.78f, 0.78f, 0.75f),
            0.0f,
            ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
        );
        rulerLayer->addChild(tick);

        // Draw label
        CCLabelBMFont* label = CCLabelBMFont::create(
            std::to_string(f).c_str(),
            "chatFont.fnt"
        );
        label->setScale(0.3f);
        label->setAnchorPoint({0.5f, 1});
        label->setPosition({x, 20});
        label->setColor({200, 200, 200});
        rulerLayer->addChild(label);
    }
}

void MacroTimelineLayer::renderPlayhead() {
    cursorLayer->removeAllChildren();

    float x = frameToPixels(timeline->getPlayheadFrame(), timeline->getPlayheadSubframe());

    if (x >= 0.0f && x <= renderState.timelineSize.width) {
        auto* cursor = CCDrawNode::create();
        cursor->drawRect(
            CCRectMake(
                x - 0.75f,
                0.0f,
                1.5f,
                renderState.timelineSize.height
            ),
            ccc4f(1.0f, 0.35f, 0.35f, 0.8f),
            0.0f,
            ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
        );
        cursor->setZOrder(100);
        cursorLayer->addChild(cursor);
    }
}

void MacroTimelineLayer::renderInspector() {
    if (!inspectorBg || !inspectorBg->isVisible())
        return;

    if (!inspector->hasData()) {
        for (auto* label : inspectorLabels)
            if (label) label->setVisible(false);
        return;
    }

    for (auto* label : inspectorLabels)
        if (label) label->setVisible(true);
}

void MacroTimelineLayer::updateFrameCounter() {
    int frame = timeline->getPlayheadFrame();
    frameCounterLabel->setString(fmt::format("Frame: {}", frame).c_str());

    if (timelineInfoLabel) {
        timelineInfoLabel->setString(
            fmt::format(
                "{} inputs | {} selected | Left/Right: frame | Up/Down: subframe | CBF {}",
                timeline->getEventCount(),
                timeline->getSelectedEventIndex() >= 0 ? 1 : 0,
                timeline->isCBFModeEnabled() ? "ON" : "OFF"
            ).c_str()
        );

        // Never let the hint line run under the toolbar / off-screen.
        float maxWidth = timelineLayer ? timelineLayer->getContentSize().width - 36.0f : 400.0f;
        float natural = timelineInfoLabel->getContentSize().width;
        if (natural > 0.0f)
            timelineInfoLabel->setScale(std::min(0.34f, maxWidth / natural));
    }
}

void MacroTimelineLayer::updateSubframeCounter() {
    double subframe = timeline->getPlayheadSubframe();
    int percent = static_cast<int>(subframe * 100);
    subframeLabel->setString(fmt::format("{:02d}%", percent).c_str());
}

void MacroTimelineLayer::scheduleUpdate() {
    this->schedule(schedule_selector(MacroTimelineLayer::updateTimeline), 0.016f);  // ~60 FPS
}

// Event handlers
void MacroTimelineLayer::onPlayPressed(CCObject*) {
    if (!timeline || timeline->getEventCount() == 0)
        return;

    auto* pl = PlayLayer::get();
    if (Global::get().state != state::playing)
        Macro::togglePlaying();

    if (Global::get().state == state::playing && pl && pl->m_isPaused)
        pl->pauseGame(false);

    toolbarPlaying = Global::get().state == state::playing;
}

void MacroTimelineLayer::onPausePressed(CCObject*) {
    auto* pl = PlayLayer::get();

    if (Global::get().state == state::playing)
        Macro::togglePlaying();

    if (pl && !pl->m_isPaused)
        pl->pauseGame(true);

    toolbarPlaying = false;
}

void MacroTimelineLayer::onStopPressed(CCObject*) {
    if (Global::get().state == state::playing)
        Macro::togglePlaying();

    Macro::resetState();
    timeline->setPlayhead(0, 0.0);
    toolbarPlaying = false;
}

void MacroTimelineLayer::onStepFramePressed(CCObject*) {
    if (!timeline) return;
    timeline->setPlayhead(timeline->getPlayheadFrame() + 1, timeline->getPlayheadSubframe());
}

void MacroTimelineLayer::onCBFTogglePressed(CCObject*) {
    if (!timeline)
        return;

    bool enabled = Mod::get()->getSavedValue<bool>("macro_cbf", true);
    Mod::get()->setSavedValue("macro_cbf", !enabled);
    timeline->toggleCBFMode();

    if (cbfModeToggle) {
        cbfModeToggle->setOpacity(!enabled ? 255 : 140);
    }
}

void MacroTimelineLayer::onZoomInPressed(CCObject*) {
    timeline->zoom(25);
    renderState.pixelsPerFrame = timeline->getZoomLevel() / 100.0f * 4.0f;
}

void MacroTimelineLayer::onZoomOutPressed(CCObject*) {
    timeline->zoom(-25);
    renderState.pixelsPerFrame = timeline->getZoomLevel() / 100.0f * 4.0f;
}

void MacroTimelineLayer::onUndoPressed(CCObject*) {
    if (timeline && timeline->canUndo())
        timeline->undo();
}

void MacroTimelineLayer::onRedoPressed(CCObject*) {
    if (timeline && timeline->canRedo())
        timeline->redo();
}

// Input handling
bool MacroTimelineLayer::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (!FLAlertLayer::ccTouchBegan(touch, event)) return false;
    if (!timeline) return true;

    const CCPoint eventPos = eventsLayer->convertToNodeSpace(touch->getLocation());
    int eventIdx = hitTestEvent(eventPos);
    if (eventIdx >= 0) {
        timeline->selectEvent(eventIdx);
        inputState.isDragging = true;
        inputState.draggedEventIdx = eventIdx;
        inputState.dragStartX = eventPos.x;
        inputState.dragStartPreciseFrame = timeline->getEvent(eventIdx)->getPreciseFrame();
        return true;
    }

    // Clicking the timeline ruler moves the playhead.
    const CCPoint timelinePos = timelineLayer->convertToNodeSpace(touch->getLocation());
    if (timelinePos.x >= 0.0f && timelinePos.x <= renderState.timelineSize.width &&
        timelinePos.y >= 0.0f && timelinePos.y <= renderState.timelineSize.height) {
        timeline->setPlayheadPrecise(
            (timelinePos.x + timeline->getScrollOffset()) / renderState.pixelsPerFrame
        );
        return true;
    }

    return true;
}

void MacroTimelineLayer::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    FLAlertLayer::ccTouchMoved(touch, event);
    if (!inputState.isDragging || inputState.draggedEventIdx < 0) return;

    const CCPoint pos = eventsLayer->convertToNodeSpace(touch->getLocation());
    double precise = (pos.x + timeline->getScrollOffset()) / renderState.pixelsPerFrame;

    if (precise < 0.0) precise = 0.0;
    timeline->setEventFrame(inputState.draggedEventIdx, static_cast<int>(precise));
    if (timeline->isCBFModeEnabled())
        timeline->setEventSubframe(inputState.draggedEventIdx, precise - std::floor(precise));
}

void MacroTimelineLayer::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    FLAlertLayer::ccTouchEnded(touch, event);
    inputState.isDragging = false;
    inputState.draggedEventIdx = -1;
}

// Helper functions
int MacroTimelineLayer::hitTestEvent(const CCPoint& pos) {
    for (int i = timeline->getEventCount() - 1; i >= 0; --i) {
        CCRect rect = getEventRenderRect(i);
        if (rect.containsPoint(pos)) {
            return i;
        }
    }
    return -1;
}

CCPoint MacroTimelineLayer::getEventRenderPos(int eventIndex) {
    if (eventIndex < 0 || eventIndex >= timeline->getEventCount()) return {0, 0};

    const auto& evt = timeline->getEvent(eventIndex);
    if (!evt) return {0, 0};

    float x = frameToPixels(evt->frame, evt->subframe);
    float y = evt->player2 ? 50.0f : 10.0f;

    return {x, y};
}

CCRect MacroTimelineLayer::getEventRenderRect(int eventIndex) {
    auto pos = getEventRenderPos(eventIndex);
    return CCRectMake(pos.x - 3, pos.y - 8, 6, 16);
}

float MacroTimelineLayer::frameToPixels(int frame, double subframe) const {
    return (frame + subframe) * renderState.pixelsPerFrame - timeline->getScrollOffset();
}

int MacroTimelineLayer::pixelsToFrame(float pixels) const {
    float framePos = (pixels + timeline->getScrollOffset()) / renderState.pixelsPerFrame;
    return static_cast<int>(framePos);
}

std::string MacroTimelineLayer::getButtonName(int button) const {
    const std::string names[] = {"None", "Jump", "Left", "Right"};
    if (button >= 0 && button < 4) return names[button];
    return "?";
}

ccColor3B MacroTimelineLayer::getPlayerColor(bool player2) const {
    return player2 ? ccc3(100, 150, 255) : ccc3(255, 100, 100);
}

ccColor3B MacroTimelineLayer::getActionColor(bool pressed) const {
    return pressed ? ccc3(100, 255, 100) : ccc3(255, 100, 100);
}

void MacroTimelineLayer::jumpToEvent(int eventIndex) {
    const input* evt = timeline->getEvent(eventIndex);
    if (evt) {
        timeline->setPlayhead(evt->frame, evt->subframe);
    }
}

void MacroTimelineLayer::jumpToFrame(int frame) {
    timeline->setPlayhead(frame, 0.0);
}

void MacroTimelineLayer::ensureEventVisible(int eventIndex) {
    if (eventIndex < 0 || eventIndex >= timeline->getEventCount()) return;
    const input* evt = timeline->getEvent(eventIndex);
    if (!evt) return;

    float x = frameToPixels(evt->frame, evt->subframe);
    float padding = 50.0f;

    if (x < padding) {
        timeline->scroll(-(int)(x - padding));
    } else if (x > renderState.timelineSize.width - padding) {
        timeline->scroll((int)(x - renderState.timelineSize.width + padding));
    }
}

void MacroTimelineLayer::updateScrollBounds() {
    // Ensure playhead is visible during playback
    float playheadX = frameToPixels(timeline->getPlayheadFrame(), timeline->getPlayheadSubframe());
    if (playheadX < 0 || playheadX > renderState.timelineSize.width) {
        timeline->setScrollOffset((int)(playheadX - renderState.timelineSize.width / 2));
    }
}

void MacroTimelineLayer::updateInspectorPanel() {
    if (!inspectorBg || !inspectorBg->isVisible())
        return;
    if (!inspector || !inspector->hasData()) return;

    const auto& d = inspector->getDetails();
    const std::string values[] = {
        fmt::format("Frame: {}", d.frame),
        fmt::format("Subframe: {:.2f}", d.subframe),
        fmt::format("Button: {} ({})", d.button, getButtonName(d.button)),
        fmt::format("Player: {}", d.player2 ? "P2" : "P1"),
        fmt::format("Action: {}", d.pressed ? "Press" : "Release"),
        fmt::format("Index: {}", d.index),
        fmt::format("Previous: {}", d.prevEventIndex >= 0 ? fmt::format("{} @ {}", d.prevEventIndex, d.prevEventFrame) : "None"),
        fmt::format("Next: {}", d.nextEventIndex >= 0 ? fmt::format("{} @ {}", d.nextEventIndex, d.nextEventFrame) : "None")
    };

    for (int i = 0; i < 8; ++i)
        inspectorLabels[i]->setString(values[i].c_str());
}

void MacroTimelineLayer::adjustSelectedFrame(int delta) {
    if (!inspector->hasData()) return;
    inspector->setFrame(std::max(0, inspector->getDetails().frame + delta));
}

void MacroTimelineLayer::adjustSelectedSubframe(double delta) {
    if (!inspector->hasData()) return;
    inspector->setSubframe(inspector->getDetails().subframe + delta);
}

void MacroTimelineLayer::cycleSelectedButton() {
    if (!inspector->hasData()) return;
    int button = inspector->getDetails().button;
    inspector->setButton(button >= 3 ? 1 : button + 1);
}

void MacroTimelineLayer::toggleSelectedPlayer() {
    if (!inspector->hasData()) return;
    inspector->setPlayer(!inspector->getDetails().player2);
}

void MacroTimelineLayer::toggleSelectedAction() {
    if (!inspector->hasData()) return;
    inspector->setPressed(!inspector->getDetails().pressed);
}

void MacroTimelineLayer::onFrameDown(CCObject*) { adjustSelectedFrame(-1); }
void MacroTimelineLayer::onFrameUp(CCObject*) { adjustSelectedFrame(1); }
void MacroTimelineLayer::onSubframeDown(CCObject*) { adjustSelectedSubframe(-0.01); }
void MacroTimelineLayer::onSubframeUp(CCObject*) { adjustSelectedSubframe(0.01); }
void MacroTimelineLayer::onButtonCycle(CCObject*) { cycleSelectedButton(); }
void MacroTimelineLayer::onPlayerToggle(CCObject*) { toggleSelectedPlayer(); }
void MacroTimelineLayer::onActionToggle(CCObject*) { toggleSelectedAction(); }
