#include "macro_timeline_layer.hpp"

#include <limits>

#include "../hacks/cbf.hpp"


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

    class FrameTaskPopup final : public framebot::Popup<> {
    public:
        static FrameTaskPopup* create(MacroTimeline* timeline) {
            auto ret = new FrameTaskPopup();
            ret->m_timeline = timeline;
            if (ret->initAnchored(520, 290, Utils::getTexture().c_str())) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

    protected:
        bool setup() override {
            this->setTitle("Frame Tasks");

            m_list = CCMenu::create();
            m_list->setPosition({0, 0});
            m_mainLayer->addChild(m_list);

            auto addLabel = [&](const char* text, float x, float y, SEL_MenuHandler callback) {
                auto label = CCLabelBMFont::create(text, "bigFont.fnt");
                label->setScale(0.42f);
                auto item = CCMenuItemLabel::create(label, this, callback);
                item->setPosition({x, y});
                m_list->addChild(item);
            };

            addLabel("Add Selected", 90, 26, menu_selector(FrameTaskPopup::onAddSelected));
            addLabel("Start All", 260, 26, menu_selector(FrameTaskPopup::onStartAll));
            addLabel("Subdivide", 430, 26, menu_selector(FrameTaskPopup::onSubdivide));

            m_status = CCLabelBMFont::create("No tasks", "chatFont.fnt");
            m_status->setScale(0.42f);
            m_status->setPosition({260, 50});
            m_mainLayer->addChild(m_status);

            this->schedule(schedule_selector(FrameTaskPopup::updateTaskRunner), 0.016f);
            refreshList();
            return true;
        }

        void onClose(CCObject* sender) override {
            stopTesting(true);
            Popup::onClose(sender);
        }

        void onAddSelected(CCObject*) {
            if (!m_timeline)
                return;

            int index = m_timeline->getSelectedEventIndex();
            const input* event = m_timeline->getEvent(index);
            if (!event)
                return;

            FrameTask task;
            task.frame = event->frame;
            task.subframe = event->subframe;
            task.button = event->button;
            task.player2 = event->player2;
            task.down = event->down;
            s_tasks.push_back(task);

            refreshList();
            m_status->setString(fmt::format("Added task at {}", formatTime(task.frame, task.subframe)).c_str());
        }

        void onStartTask(CCObject* sender) {
            auto item = static_cast<CCNode*>(sender);
            int index = item->getTag();
            if (index >= 0 && index < static_cast<int>(s_tasks.size())) {
                s_grid = 0;
                startTesting(index, false);
            }
        }

        void generateAllTasks() {
            if (!m_timeline)
                return;

            auto const* events = m_timeline->getEvents();
            if (!events)
                return;

            s_tasks.clear();
            s_tasks.reserve(events->size());

            for (auto const& event : *events) {
                FrameTask task;
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
            s_grid = 0; // back to the menu's Substep Divider
            if (s_tasks.empty())
                generateAllTasks();

            if (s_tasks.empty())
                return;

            startTesting(0, true);
        }

        void refreshList() {
            if (!m_list)
                return;

            m_list->removeAllChildren();

            float y = 248.0f;
            for (int i = 0; i < static_cast<int>(s_tasks.size()); ++i) {
                auto& task = s_tasks[i];

                int successful = 0;
                for (const auto& result : task.results)
                    successful += result.passed ? 1 : 0;

                std::string resultText;
                if (task.finished) {
                    if (task.kind == FrameWindowKind::Impossible)
                        resultText = task.cbfOnly
                            ? fmt::format("  [X] CBF {:.1f}f", task.windowWidth)
                            : "  [X]";
                    else if (task.kind == FrameWindowKind::Disconnected)
                        resultText = fmt::format("  [A-] {} valid", successful);
                    else
                        resultText = fmt::format(
                            "  [{}] {:.1f}f",
                            formatWindowKind(task.kind),
                            task.windowWidth
                        );
                }

                auto text = CCLabelBMFont::create(
                    fmt::format("{}: {}{}", i + 1, formatTime(task.frame, task.subframe), resultText).c_str(),
                    "chatFont.fnt"
                );
                text->setScale(0.38f);
                text->setAnchorPoint({0, 0.5f});
                text->setPosition({45, y});

                auto item = CCMenuItemLabel::create(text, this, menu_selector(FrameTaskPopup::onStartTask));
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
            int const grid = activeGrid();

            for (int offset = -8; offset <= 8; ++offset) {
                m_candidates.push_back({
                    std::max(0, task.frame + offset),
                    0.0,
                    0,
                    0,
                    0.0,
                    false,
                    false
                });
            }

            if (task.subframe > 0.0001 ||
                (m_timeline && m_timeline->isCBFModeEnabled())) {
                for (int offset = -2; offset <= 2; ++offset) {
                    for (int step = 1; step < grid; ++step) {
                        m_candidates.push_back({
                            std::max(0, task.frame + offset),
                            static_cast<double>(step) / grid,
                            0,
                            0,
                            0.0,
                            false,
                            false
                        });
                    }
                }
            }
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
                if (m_probeStage == ProbeStage::Primary) {
                    buildAlternatingProbes(s_tasks[m_taskIndex]);
                    if (!m_candidates.empty()) {
                        m_candidateIndex = 0;
                        beginCandidate();
                        return;
                    }
                }

                if (m_probeStage == ProbeStage::Alternating) {
                    buildPairProbes(s_tasks[m_taskIndex]);
                    if (!m_candidates.empty()) {
                        m_candidateIndex = 0;
                        beginCandidate();
                        return;
                    }
                }

                finalizeTaskWindow(s_tasks[m_taskIndex]);
                s_tasks[m_taskIndex].finished = true;

                if (m_testingAll && m_taskIndex + 1 < static_cast<int>(s_tasks.size())) {
                    ++m_taskIndex;

                    auto& nextTask = s_tasks[m_taskIndex];
                    nextTask.results.clear();
                    nextTask.kind = FrameWindowKind::Normal;
                    nextTask.windowCount = 0;
                    nextTask.windowLow = 0;
                    nextTask.windowHigh = 0;
                    nextTask.cbfOnly = false;
                    nextTask.recoveryMode = false;

                    m_probeStage = ProbeStage::Primary;
                    m_alternatingRuns.clear();
                    m_pairRuns.clear();
                    m_alternatingOffsets.clear();
                    m_recoveryProbe = false;

                    buildCandidates(nextTask);
                    m_candidateIndex = 0;
                    beginCandidate();
                    return;
                }

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

            candidateMacro.inputs[eventIndex].frame = candidate.frame;
            candidateMacro.inputs[eventIndex].subframe = candidate.subframe;

            m_probeContextShift = candidate.contextShift;
            m_probePairedFrame = candidate.pairedFrame;
            m_probePairedSubframe = candidate.pairedSubframe;

            const int originalTaskIndex = findTaskEvent(m_backupMacro, task);

            if (m_probeStage == ProbeStage::Alternating && originalTaskIndex >= 0) {
                const int previous =
                    findAdjacentEvent(m_backupMacro, originalTaskIndex, true);

                if (previous >= 0) {
                    const int livePrevious = findEventNear(
                        candidateMacro,
                        m_backupMacro.inputs[previous],
                        m_backupMacro.inputs[previous].getPreciseFrame()
                    );

                    if (livePrevious >= 0) {
                        candidateMacro.inputs[livePrevious].setPreciseFrame(
                            m_backupMacro.inputs[previous].getPreciseFrame() +
                            m_probeContextShift
                        );
                    }
                }
            }

            if (m_probeStage == ProbeStage::Pair && originalTaskIndex >= 0) {
                const int next =
                    findAdjacentEvent(m_backupMacro, originalTaskIndex, false);

                if (next >= 0) {
                    const int liveNext = findEventNear(
                        candidateMacro,
                        m_backupMacro.inputs[next],
                        m_probePairedFrame
                    );

                    if (liveNext >= 0) {
                        candidateMacro.inputs[liveNext].frame = m_probePairedFrame;
                        candidateMacro.inputs[liveNext].subframe =
                            m_probePairedSubframe;
                    }
                }
            }

            std::sort(candidateMacro.inputs.begin(), candidateMacro.inputs.end());

            const double candidatePrecise =
                candidate.frame + candidate.subframe;

            m_passFrame = -1;
            m_passSubframe = 0.0;

            if (m_probeStage == ProbeStage::Pair) {
                const int next = findAdjacentEvent(
                    candidateMacro,
                    findTaskEvent(candidateMacro, task),
                    false
                );

                if (next >= 0) {
                    const int boundary = findNextSamePlayer(
                        candidateMacro,
                        task.player2,
                        candidateMacro.inputs[next].getPreciseFrame()
                    );

                    if (boundary >= 0) {
                        m_passFrame = candidateMacro.inputs[boundary].frame;
                        m_passSubframe = candidateMacro.inputs[boundary].subframe;
                    }
                }
            }

            if (m_passFrame < 0) {
                for (const auto& input : candidateMacro.inputs) {
                    if (input.player2 == task.player2 &&
                        input.button <= 3 &&
                        input.getPreciseFrame() > candidatePrecise + 0.0001) {
                        m_passFrame = input.frame;
                        m_passSubframe = input.subframe;
                        break;
                    }
                }
            }

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
                    "Testing {} / {}: {}",
                    m_candidateIndex + 1,
                    m_candidates.size(),
                    formatTime(m_targetFrame, m_targetSubframe)
                ).c_str()
            );
        }

        void updateTaskRunner(float) {
            if (!m_testing)
                return;

            PlayLayer* pl = PlayLayer::get();
            if (!pl)
                return finishTesting();

            int frame = Global::getCurrentFrame();

            if (m_attemptStartFrame == 0 && frame > 0)
                m_attemptStartFrame = frame;

            if (!m_targetSeen && frame >= m_targetFrame) {
                PlayerObject* player = s_tasks[m_taskIndex].player2 ? pl->m_player2 : pl->m_player1;
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

            // A window ends at the next macro input. Reaching that input
            // means the player made it through the gap being tested. If this
            // is the last input, use a small fallback lookahead because there
            // is no later input to act as the boundary.
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
            if (m_probeStage == ProbeStage::Primary) {
                task.results.push_back(result);
            } else if (m_probeStage == ProbeStage::Alternating) {
                m_alternatingRuns.push_back({
                    m_probeContextShift,
                    result.frame - task.frame,
                    passed
                });
            } else {
                m_pairRuns.push_back({
                    m_probeContextShift,
                    m_probePairedFrame - m_pairBaseFrame,
                    passed
                });
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
            int integerCount = 0;
            int subframeCount = 0;
            int first = std::numeric_limits<int>::max();
            int last = std::numeric_limits<int>::min();
            int previous = std::numeric_limits<int>::min();
            bool hole = false;

            for (const auto& result : task.results) {
                if (!result.passed)
                    continue;

                if (std::abs(result.subframe) < 0.0001) {
                    ++integerCount;
                    first = std::min(first, result.frame);
                    last = std::max(last, result.frame);

                    if (previous != std::numeric_limits<int>::min() &&
                        result.frame > previous + 1)
                        hole = true;

                    previous = result.frame;
                } else {
                    ++subframeCount;
                }
            }

            // Passing span relative to where the click originally sat. The width counts the
            // last sample too, so one passing whole frame is 1.0f and, on a 1/10 grid, a
            // window from -1.4 to +0.3 is 1.8f.
            {
                double low = std::numeric_limits<double>::max();
                double high = std::numeric_limits<double>::lowest();
                int const grid = activeGrid();
                int tested = 0;
                int passed = 0;

                for (const auto& result : task.results) {
                    ++tested;
                    if (!result.passed)
                        continue;

                    ++passed;
                    double offset =
                        (result.frame + result.subframe) -
                        (task.frame + task.subframe);
                    low = std::min(low, offset);
                    high = std::max(high, offset);
                }

                task.grid = grid;
                task.tested = tested;
                task.passedCount = passed;

                if (passed > 0) {
                    double const step = subframeCount > 0 ? 1.0 / grid : 1.0;
                    task.spanLow = low;
                    task.spanHigh = high;
                    task.windowWidth = (high - low) + step;
                }
                else {
                    task.spanLow = task.spanHigh = task.windowWidth = 0.0;
                }
            }

            task.windowCount = integerCount > 0 ? integerCount : subframeCount;
            task.windowLow =
                first == std::numeric_limits<int>::max() ? 0 : first;
            task.windowHigh =
                last == std::numeric_limits<int>::min() ? 0 : last;

            if (integerCount == 0) {
                task.kind = FrameWindowKind::Impossible;
                task.cbfOnly = subframeCount > 0;
            } else if (hole) {
                task.kind = FrameWindowKind::Disconnected;
            } else if (task.recoveryMode) {
                task.kind = FrameWindowKind::Recovery;
            } else if (hasAlternatingBehavior()) {
                task.kind = FrameWindowKind::Alternating;
            } else {
                bool shared = false;

                if (hasDependentRelationship(shared))
                    task.kind =
                        shared ? FrameWindowKind::Shared : FrameWindowKind::Optimal;
                else
                    task.kind = FrameWindowKind::Normal;
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

            constexpr int markerTag = 0xF7A5;

            auto* markerLayer = pl->getChildByTag(markerTag);
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
            g.macro = m_backupMacro;
            g.state = m_backupState;
            g.currentAction = m_backupCurrentAction;
            g.currentFrameFix = m_backupCurrentFrameFix;
            g.restart = m_backupRestart;
            g.firstAttempt = m_backupFirstAttempt;
            g.respawnFrame = m_backupRespawnFrame;

            m_testing = false;
            cbf::setDividerOverride(0);

            if (m_testingAll) {
                for (auto& task : s_tasks)
                    task.finished = true;
            } else if (m_taskIndex >= 0 && m_taskIndex < static_cast<int>(s_tasks.size())) {
                s_tasks[m_taskIndex].finished = true;
            }

            Macro::updateTPS();

            refreshList();

            int total = 0;
            for (const auto& task : s_tasks)
                for (const auto& result : task.results)
                    total += result.passed ? 1 : 0;

            int const grid = activeGrid();
            std::string note = fmt::format("Finished: {} successful timings (1/{} steps)", total, grid);

            // Sub-frame timings only play back on the grid they were found on.
            if (grid != cbf::substepDivider())
                note += fmt::format(" - set Substep Divider to {} to play them", grid);

            m_status->setString(note.c_str());
        }

        void stopTesting(bool restore) {
            if (!m_testing)
                return;

            if (restore)
                finishTesting();
            else
                m_testing = false;
        }

    private:
        MacroTimeline* m_timeline = nullptr;
        CCMenu* m_list = nullptr;
        CCLabelBMFont* m_status = nullptr;

        bool m_testing = false;
        bool m_testingAll = false;
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

MacroTimelineLayer::~MacroTimelineLayer() {}

bool MacroTimelineLayer::setup(Macro* setupMacro) {
    macro = setupMacro;

    this->setTitle("Macro Timeline");

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

    if (Global::get().state != state::playing)
        Macro::togglePlaying();

    toolbarPlaying = Global::get().state == state::playing;
}

void MacroTimelineLayer::onPausePressed(CCObject*) {
    if (Global::get().state == state::playing)
        Macro::togglePlaying();

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
