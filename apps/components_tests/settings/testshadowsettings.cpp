#include <components/settings/categories/shadows.hpp>
#include <components/settings/parser.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <utility>

#ifndef OPENMW_PROJECT_SOURCE_DIR
#define OPENMW_PROJECT_SOURCE_DIR "."
#endif

namespace
{
    struct ShadowSettingsTest : testing::Test
    {
        void SetUp() override
        {
            Settings::Manager::mDefaultSettings.clear();
            Settings::Manager::mUserSettings.clear();
            Settings::Manager::mChangedSettings.clear();
            Settings::SettingsFileParser parser;
            parser.loadSettingsFile(std::filesystem::path(OPENMW_PROJECT_SOURCE_DIR) / "files/settings-default.cfg",
                Settings::Manager::mDefaultSettings);
        }

        void importDesktopShadows()
        {
            auto& settings = Settings::Manager::mUserSettings;
            settings[{ "Shadows", "number of shadow maps" }] = "4";
            settings[{ "Shadows", "shadow update interval" }] = "2";
            settings[{ "Shadows", "shadow map resolution" }] = "4096";
            settings[{ "Shadows", "normal offset distance" }] = "2.0";
        }
    };

    TEST_F(ShadowSettingsTest, imported_settings_keep_supported_maps_and_reuse)
    {
        importDesktopShadows();
        Settings::Index index;
        Settings::ShadowsCategory shadows(index);
#ifdef ANDROID
        EXPECT_EQ(shadows.mNumberOfShadowMaps.get(), 1);
#else
        EXPECT_EQ(shadows.mNumberOfShadowMaps.get(), 4);
#endif
        EXPECT_EQ(shadows.mShadowUpdateInterval.get(), 2);
        EXPECT_EQ(shadows.mShadowMapResolution.get(), 4096);
        EXPECT_FLOAT_EQ(shadows.mNormalOffsetDistance.get(), 2.f);
    }

    TEST_F(ShadowSettingsTest, loading_user_overrides_after_defaults_keeps_the_backend_limit)
    {
        Settings::Index index;
        Settings::ShadowsCategory defaults(index);
        importDesktopShadows();
        Settings::ShadowsCategory shadows(std::move(defaults));
#ifdef ANDROID
        EXPECT_EQ(shadows.mNumberOfShadowMaps.get(), 1);
#else
        EXPECT_EQ(shadows.mNumberOfShadowMaps.get(), 4);
#endif
        EXPECT_EQ(shadows.mShadowUpdateInterval.get(), 2);
    }

    TEST_F(ShadowSettingsTest, runtime_changes_save_the_effective_map_count)
    {
        importDesktopShadows();
        Settings::Index index;
        Settings::ShadowsCategory shadows(index);
        shadows.mNumberOfShadowMaps.set(8);
#ifdef ANDROID
        EXPECT_EQ(shadows.mNumberOfShadowMaps.get(), 1);
        EXPECT_EQ(Settings::Manager::getInt("number of shadow maps", "Shadows"), 1);
#else
        EXPECT_EQ(shadows.mNumberOfShadowMaps.get(), 8);
        EXPECT_EQ(Settings::Manager::getInt("number of shadow maps", "Shadows"), 8);
#endif
        EXPECT_EQ(shadows.mShadowUpdateInterval.get(), 2);
    }
}
