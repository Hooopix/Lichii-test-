// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QDir>
#include <QLinearGradient>
#include <QPainter>
#include <QShortcut>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTest>
#include <QTreeWidget>

#include "lichii/game/game_card.h"
#include "lichii/game/game_filter_proxy.h"
#include "lichii/glass/glass_command_palette.h"
#include "lichii/glass/glass_game_entry.h"
#include "lichii/glass/glass_home_page.h"
#include "lichii/glass/glass_mods_page.h"
#include "lichii/glass/glass_multiplayer_page.h"
#include "lichii/glass/glass_settings_page.h"
#include "lichii/glass/glass_shell.h"
#include "lichii/glass/glass_tokens.h"
#include "lichii/glass/glass_widgets.h"
#include "lichii/hotkeys.h"
#include "lichii/mod_store/mod_store_client.h"
#include "lichii/mod_store/mod_store_dialog.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"

#include "hid_core/frontend/emulated_controller.h"

namespace PlayTime {
std::string PlayTimeManager::GetReadablePlayTime(PlayTime /*play_time*/) {
    return {};
}
} // namespace PlayTime

namespace {

Core::HID::ControllerUpdateCallback g_test_controller_cb;
Core::HID::NpadButtonState g_test_npad_state{};
Core::HID::CaptureButtonState g_test_capture_state{};
Core::HID::HomeButtonState g_test_home_state{};
int g_test_deleted_callback_key = -1;

} // namespace

namespace Core::HID {
HomeButtonState EmulatedController::GetHomeButtons() const {
    return g_test_home_state;
}
CaptureButtonState EmulatedController::GetCaptureButtons() const {
    return g_test_capture_state;
}
NpadButtonState EmulatedController::GetNpadButtons() const {
    return g_test_npad_state;
}
int EmulatedController::SetCallback(ControllerUpdateCallback update_callback) {
    g_test_controller_cb = std::move(update_callback);
    return 42;
}
void EmulatedController::DeleteCallback(int key) {
    g_test_deleted_callback_key = key;
}
} // namespace Core::HID

namespace {

QPixmap MakeSyntheticCover(const QColor& c1, const QColor& c2, const QString& badge) {
    QPixmap pm(256, 256);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient grad(0, 0, 256, 256);
    grad.setColorAt(0.0, c1);
    grad.setColorAt(1.0, c2);
    p.fillRect(pm.rect(), grad);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 36));
    p.drawEllipse(QPointF(190, 72), 88, 88);
    p.drawEllipse(QPointF(60, 210), 64, 64);

    QFont f;
    f.setBold(true);
    f.setPixelSize(46);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(pm.rect(), Qt::AlignCenter, badge);
    p.end();
    return pm;
}

void ApplyGlassThemeToApp(bool dark, const QString& accent_hex, bool performance_mode = false) {
    UISettings::values.use_glass_shell = true;
    UISettings::values.ui_color_mode = dark ? "dark" : "light";
    UISettings::values.ui_accent_color = accent_hex.toStdString();
    UISettings::values.ui_performance_mode = performance_mode;

    const auto tokens = GlassUi::ActiveTokens();
    qApp->setPalette(GlassUi::BuildQPalette(tokens));
    qApp->setStyleSheet(GlassUi::BuildStyleSheet(tokens));
}

} // namespace

class GlassUiTest final : public QObject {
    Q_OBJECT

private slots:
    void tokensContrastAndStylesheet();
    void gameFilterProxyAndCardLayout();
    void hotkeyRegistryReloadAndControllerLatch();
    void modStoreAutoDetectionAndGrouping();
    void glassShellWidgetsAndVisualCapture();
};

void GlassUiTest::tokensContrastAndStylesheet() {
    QList<QColor> accents;
    for (const auto& preset : GlassUi::AccentPresets) {
        accents.push_back(QColor(QString::fromLatin1(preset.hex)));
    }
    accents.push_back(QColor(QStringLiteral("#ffff00")));
    accents.push_back(QColor(QStringLiteral("#080810")));
    accents.push_back(QColor(QStringLiteral("#00e5ff")));

    for (const bool dark : {true, false}) {
        for (const bool perf : {false, true}) {
            for (const QColor& accent : accents) {
                const auto tokens = GlassUi::ComputeTokens(dark, accent, perf);
                QCOMPARE(tokens.dark, dark);
                QCOMPARE(tokens.performance_mode, perf);
                QVERIFY2(GlassUi::ContrastRatio(tokens.accent, tokens.on_accent) >= 4.5,
                         qPrintable(QStringLiteral("Low contrast for %1").arg(accent.name())));

                const QString qss = GlassUi::BuildStyleSheet(tokens);
                QVERIFY(!qss.isEmpty());
                QVERIFY(qss.contains(QStringLiteral("searchMatch")));
                QVERIFY(qss.contains(accent.name()));

                const QPalette pal = GlassUi::BuildQPalette(tokens);
                QCOMPARE(pal.color(QPalette::Highlight), tokens.accent);
            }
        }
    }

    const auto glass_dark = GlassUi::ComputeTokens(true, QColor(QStringLiteral("#f0386b")), false);
    const auto perf_dark = GlassUi::ComputeTokens(true, QColor(QStringLiteral("#f0386b")), true);
    QVERIFY(glass_dark.plate_bg.alpha() < 255);
    QCOMPARE(perf_dark.plate_bg.alpha(), 255);

    const auto glass_light =
        GlassUi::ComputeTokens(false, QColor(QStringLiteral("#f0386b")), false);
    QVERIFY(glass_dark.page_bg.lightness() < 60);
    QVERIFY(glass_light.page_bg.lightness() > 210);
}

void GlassUiTest::gameFilterProxyAndCardLayout() {
    QStandardItemModel source;
    const QStringList titles{
        QStringLiteral("Alpha Quest"), QStringLiteral("Mario Kart World"),
        QStringLiteral("Star Fox"),    QStringLiteral("Donkey Kong Bananza"),
        QStringLiteral("Mario Tennis Fever"),
    };
    for (int i = 0; i < titles.size(); ++i) {
        auto* item = new GameListItemPath(QStringLiteral("/games/game_%1.nsp").arg(i), {},
                                          titles[i], QStringLiteral("NSP"),
                                          0x0400000000001000ULL + static_cast<u64>(i), 0, {});
        source.appendRow(item);
    }

    GameFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QCOMPARE(proxy.rowCount(), 5);

    proxy.SetFilterText(QStringLiteral("Mario"));
    QCOMPARE(proxy.rowCount(), 2);
    QCOMPARE(proxy.index(0, 0).data(GameListItemPath::TitleRole).toString(),
             QStringLiteral("Mario Kart World"));
    QCOMPARE(proxy.index(1, 0).data(GameListItemPath::TitleRole).toString(),
             QStringLiteral("Mario Tennis Fever"));

    GameCard card;
    card.setSize(QSize(180, 220), QSize(148, 200), 16, 3);
    QStyleOptionViewItem opt0;
    opt0.rect = QRect(0, 0, 180, 220);
    QStyleOptionViewItem opt1;
    opt1.rect = QRect(180, 0, 180, 220);
    const QRect r0 = card.getCardRect(opt0, proxy.index(0, 0));
    const QRect r1 = card.getCardRect(opt1, proxy.index(1, 0));
    QCOMPARE(r0.y(), r1.y());
    QCOMPARE(r0.left(), 12);
    QVERIFY(r1.x() > r0.right());
}

void GlassUiTest::hotkeyRegistryReloadAndControllerLatch() {
    const auto saved_shortcuts = UISettings::values.shortcuts;

    UISettings::values.shortcuts = {
        UISettings::Shortcut{"Fullscreen", "Main Window",
                             UISettings::ContextualShortcut{"F11", "", Qt::WindowShortcut, false}},
    };

    QWidget host;
    HotkeyRegistry registry;
    registry.LoadHotkeys();
    QShortcut* sc = registry.GetHotkey("Main Window", "Fullscreen", &host);
    QVERIFY(sc != nullptr);
    QCOMPARE(sc->key(), QKeySequence(QStringLiteral("F11"), QKeySequence::PortableText));

    // Clear the shortcut in settings and verify LoadHotkeys overwrites the live QShortcut.
    UISettings::values.shortcuts[0].shortcut.keyseq.clear();
    registry.LoadHotkeys();
    QVERIFY(sc->key().isEmpty());

    // Round-trip load + save with PortableText.
    UISettings::values.shortcuts[0].shortcut.keyseq = "Ctrl+Shift+F";
    registry.LoadHotkeys();
    QCOMPARE(sc->key(), QKeySequence(QStringLiteral("Ctrl+Shift+F"), QKeySequence::PortableText));
    UISettings::values.shortcuts.clear();
    registry.SaveHotkeys();
    bool found_saved = false;
    for (const auto& entry : UISettings::values.shortcuts) {
        if (entry.group == "Main Window" && entry.name == "Fullscreen") {
            QCOMPARE(QString::fromStdString(entry.shortcut.keyseq),
                     QStringLiteral("Ctrl+Shift+F"));
            found_saved = true;
        }
    }
    QVERIFY(found_saved);

    // Verify ControllerShortcut held-button combo fires once until released.
    g_test_npad_state = {};
    g_test_capture_state = {};
    g_test_home_state = {};
    g_test_deleted_callback_key = -1;
    {
        auto* fake_controller = reinterpret_cast<Core::HID::EmulatedController*>(0x1000);
        ControllerShortcut ctrl_sc(fake_controller);
        ctrl_sc.SetKey(std::string("L+Plus"));
        int fires = 0;
        QObject::connect(&ctrl_sc, &ControllerShortcut::Activated, [&] { ++fires; });

        // Hold L+Plus across 4 consecutive button update events: must fire only once!
        g_test_npad_state.l.Assign(1);
        g_test_npad_state.plus.Assign(1);
        for (int i = 0; i < 4; ++i) {
            g_test_controller_cb.on_change(Core::HID::ControllerTriggerType::Button);
        }
        QCOMPARE(fires, 1);

        // Release Plus, then press again: must fire a second time.
        g_test_npad_state.plus.Assign(0);
        g_test_controller_cb.on_change(Core::HID::ControllerTriggerType::Button);
        QCOMPARE(fires, 1);

        g_test_npad_state.plus.Assign(1);
        g_test_controller_cb.on_change(Core::HID::ControllerTriggerType::Button);
        QCOMPARE(fires, 2);
    }
    QCOMPARE(g_test_deleted_callback_key, 42);

    UISettings::values.shortcuts = saved_shortcuts;
}

void GlassUiTest::modStoreAutoDetectionAndGrouping() {
    ApplyGlassThemeToApp(true, QStringLiteral("#f0386b"), false);

    QList<ModStore::DetectedGame> detected{
        {QStringLiteral("040074A01BF12000"), QStringLiteral("Donkey Kong Bananza"),
         MakeSyntheticCover(QColor(0xd9, 0x77, 0x06), QColor(0x7c, 0x2d, 0x12),
                            QStringLiteral("DK")),
         1},
        {QStringLiteral("0400C3F00006E000"), QStringLiteral("Mario Kart World"),
         MakeSyntheticCover(QColor(0xe1, 0x1d, 0x48), QColor(0x4c, 0x1d, 0x95),
                            QStringLiteral("MKW")),
         2},
    };

    ModStore::DialogContext ctx{
        .base_url = QStringLiteral("https://example.invalid"),
        .initial_title_id = {},
        .mods_root = QDir::tempPath(),
        .detected_games = detected,
        .auto_load = false,
        .game_identity =
            [](const QString& tid) {
                return ModStore::GameIdentity{.installed = tid == QStringLiteral("0400C3F00006E000"),
                                              .display_version = QStringLiteral("1.1.0"),
                                              .build_id = QString(64, QLatin1Char('A'))};
            },
        .is_enabled = [](const QString&, const QString&) { return true; },
        .set_enabled = [](const QString&, const QString&, bool) { return true; },
    };

    ModStore::Widget widget(std::move(ctx), nullptr, true);
    QVERIFY(widget.IsMyGamesOnly());
    QCOMPARE(widget.DetectedGameCount(), 2);

    widget.SetCatalogGames({
        {QStringLiteral("040074A01BF12000"), QStringLiteral("Donkey Kong Bananza"), 3},
        {QStringLiteral("0400C3F00006E000"), QStringLiteral("Mario Kart World"), 14},
        {QStringLiteral("040077201BE50000"), QStringLiteral("Kirby Air Riders"), 8},
        {QStringLiteral("010019A01E2F2000"), QStringLiteral("Metroid Prime 4: Beyond"), 5},
    });

    QCOMPARE(widget.MatchedCatalogGameCount(), 2);
    // Auto-selects the detected library game with the most matched catalog mods.
    QCOMPARE(widget.SelectedTitleId(), QStringLiteral("0400C3F00006E000"));

    QTreeWidget* tree = widget.GameTree();
    QVERIFY(tree != nullptr);
    QVERIFY(tree->topLevelItemCount() >= 3);
    QVERIFY(tree->topLevelItem(0)->text(0).contains(QStringLiteral("AUTO-DETECTED")));
    QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Mario Kart World"));
    QCOMPARE(tree->topLevelItem(1)->text(1), QStringLiteral("14"));
    QCOMPARE(tree->topLevelItem(2)->text(0), QStringLiteral("Donkey Kong Bananza"));
    QCOMPARE(tree->topLevelItem(2)->text(1), QStringLiteral("3"));

    // Toggle to All Games and confirm unowned catalog games appear in a separate section.
    widget.SetMyGamesOnly(false);
    QVERIFY(!widget.IsMyGamesOnly());
    bool found_all_header = false;
    int mkw_occurrences = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const QString txt = tree->topLevelItem(i)->text(0);
        if (txt.contains(QStringLiteral("ALL CATALOG GAMES"))) {
            found_all_header = true;
        }
        if (txt == QStringLiteral("Mario Kart World")) {
            ++mkw_occurrences;
        }
    }
    QVERIFY(found_all_header);
    QCOMPARE(mkw_occurrences, 1);

    widget.SetSelectedTitleId(QStringLiteral("040074A01BF12000"));
    QCOMPARE(widget.SelectedTitleId(), QStringLiteral("040074A01BF12000"));
}

void GlassUiTest::glassShellWidgetsAndVisualCapture() {
    // 1. Test GlassSwitch instant transition in Performance Mode.
    ApplyGlassThemeToApp(true, QStringLiteral("#f0386b"), true);
    GlassUi::GlassSwitch sw;
    sw.RefreshTokens();
    QCOMPARE(sw.Offset(), 0.0);
    sw.click();
    QVERIFY(sw.isChecked());
    QCOMPARE(sw.Offset(), 1.0);

    // 2. Test GlassSegmentedControl.
    GlassUi::GlassSegmentedControl seg;
    seg.SetSegments({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    int last_idx = -1;
    QObject::connect(&seg, &GlassUi::GlassSegmentedControl::CurrentIndexChanged,
                     [&](int idx) { last_idx = idx; });
    seg.SetCurrentIndex(2);
    QCOMPARE(seg.CurrentIndex(), 2);
    QCOMPARE(last_idx, 2);

    // 3. Test GlassCommandPalette fuzzy filtering & activation.
    ApplyGlassThemeToApp(true, QStringLiteral("#f0386b"), false);
    GlassUi::GlassCommandPalette palette;
    bool triggered = false;
    palette.SetItems({
        {.title = QStringLiteral("Play Mario Kart World"),
         .subtitle = QStringLiteral("0400C3F00006E000"),
         .category = QStringLiteral("Game"),
         .keywords = QStringLiteral("mkw racing"),
         .action = [&] { triggered = true; }},
        {.title = QStringLiteral("Settings · Graphics"),
         .subtitle = QStringLiteral("Vulkan, FSR 4, resolution scale"),
         .category = QStringLiteral("Settings"),
         .keywords = QStringLiteral("vulkan shader"),
         .action = {}},
    });
    palette.SetQuery(QStringLiteral("kart world"));
    QCOMPARE(palette.VisibleResultCount(), 1);
    QVERIFY(palette.ActivateSelected());
    QVERIFY(triggered);

    // 4. Full GlassShell smoke test + optional offscreen PNG renders.
    GlassUi::GlassShell shell;
    shell.resize(1280, 800);

    QList<GlassUi::GameEntry> games{
        {.program_id = 0x0400C3F00006E000ULL,
         .title_id_hex = QStringLiteral("0400C3F00006E000"),
         .title = QStringLiteral("Mario Kart World"),
         .path = QStringLiteral("/games/Mario Kart World.xci"),
         .file_type = QStringLiteral("XCI"),
         .version = QStringLiteral("1.1.0"),
         .play_time = QStringLiteral("14h 22m"),
         .size_text = QStringLiteral("21.8 GiB"),
         .icon = MakeSyntheticCover(QColor(0xe1, 0x1d, 0x48), QColor(0x4c, 0x1d, 0x95),
                                    QStringLiteral("MKW")),
         .is_favorite = true,
         .mod_count = 3},
        {.program_id = 0x040074A01BF12000ULL,
         .title_id_hex = QStringLiteral("040074A01BF12000"),
         .title = QStringLiteral("Donkey Kong Bananza"),
         .path = QStringLiteral("/games/Donkey Kong Bananza.nsp"),
         .file_type = QStringLiteral("NSP"),
         .version = QStringLiteral("1.0.2"),
         .play_time = QStringLiteral("6h 10m"),
         .size_text = QStringLiteral("10.4 GiB"),
         .icon = MakeSyntheticCover(QColor(0xd9, 0x77, 0x06), QColor(0x7c, 0x2d, 0x12),
                                    QStringLiteral("DK")),
         .is_favorite = true,
         .mod_count = 1},
        {.program_id = 0x040077201BE50000ULL,
         .title_id_hex = QStringLiteral("040077201BE50000"),
         .title = QStringLiteral("Kirby Air Riders"),
         .path = QStringLiteral("/games/Kirby Air Riders.nsp"),
         .file_type = QStringLiteral("NSP"),
         .version = QStringLiteral("1.0.0"),
         .play_time = QStringLiteral("2h 45m"),
         .size_text = QStringLiteral("8.2 GiB"),
         .icon = MakeSyntheticCover(QColor(0xec, 0x48, 0x99), QColor(0x02, 0x84, 0xc7),
                                    QStringLiteral("KAR")),
         .is_favorite = false,
         .mod_count = 0},
        {.program_id = 0x010019A01E2F2000ULL,
         .title_id_hex = QStringLiteral("010019A01E2F2000"),
         .title = QStringLiteral("Metroid Prime 4: Beyond"),
         .path = QStringLiteral("/games/Metroid Prime 4 Beyond.xci"),
         .file_type = QStringLiteral("XCI"),
         .version = QStringLiteral("1.0.1"),
         .play_time = QStringLiteral("4h 05m"),
         .size_text = QStringLiteral("18.6 GiB"),
         .icon = MakeSyntheticCover(QColor(0x0f, 0x76, 0x6e), QColor(0x1e, 0x1b, 0x4b),
                                    QStringLiteral("MP4")),
         .is_favorite = false,
         .mod_count = 2},
    };

    const auto addon_loader = [](u64 pid, const QString&) -> QList<GlassUi::InstalledAddon> {
        if (pid == 0x0400C3F00006E000ULL) {
            return {
                {.key = QStringLiteral("60fps-unlock"),
                 .name = QStringLiteral("60 FPS Split-Screen Unlock"),
                 .version = QStringLiteral("v1.1.0"),
                 .category = QStringLiteral("Mod"),
                 .location = QStringLiteral("/tmp/mock-mod-1"),
                 .enabled = true,
                 .toggleable = true},
                {.key = QStringLiteral("ultrawide-21-9"),
                 .name = QStringLiteral("21:9 Ultrawide HUD Fix"),
                 .version = QStringLiteral("v2.0"),
                 .category = QStringLiteral("Mod"),
                 .location = QStringLiteral("/tmp/mock-mod-2"),
                 .enabled = true,
                 .toggleable = true},
                {.key = QStringLiteral("Update@65536"),
                 .name = QStringLiteral("Update"),
                 .version = QStringLiteral("1.1.0"),
                 .category = QStringLiteral("Update"),
                 .location = {},
                 .enabled = true,
                 .toggleable = true,
                 .is_update = true},
                {.key = QStringLiteral("no-dof"),
                 .name = QStringLiteral("Disable Depth of Field"),
                 .version = QStringLiteral("v1.0"),
                 .category = QStringLiteral("Mod"),
                 .location = QStringLiteral("/tmp/mock-mod-3"),
                 .enabled = false,
                 .toggleable = true},
            };
        }
        return {};
    };

    shell.HomePage()->SetAddonLoader(addon_loader);
    shell.HomePage()->SetFooterText(QStringLiteral("Lichii nx2 · Firmware 20.1.0"));
    shell.ModsPage()->SetAddonLoader(addon_loader);
    shell.ModsPage()->SetStoreContextFactory([&](const QString& initial_tid) {
        return ModStore::DialogContext{
            .base_url = QStringLiteral("https://example.invalid"),
            .initial_title_id = initial_tid,
            .mods_root = QDir::tempPath(),
            .detected_games = GlassUi::ToDetectedGames(games),
            .auto_load = false,
            .game_identity =
                [](const QString&) {
                    return ModStore::GameIdentity{.installed = true,
                                                  .display_version = QStringLiteral("1.1.0"),
                                                  .build_id = QString(64, QLatin1Char('B'))};
                },
        };
    });
    shell.SetGames(games);
    shell.show();
    QTest::qWait(40);

    const QString out_dir = qEnvironmentVariable("LICHII_GLASS_SCREENSHOT_DIR");
    const auto save_shot = [&](QWidget& target, const QString& filename) {
        if (out_dir.isEmpty()) {
            return;
        }
        QDir().mkpath(out_dir);
        QTest::qWait(30);
        QVERIFY(target.grab().save(QDir(out_dir).filePath(filename)));
    };

    // Dark + Lichii Rose (default): Play, Mods (Auto-Detect), Mods (Installed), Settings, Palette
    save_shot(shell, QStringLiteral("play_dark_rose.png"));

    shell.SetCurrentPage(GlassUi::GlassShell::Page::Mods);
    QVERIFY(shell.ModsPage()->StoreWidget() != nullptr);
    shell.ModsPage()->StoreWidget()->SetCatalogGames({
        {QStringLiteral("0400C3F00006E000"), QStringLiteral("Mario Kart World"), 14},
        {QStringLiteral("040074A01BF12000"), QStringLiteral("Donkey Kong Bananza"), 6},
        {QStringLiteral("040077201BE50000"), QStringLiteral("Kirby Air Riders"), 4},
        {QStringLiteral("010019A01E2F2000"), QStringLiteral("Metroid Prime 4: Beyond"), 9},
        {QStringLiteral("0400D4201C6EA000"), QStringLiteral("Mario Tennis Fever"), 2},
    });
    save_shot(shell, QStringLiteral("mods_autodetect_dark.png"));

    shell.ModsPage()->SetCurrentSubTab(1);
    save_shot(shell, QStringLiteral("mods_installed_dark.png"));

    shell.SetCurrentPage(GlassUi::GlassShell::Page::Multiplayer);
    QCOMPARE(shell.CurrentPage(), GlassUi::GlassShell::Page::Multiplayer);

    shell.SetCurrentPage(GlassUi::GlassShell::Page::Settings);
    save_shot(shell, QStringLiteral("settings_appearance_dark.png"));

    // Dark + Horizon Violet (#6b4bff)
    ApplyGlassThemeToApp(true, QStringLiteral("#6b4bff"), false);
    shell.RefreshTheme();
    shell.SetCurrentPage(GlassUi::GlassShell::Page::Play);
    save_shot(shell, QStringLiteral("play_dark_violet.png"));

    // Light + Lichii Rose (#f0386b)
    ApplyGlassThemeToApp(false, QStringLiteral("#f0386b"), false);
    shell.RefreshTheme();
    save_shot(shell, QStringLiteral("play_light_rose.png"));

    // Command Palette preview in Dark + Lichii Rose
    ApplyGlassThemeToApp(true, QStringLiteral("#f0386b"), false);
    shell.RefreshTheme();
    const auto tokens = GlassUi::ActiveTokens();
    auto* pal = shell.CommandPalette();
    pal->SetItems({
        {.title = QStringLiteral("Play Mario Kart World"),
         .subtitle = QStringLiteral("0400C3F00006E000 · v1.1.0"),
         .category = QStringLiteral("Game"),
         .icon = QIcon(GlassUi::RoundedPixmap(games[0].icon, 24, 6.0))},
        {.title = QStringLiteral("Play Donkey Kong Bananza"),
         .subtitle = QStringLiteral("040074A01BF12000 · v1.0.2"),
         .category = QStringLiteral("Game"),
         .icon = QIcon(GlassUi::RoundedPixmap(games[1].icon, 24, 6.0))},
        {.title = QStringLiteral("Mods"),
         .subtitle = QStringLiteral("Auto-detecting Mod Store & installed add-ons"),
         .category = QStringLiteral("Navigate"),
         .icon = GlassUi::TintedIcon(QStringLiteral("puzzle"), tokens.accent_soft, 18)},
        {.title = QStringLiteral("Settings · Appearance"),
         .subtitle = QStringLiteral("Glass shell, Dark/Light/Auto, accent color, Performance Mode"),
         .category = QStringLiteral("Settings"),
         .icon = GlassUi::TintedIcon(QStringLiteral("sparkles"), tokens.ink_muted, 18)},
        {.title = QStringLiteral("Open Mod Folder"),
         .subtitle = QStringLiteral("Open in system file manager"),
         .category = QStringLiteral("Folder"),
         .icon = GlassUi::TintedIcon(QStringLiteral("folder"), tokens.ink_muted, 18)},
    });
    pal->OpenCentered();
    save_shot(*pal, QStringLiteral("command_palette_dark.png"));
    pal->hide();
}

QTEST_MAIN(GlassUiTest)
#include "glass_ui_test.moc"
