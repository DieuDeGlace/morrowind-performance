#ifndef MWINPUT_ACTIONMANAGER_H
#define MWINPUT_ACTIONMANAGER_H

#include <functional>
#include <optional>

#include <components/esm3/refnum.hpp>

namespace MWInput
{
    class BindingsManager;

    class ActionManager
    {
    public:
        ActionManager(BindingsManager* bindingsManager, std::function<void()> takeScreenshot);

        void update(float dt);
        void cancelCorpseActivation();

        void executeAction(int action);

        bool checkAllowedToUseItems() const;

        void toggleMainMenu();
        void toggleConsole();
        void screenshot();
        void activate();
        void rest();
        void quickLoad();
        void quickSave();

        void quickKey(int index);

        void resetIdleTime();
        float getIdleTime() const { return mTimeIdle; }

        bool isSneaking() const;

    private:
        void handleGuiArrowKey(int action);
        void updateCorpseActivation(float dt);

        BindingsManager* mBindingsManager;
        std::function<void()> mTakeScreenshot;

        float mTimeIdle;
        std::optional<ESM::RefNum> mCorpseActivation;
        float mActivateHoldTime = 0.f;
        bool mGrabAttempted = false;
    };
}
#endif
