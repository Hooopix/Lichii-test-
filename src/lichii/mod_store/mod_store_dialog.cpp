// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/mod_store/mod_store_dialog.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include "common/scm_rev.h"
#include "lichii/glass/glass_tokens.h"
#include "lichii/mod_store/mod_store_client.h"
#include "lichii/mod_store/mod_store_install.h"
#include "qt_common/config/uisettings.h"

namespace ModStore {
namespace {

constexpr int PageSize = 50;
constexpr qint64 MaxArchiveBytes = 64 * 1024 * 1024;
constexpr int IdRole = Qt::UserRole;
constexpr int TitleRole = Qt::UserRole + 1;
constexpr int HeaderRole = Qt::UserRole + 2;

enum class Compatibility { Match, Unknown, Mismatch };

QString NormalizeTitleId(const QString& id) {
    return id.trimmed().toUpper();
}

std::optional<qulonglong> BuildOrdinal(const QString& tag) {
    static const QRegularExpression format(QStringLiteral("^build-[1-9][0-9]*$"));
    bool ok = false;
    const auto number = tag.mid(6).toULongLong(&ok);
    if (!ok || !format.match(tag).hasMatch()) {
        return std::nullopt;
    }
    return number;
}

bool KnownBuild(const QString& build) {
    static const QRegularExpression format(QStringLiteral("^[0-9A-Fa-f]{64}$"));
    return format.match(build).hasMatch() &&
           std::ranges::any_of(build, [](QChar digit) { return digit != QLatin1Char('0'); });
}

Compatibility MatchVersion(const QString& selected_title, const GameIdentity& game,
                           const Version& version) {
    if (selected_title.compare(version.title_id, Qt::CaseInsensitive) != 0 ||
        (!game.display_version.isEmpty() && game.display_version != version.game_version) ||
        (KnownBuild(game.build_id) &&
         game.build_id.compare(version.build_id, Qt::CaseInsensitive) != 0)) {
        return Compatibility::Mismatch;
    }
    const auto running = BuildOrdinal(QString::fromLatin1(Common::g_build_release_tag));
    const auto required = BuildOrdinal(version.min_lichii_build);
    if (running && required && *running < *required) {
        return Compatibility::Mismatch;
    }
    return game.installed && !game.display_version.isEmpty() && KnownBuild(game.build_id) &&
                   running && required
               ? Compatibility::Match
               : Compatibility::Unknown;
}

struct Offer {
    std::variant<Version, ExternalFile> file;
    QString version;
    Compatibility compatibility;
    bool supported;
};

QString ExternalVersionPrefix(qint64 id) {
    return QStringLiteral("%1-").arg(id);
}

QString ExternalVersion(qint64 id, const QByteArray& sha256) {
    return ExternalVersionPrefix(id) + QString::fromLatin1(sha256.toHex().left(16));
}

QLabel* PlainLabel(QWidget* parent, bool wrap = false) {
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(wrap);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QIcon MakeDetectedGameIcon(const QPixmap& source, const QColor& live_color) {
    constexpr int px = 48;
    QImage canvas(px, px, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!source.isNull()) {
        const QPixmap rounded = GlassUi::RoundedPixmap(source, 22, 5.0);
        p.drawPixmap(QRect(2, 2, 44, 44), rounded);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x28, 0x28, 0x34));
        p.drawRoundedRect(QRectF(2, 2, 44, 44), 10.0, 10.0);
    }
    // Small green detected indicator dot in bottom-right corner.
    p.setPen(QPen(QColor(0x10, 0x10, 0x16), 2.5));
    p.setBrush(live_color);
    p.drawEllipse(QRectF(33, 33, 13, 13));
    p.end();

    QPixmap pm = QPixmap::fromImage(canvas);
    pm.setDevicePixelRatio(2.0);
    return QIcon(pm);
}

QString BuildPrimaryButtonStyle(const GlassUi::PaletteTokens& tokens, bool use_glass) {
    const QColor accent = use_glass ? tokens.accent
                                    : (tokens.dark ? QColor(QStringLiteral("#f0386b"))
                                                   : QColor(QStringLiteral("#d52c5b")));
    const QColor hover = use_glass ? tokens.accent_hover
                                   : (tokens.dark ? QColor(QStringLiteral("#f45a85"))
                                                  : QColor(QStringLiteral("#bc1f4b")));
    const QColor pressed = use_glass ? tokens.accent_pressed
                                     : (tokens.dark ? QColor(QStringLiteral("#d42c5a"))
                                                    : QColor(QStringLiteral("#a61942")));
    const QColor fg = use_glass ? tokens.on_accent : QColor(Qt::white);
    return QStringLiteral(
               "QPushButton { background: %1; border: 1px solid %1; border-radius: 8px;"
               " color: %2; font-weight: 600; padding: 0 12px; min-height: 30px; }"
               "QPushButton:hover { background: %3; border-color: %3; }"
               "QPushButton:pressed { background: %4; border-color: %4; }"
               "QPushButton:focus { border-color: %5; }")
        .arg(accent.name(), fg.name(), hover.name(), pressed.name(),
             tokens.dark ? QStringLiteral("white") : QStringLiteral("palette(window-text)"));
}

} // namespace

struct Widget::Impl {
    Q_DECLARE_TR_FUNCTIONS(ModStore::Widget)

public:
    Impl(Widget* owner_, DialogContext ctx, bool embedded_)
        : owner(owner_), context(std::move(ctx)), client(context.base_url, owner_),
          embedded(embedded_) {
        row_height = qMax(30, owner->fontMetrics().height() + 12);

        auto* root = new QVBoxLayout(owner);
        root->setContentsMargins(embedded ? 0 : 12, embedded ? 0 : 12, embedded ? 0 : 12,
                                 embedded ? 0 : 12);
        root->setSpacing(8);

        // Auto-detection summary bar + My Games / All Games switcher.
        auto* detect_row = new QHBoxLayout;
        detect_row->setSpacing(8);
        detect_banner = PlainLabel(owner, false);
        detect_banner->setObjectName(QStringLiteral("modStoreDetectBanner"));
        my_games_btn = new QPushButton(tr("My Games"), owner);
        my_games_btn->setCheckable(true);
        my_games_btn->setCursor(Qt::PointingHandCursor);
        all_catalog_btn = new QPushButton(tr("All Games"), owner);
        all_catalog_btn->setCheckable(true);
        all_catalog_btn->setCursor(Qt::PointingHandCursor);
        detect_row->addWidget(detect_banner, 1);
        detect_row->addWidget(my_games_btn);
        detect_row->addWidget(all_catalog_btn);
        root->addLayout(detect_row);

        auto* games_row = new QHBoxLayout;
        auto* toolbar = new QHBoxLayout;
        game_picker = new QComboBox(owner);
        game_picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        game_picker->setMinimumContentsLength(22);
        refresh = new QPushButton(tr("Refresh"), owner);
        all_games = new QPushButton(tr("Show all"), owner);
        installed_mode = new QPushButton(tr("Store Installed"), owner);
        installed_mode->setCheckable(true);
        if (embedded) {
            detect_row->addWidget(refresh);
            detect_row->addWidget(installed_mode);
            game_picker->hide();
            all_games->hide();
        } else {
            games_row->addWidget(new QLabel(tr("Game:"), owner));
            games_row->addWidget(game_picker, 1);
            games_row->addWidget(all_games);
            games_row->addWidget(refresh);
            games_row->addWidget(installed_mode);
        }

        search = new QLineEdit(owner);
        search->setPlaceholderText(tr("Search mods by name, author, or description..."));
        search->setClearButtonEnabled(true);
        search->setMaxLength(128);
        type = new QComboBox(owner);
        type->addItem(tr("All types"));
        for (const auto category : {ModType::Graphics, ModType::Fps, ModType::Cheats,
                                    ModType::Translation, ModType::Gameplay}) {
            type->addItem(TypeName(category), static_cast<int>(category));
        }
        sort = new QComboBox(owner);
        sort->addItem(tr("Recently updated"), static_cast<int>(Sort::Updated));
        sort->addItem(tr("Name A–Z"), static_cast<int>(Sort::Name));
        toolbar->addWidget(new QLabel(tr("Filter:"), owner));
        toolbar->addWidget(search, 1);
        toolbar->addWidget(new QLabel(tr("Type:"), owner));
        toolbar->addWidget(type);
        toolbar->addWidget(new QLabel(tr("Sort by:"), owner));
        toolbar->addWidget(sort);
        if (!embedded) {
            root->addLayout(games_row);
        }
        root->addLayout(toolbar);

        auto* split = new QSplitter(owner);
        game_tree = new QTreeWidget(split);
        game_tree->setHeaderLabels({tr("Games"), tr("Mods")});
        game_tree->setRootIsDecorated(false);
        game_tree->setUniformRowHeights(true);
        game_tree->setIconSize(QSize(24, 24));
        game_tree->header()->setMinimumHeight(row_height);
        game_tree->header()->setStretchLastSection(false);
        game_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        game_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

        auto* middle = new QWidget(split);
        auto* middle_layout = new QVBoxLayout(middle);
        middle_layout->setContentsMargins(0, 0, 0, 0);
        results = new QTreeWidget(middle);
        results->setHeaderLabels({tr("Mod name"), tr("Type")});
        results->setRootIsDecorated(false);
        results->setUniformRowHeights(true);
        results->setAlternatingRowColors(true);
        results->header()->setMinimumHeight(row_height);
        results->header()->setStretchLastSection(false);
        results->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        results->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        more = new QPushButton(tr("Load more"), middle);
        more->hide();
        middle_layout->addWidget(results, 1);
        middle_layout->addWidget(more);

        inspector_frame = new QFrame(split);
        inspector_frame->setObjectName(QStringLiteral("modStoreInspector"));
        auto* inspector_layout = new QVBoxLayout(inspector_frame);
        inspector_layout->setContentsMargins(1, 1, 1, 1);
        inspector_layout->setSpacing(0);
        inspector = new QScrollArea(inspector_frame);
        inspector->setFrameShape(QFrame::NoFrame);
        inspector->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        inspector->setWidgetResizable(true);
        inspector_layout->addWidget(inspector, 1);
        auto* pane = new QWidget(inspector);
        auto* detail = new QVBoxLayout(pane);
        detail->setContentsMargins(10, 10, 10, 10);
        detail->setSpacing(6);
        name = PlainLabel(pane, true);
        QFont heading = name->font();
        heading.setBold(true);
        heading.setPointSizeF(heading.pointSizeF() + 2);
        name->setFont(heading);
        byline = PlainLabel(pane, true);
        description = PlainLabel(pane, true);
        description_toggle = new QToolButton(pane);
        description_toggle->setCheckable(true);
        description_toggle->setAutoRaise(true);
        preview = PlainLabel(pane);
        preview->setAlignment(Qt::AlignCenter);
        preview->setFixedHeight(172);
        preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        preview->setToolTip(tr("Mod preview"));
        preview_select = new QComboBox(pane);
        preview_select->hide();
        detail->addWidget(name);
        detail->addWidget(byline);
        detail->addWidget(description);
        detail->addWidget(description_toggle, 0, Qt::AlignLeft);
        target_picker = new QComboBox(pane);
        target_picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        target_picker->setMinimumContentsLength(20);
        detail->addWidget(target_picker);
        release = new QComboBox(pane);
        release->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        release->setMinimumContentsLength(20);
        auto* media_options = new QHBoxLayout;
        media_options->addWidget(release, 1);
        media_options->addWidget(preview_select);
        detail->addLayout(media_options);
        detail->addWidget(preview);

        compatibility_card = new QFrame(pane);
        compatibility_card->setObjectName(QStringLiteral("modStoreCompatibility"));
        auto* compatibility_layout = new QGridLayout(compatibility_card);
        compatibility_layout->setContentsMargins(9, 8, 9, 8);
        compatibility_layout->setHorizontalSpacing(12);
        compatibility_layout->setVerticalSpacing(4);
        compatibility_layout->setColumnStretch(1, 1);
        auto* caption = PlainLabel(compatibility_card);
        caption->setText(tr("Compatibility"));
        auto caption_font = caption->font();
        caption_font.setBold(true);
        caption->setFont(caption_font);
        compatibility_state = PlainLabel(compatibility_card);
        compatibility_state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        compatibility_layout->addWidget(caption, 0, 0);
        compatibility_layout->addWidget(compatibility_state, 0, 1);
        const auto add_divider = [&](int row) {
            auto* rule = new QFrame(compatibility_card);
            rule->setFixedHeight(1);
            compatibility_dividers.push_back(rule);
            compatibility_layout->addWidget(rule, row, 0, 1, 2);
        };
        add_divider(1);
        for (size_t i = 0; i < compatibility_rows.size(); ++i) {
            auto& [key, value] = compatibility_rows[i];
            key = PlainLabel(compatibility_card, true);
            value = PlainLabel(compatibility_card, true);
            const auto row = static_cast<int>(i) + (i < 5 ? 2 : 3);
            compatibility_layout->addWidget(key, row, 0, Qt::AlignTop);
            compatibility_layout->addWidget(value, row, 1, Qt::AlignTop);
        }
        add_divider(7);
        catalog_note = PlainLabel(compatibility_card, true);
        catalog_note->setText(tr("Not verified by the catalog"));
        compatibility_layout->addWidget(catalog_note, 11, 0, 1, 2);
        detail->addWidget(compatibility_card);

        detail->addStretch();
        inspector->setWidget(pane);

        auto* actions = new QWidget(inspector_frame);
        auto* actions_layout = new QVBoxLayout(actions);
        actions_layout->setContentsMargins(10, 4, 10, 10);
        actions_layout->setSpacing(6);
        inspector_layout->addWidget(actions);
        hint = PlainLabel(actions, true);
        mod_actions = new QHBoxLayout;
        install = new QPushButton(tr("Install"), actions);
        source = new QPushButton(tr("View on GameBanana"), actions);
        mod_actions->addWidget(install);
        mod_actions->addWidget(source);
        mod_actions->addStretch();
        actions_layout->addLayout(mod_actions);
        auto* installed_actions = new QHBoxLayout;
        enable = new QPushButton(actions);
        uninstall = new QPushButton(tr("Uninstall"), actions);
        installed_actions->addWidget(enable);
        installed_actions->addWidget(uninstall);
        installed_actions->addStretch();
        actions_layout->addLayout(installed_actions);
        actions_layout->addWidget(hint);

        split->setChildrenCollapsible(false);
        split->setSizes({276, 314, 460});
        split->setStretchFactor(2, 1);
        root->addWidget(split, 1);

        auto* footer = new QHBoxLayout;
        status = PlainLabel(owner, true);
        close_button = new QPushButton(tr("Close"), owner);
        close_button->setVisible(!embedded);
        footer->addWidget(status, 1);
        footer->addWidget(close_button);
        root->addLayout(footer);

        progress = new QDialog(owner);
        progress->setWindowTitle(tr("Installing mod"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumWidth(360);
        auto* progress_layout = new QVBoxLayout(progress);
        progress_label = PlainLabel(progress, true);
        progress_catalog_note = PlainLabel(progress, true);
        progress_catalog_note->setText(catalog_note->text());
        progress_bar = new QProgressBar(progress);
        cancel_button = new QPushButton(tr("Cancel"), progress);
        progress_layout->addWidget(progress_label);
        progress_layout->addWidget(progress_catalog_note);
        progress_layout->addWidget(progress_bar);
        progress_layout->addWidget(cancel_button, 0, Qt::AlignRight);

        for (auto* button : owner->findChildren<QPushButton*>()) {
            button->setAutoDefault(false);
        }

        connect(cancel_button, &QPushButton::clicked, owner, [this] { CancelDownload(); });
        connect(progress, &QDialog::rejected, owner, [this] { CancelDownload(); });
        debounce.setSingleShot(true);
        debounce.setInterval(240);
        connect(&debounce, &QTimer::timeout, owner, [this] { ReloadMods(); });
        connect(search, &QLineEdit::textChanged, owner, [this] {
            more->hide();
            debounce.start();
        });
        const auto filters_changed = [this] {
            debounce.stop();
            ReloadMods();
        };
        connect(type, &QComboBox::currentIndexChanged, owner, filters_changed);
        connect(sort, &QComboBox::currentIndexChanged, owner, filters_changed);
        connect(refresh, &QPushButton::clicked, owner, [this] { RefreshAll(); });
        connect(my_games_btn, &QPushButton::clicked, owner, [this] { SetMyGamesOnly(true); });
        connect(all_catalog_btn, &QPushButton::clicked, owner, [this] { SetMyGamesOnly(false); });
        connect(all_games, &QPushButton::clicked, owner, [this] {
            user_selected_game = true;
            SetMyGamesOnly(false);
            SelectGame({}, true);
        });
        connect(installed_mode, &QPushButton::toggled, owner, [this](bool installed) {
            type->setEnabled(!installed);
            sort->setEnabled(!installed);
            ReloadMods();
        });
        connect(game_picker, &QComboBox::currentIndexChanged, owner, [this] {
            user_selected_game = true;
            SelectGame(game_picker->currentData().toString(), true);
        });
        connect(game_tree, &QTreeWidget::currentItemChanged, owner,
                [this](QTreeWidgetItem* item) {
                    if (item && !item->data(0, HeaderRole).toBool()) {
                        user_selected_game = true;
                        SelectGame(item->data(0, IdRole).toString(), true);
                    }
                });
        connect(results, &QTreeWidget::currentItemChanged, owner,
                [this](QTreeWidgetItem* item) { SelectMod(item); });
        connect(more, &QPushButton::clicked, owner, [this] {
            if (next && !debounce.isActive() && !reload_after_install) {
                FetchMods(*next, list_epoch);
            }
        });
        connect(release, &QComboBox::currentIndexChanged, owner, [this] { UpdateOffer(); });
        connect(target_picker, &QComboBox::currentIndexChanged, owner, [this] {
            local_title = target_picker->currentData().toString();
            ReadLocal();
            BuildOffers();
        });
        connect(preview_select, &QComboBox::currentIndexChanged, owner, [this] { LoadPreview(); });
        connect(description_toggle, &QToolButton::toggled, owner, [this](bool expanded) {
            if (selected) {
                description_toggle->setText(expanded ? tr("Show less")
                                                     : tr("Show full description"));
                description->setText(expanded ? selected->description : description_excerpt);
            }
        });
        connect(source, &QPushButton::clicked, owner, [this] { OpenSource(); });
        connect(install, &QPushButton::clicked, owner, [this] { StartInstall(); });
        connect(enable, &QPushButton::clicked, owner, [this] { ToggleEnabled(); });
        connect(uninstall, &QPushButton::clicked, owner, [this] { RemoveInstalled(); });
        connect(close_button, &QPushButton::clicked, owner, [this] { emit owner->CloseRequested(); });

        title = NormalizeTitleId(context.initial_title_id);
        if (!title.isEmpty()) {
            user_selected_game = true;
        }
        RebuildDetectedIndex(std::move(context.detected_games));
        my_games_only = !detected_games.isEmpty();
        if (title.isEmpty() && !detected_games.isEmpty()) {
            title = detected_games.first().title_id;
        }

        RefreshTheme();
        PopulateGames();
        ClearDetail();
        if (context.auto_load) {
            QTimer::singleShot(0, owner, [this] {
                LoadGames();
                ReloadMods();
            });
        }
    }

    static QString TypeName(ModType value) {
        switch (value) {
        case ModType::Graphics:
            return tr("Graphics");
        case ModType::Fps:
            return tr("FPS");
        case ModType::Cheats:
            return tr("Cheats");
        case ModType::Translation:
            return tr("Translation");
        case ModType::Gameplay:
            return tr("Gameplay");
        }
        return {};
    }

    static QString CompatibilityName(Compatibility value) {
        switch (value) {
        case Compatibility::Match:
            return tr("Match");
        case Compatibility::Unknown:
            return tr("Unverified");
        case Compatibility::Mismatch:
            return tr("Mismatch");
        }
        return {};
    }

    void RebuildDetectedIndex(QList<DetectedGame> incoming) {
        detected_games.clear();
        detected_by_id.clear();
        for (auto& g : incoming) {
            g.title_id = NormalizeTitleId(g.title_id);
            if (g.title_id.isEmpty() || detected_by_id.contains(g.title_id)) {
                continue;
            }
            detected_by_id.insert(g.title_id, g);
            detected_games.push_back(std::move(g));
        }
    }

    void RefreshTheme() {
        tokens = GlassUi::ActiveTokens();
        use_glass = UISettings::IsGlassShellEnabled();
        dark = use_glass ? tokens.dark : owner->palette().color(QPalette::Window).lightness() < 128;

        const QString border = use_glass ? GlassUi::RgbaCss(tokens.rule)
                                         : (dark ? QStringLiteral("#2a2b30")
                                                 : QStringLiteral("#cbd0d6"));
        const QString card_bg = use_glass ? GlassUi::RgbaCss(tokens.plate_bg)
                                          : (dark ? QStringLiteral("#1b1c20")
                                                  : QStringLiteral("#f7f7f8"));
        const QString muted_css = use_glass ? GlassUi::RgbaCss(tokens.ink_muted)
                                            : (dark ? QStringLiteral("#9b9da4")
                                                    : QStringLiteral("#555c67"));

        const QString tree_style =
            QStringLiteral("QTreeWidget { border: 1px solid %1; border-radius: %2px; }")
                .arg(border)
                .arg(use_glass ? 12 : 4);
        game_tree->setStyleSheet(tree_style);
        results->setStyleSheet(tree_style);
        inspector_frame->setStyleSheet(
            QStringLiteral("QFrame#modStoreInspector { border: 1px solid %1; border-radius: %2px; }")
                .arg(border)
                .arg(use_glass ? 12 : 4));
        compatibility_card->setStyleSheet(
            QStringLiteral("QFrame#modStoreCompatibility { background: %1; border: 1px solid %2;"
                           " border-radius: 8px; }")
                .arg(card_bg, border));
        for (auto* rule : compatibility_dividers) {
            rule->setStyleSheet(QStringLiteral("background: %1;").arg(border));
        }
        for (auto& [key, value] : compatibility_rows) {
            key->setStyleSheet(QStringLiteral("color: %1;").arg(muted_css));
        }

        UpdateFilterToggleStyles();
        if (selected) {
            UpdateOffer();
        } else {
            UpdateButtons();
        }
        PopulateGames();
    }

    void UpdateFilterToggleStyles() {
        const QString active_style = BuildPrimaryButtonStyle(tokens, use_glass);
        my_games_btn->setChecked(my_games_only);
        all_catalog_btn->setChecked(!my_games_only);
        my_games_btn->setEnabled(!detected_games.isEmpty());
        my_games_btn->setStyleSheet(my_games_only && !detected_games.isEmpty() ? active_style
                                                                               : QString{});
        all_catalog_btn->setStyleSheet(!my_games_only || detected_games.isEmpty() ? active_style
                                                                                  : QString{});
        my_games_btn->setText(detected_games.isEmpty()
                                  ? tr("My Games")
                                  : tr("My Games (%1)").arg(detected_games.size()));

        const int matched = MatchedCatalogGameCount();
        if (detected_games.isEmpty()) {
            detect_banner->setText(
                tr("Add a game folder to auto-detect compatible mods for your library."));
            detect_banner->setStyleSheet(
                QStringLiteral("color: %1;").arg(GlassUi::RgbaCss(tokens.ink_muted)));
        } else if (matched > 0) {
            detect_banner->setText(
                tr("● Auto-detected %1 library game(s) — %2 matched in Mod Store")
                    .arg(detected_games.size())
                    .arg(matched));
            detect_banner->setStyleSheet(
                QStringLiteral("color: %1; font-weight: 600;").arg(tokens.live.name()));
        } else {
            detect_banner->setText(
                tr("● Auto-detected %1 library game(s)").arg(detected_games.size()));
            detect_banner->setStyleSheet(
                QStringLiteral("color: %1; font-weight: 600;").arg(tokens.live.name()));
        }
    }

    int MatchedCatalogGameCount() const {
        int matched = 0;
        for (const auto& game : games) {
            if (detected_by_id.contains(NormalizeTitleId(game.title_id)) && game.mod_count > 0) {
                ++matched;
            }
        }
        return matched;
    }

    void SetDetectedGames(QList<DetectedGame> incoming) {
        const bool had_detected = !detected_games.isEmpty();
        RebuildDetectedIndex(std::move(incoming));
        if (!had_detected && !detected_games.isEmpty() && !user_selected_game) {
            my_games_only = true;
            AutoSelectBestDetectedGame();
        } else if (detected_games.isEmpty()) {
            my_games_only = false;
        }
        PopulateGames();
        UpdateFilterToggleStyles();
    }

    void SetMyGamesOnly(bool value) {
        const bool next_mode = value && !detected_games.isEmpty();
        if (my_games_only == next_mode) {
            UpdateFilterToggleStyles();
            return;
        }
        my_games_only = next_mode;
        if (my_games_only && !detected_by_id.contains(title) && !detected_games.isEmpty()) {
            AutoSelectBestDetectedGame();
        }
        PopulateGames();
        UpdateFilterToggleStyles();
        if (context.auto_load) {
            ReloadMods();
        }
    }

    void AutoSelectBestDetectedGame() {
        QHash<QString, int> catalog_counts;
        for (const auto& g : games) {
            catalog_counts.insert(NormalizeTitleId(g.title_id), g.mod_count);
        }
        QString best;
        int best_count = -1;
        for (const auto& det : detected_games) {
            const int count = catalog_counts.value(det.title_id, 0);
            if (best.isEmpty() || count > best_count) {
                best = det.title_id;
                best_count = count;
            }
        }
        if (!best.isEmpty() && best != title) {
            title = best;
            emit owner->SelectedGameChanged(title);
        }
    }

    void Status(const QString& text) {
        status->setText(text);
    }

    GameIdentity Identity(const QString& title_id) {
        const QString normalized = NormalizeTitleId(title_id);
        const auto found = identities.constFind(normalized);
        if (found != identities.cend()) {
            return found.value();
        }
        const auto identity = !normalized.isEmpty() && context.game_identity
                                  ? context.game_identity(normalized)
                                  : GameIdentity{};
        identities.insert(normalized, identity);
        return identity;
    }

    void PopulateGames() {
        const QSignalBlocker block_picker(game_picker);
        const QSignalBlocker block_tree(game_tree);
        game_picker->clear();
        game_tree->clear();

        QHash<QString, Game> catalog_by_id;
        for (const auto& g : games) {
            catalog_by_id.insert(NormalizeTitleId(g.title_id), g);
        }

        const auto add_section_header = [&](const QString& label, const QString& badge) {
            auto* header = new QTreeWidgetItem(game_tree, {label, badge});
            header->setFlags(Qt::ItemIsEnabled);
            header->setData(0, HeaderRole, true);
            QFont f = header->font(0);
            f.setBold(true);
            f.setPointSizeF(std::max<qreal>(8.0, f.pointSizeF() - 1.0));
            header->setFont(0, f);
            header->setFont(1, f);
            header->setForeground(0, tokens.ink_faint);
            header->setForeground(1, tokens.ink_faint);
            header->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            header->setSizeHint(0, QSize(0, qMax(24, row_height - 4)));
        };

        const auto append_row = [&](const QString& id, const QString& label, const QString& count,
                                    const QIcon& icon, const QString& tooltip) {
            game_picker->addItem(icon, label, id);
            auto* node = new QTreeWidgetItem(game_tree, {label, count});
            node->setSizeHint(0, QSize(0, row_height));
            node->setData(0, IdRole, id);
            node->setData(0, HeaderRole, false);
            node->setToolTip(0, tooltip.isEmpty() ? label : tooltip);
            node->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            if (!icon.isNull()) {
                node->setIcon(0, icon);
            }
            if (id == title) {
                game_tree->setCurrentItem(node);
                game_picker->setCurrentIndex(game_picker->count() - 1);
            }
        };

        if (!my_games_only) {
            append_row({}, tr("All games"), {}, {}, tr("Browse mods for all games"));
        }

        // 1. Pinned My Games (Auto-Detected) section.
        if (!detected_games.isEmpty()) {
            QList<DetectedGame> sorted = detected_games;
            std::ranges::sort(sorted, [&](const DetectedGame& a, const DetectedGame& b) {
                const int ca = catalog_by_id.contains(a.title_id) ? catalog_by_id[a.title_id].mod_count
                                                                  : 0;
                const int cb = catalog_by_id.contains(b.title_id) ? catalog_by_id[b.title_id].mod_count
                                                                  : 0;
                if ((ca > 0) != (cb > 0)) {
                    return ca > cb;
                }
                if (ca != cb) {
                    return ca > cb;
                }
                return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });

            add_section_header(tr("MY GAMES (AUTO-DETECTED)"), QString::number(sorted.size()));
            for (const auto& det : sorted) {
                const auto cat_it = catalog_by_id.constFind(det.title_id);
                const int count = cat_it != catalog_by_id.cend() ? cat_it->mod_count : 0;
                const QString display_name =
                    !det.name.isEmpty()
                        ? det.name
                        : (cat_it != catalog_by_id.cend() ? cat_it->name : det.title_id);
                const QString count_text = count > 0 ? QString::number(count)
                                                     : (det.installed_mods > 0
                                                            ? tr("%1 local").arg(det.installed_mods)
                                                            : QStringLiteral("0"));
                const QString tooltip =
                    tr("%1\nTitle ID: %2\nDetected in your library · %3 catalog mod(s)")
                        .arg(display_name, det.title_id)
                        .arg(count);
                append_row(det.title_id, display_name, count_text,
                           MakeDetectedGameIcon(det.icon, tokens.live), tooltip);
            }
        }

        // 2. Remaining Catalog Games (shown in All Games mode or when no library games exist).
        if (!my_games_only || detected_games.isEmpty()) {
            QList<Game> other_games;
            for (const auto& game : games) {
                if (!detected_by_id.contains(NormalizeTitleId(game.title_id))) {
                    other_games.push_back(game);
                }
            }
            if (!other_games.isEmpty()) {
                if (!detected_games.isEmpty()) {
                    add_section_header(tr("ALL CATALOG GAMES"),
                                       QString::number(other_games.size()));
                }
                for (const auto& game : other_games) {
                    const QString nid = NormalizeTitleId(game.title_id);
                    append_row(nid, game.name, QString::number(game.mod_count), {},
                               QStringLiteral("%1 (%2)").arg(game.name, nid));
                }
            }
        }

        if (!title.isEmpty() && game_picker->findData(title) < 0) {
            append_row(title, title, {}, {}, title);
        }

        UpdateFilterToggleStyles();
    }

    void SetCatalogGames(QList<Game> catalog_games) {
        games = std::move(catalog_games);
        if (!user_selected_game && !detected_games.isEmpty()) {
            const QString previous = title;
            AutoSelectBestDetectedGame();
            if (title != previous && context.auto_load) {
                ReloadMods();
            }
        }
        PopulateGames();
    }

    void LoadGames(int offset = 0) {
        if (offset == 0) {
            if (games_request) {
                games_request->Cancel();
            }
            ++game_epoch;
            incoming_games.clear();
        }
        const auto epoch = game_epoch;
        games_request = client.GetGames(offset, PageSize, [this, epoch, offset](auto response) {
            if (epoch != game_epoch) {
                return;
            }
            if (const auto* page = std::get_if<Page<Game>>(&response)) {
                incoming_games.append(page->items);
                if (page->next_offset && *page->next_offset > offset &&
                    incoming_games.size() < 1000) {
                    LoadGames(*page->next_offset);
                    return;
                }
                SetCatalogGames(std::move(incoming_games));
            }
        });
    }

    void SelectGame(const QString& raw_id, bool emit_signal) {
        const QString id = NormalizeTitleId(raw_id);
        if (title == id) {
            return;
        }
        title = id;
        if (!title.isEmpty() && my_games_only && !detected_by_id.contains(title)) {
            my_games_only = false;
        }
        PopulateGames();
        if (context.auto_load) {
            ReloadMods();
        }
        if (emit_signal) {
            emit owner->SelectedGameChanged(title);
        }
    }

    void RefreshAll() {
        identities.clear();
        if (!installed_mode->isChecked()) {
            LoadGames();
        }
        ReloadMods();
    }

    void ReloadMods() {
        debounce.stop();
        ++list_epoch;
        if (list_request) {
            list_request->Cancel();
        }
        next.reset();
        more->hide();
        results->clear();
        catalog.clear();
        ClearDetail();
        if (installed_mode->isChecked()) {
            ShowInstalled();
            return;
        }
        list_filter = {title, search->text().trimmed(),
                       type->currentIndex() == 0
                           ? std::nullopt
                           : std::optional{static_cast<ModType>(type->currentData().toInt())},
                       static_cast<Sort>(sort->currentData().toInt())};
        FetchMods(0, list_epoch);
    }

    void FetchMods(int offset, int epoch) {
        more->setEnabled(false);
        Status(tr("Loading mods…"));
        list_request =
            client.GetMods(list_filter, offset, PageSize, [this, offset, epoch](auto response) {
                if (epoch != list_epoch) {
                    return;
                }
                if (const auto* error = std::get_if<Error>(&response)) {
                    Status(tr("Could not load the catalog: %1. Retry with Refresh; installed mods "
                              "are still available.")
                               .arg(error->message));
                    more->setEnabled(true);
                    return;
                }
                auto page = std::get<Page<Mod>>(std::move(response));
                for (auto& mod : page.items) {
                    if (catalog.contains(mod.id)) {
                        continue;
                    }
                    auto* item = new QTreeWidgetItem(results, {mod.name, TypeName(mod.type)});
                    item->setSizeHint(0, QSize(0, row_height));
                    item->setData(0, IdRole, mod.id);
                    item->setToolTip(0, mod.summary);
                    const auto key = mod.id;
                    catalog.insert(key, std::move(mod));
                }
                next = page.next_offset && *page.next_offset > offset ? page.next_offset
                                                                      : std::nullopt;
                more->setVisible(next.has_value() && !debounce.isActive() && !reload_after_install);
                more->setEnabled(true);
                Status(catalog.isEmpty()
                           ? tr("No matching mods. Change the game, type, or search filter.")
                           : tr("%n result(s)", nullptr, static_cast<int>(catalog.size())));
            });
    }

    void ShowInstalled() {
        QStringList titles{title};
        if (title.isEmpty()) {
            titles = QDir(context.mods_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        }
        for (const auto& id : titles) {
            for (const auto& installed : ReadInstalledMods(context.mods_root, id)) {
                if (!search->text().trimmed().isEmpty() &&
                    !installed.name.contains(search->text().trimmed(), Qt::CaseInsensitive)) {
                    continue;
                }
                const QString nid = NormalizeTitleId(id);
                const auto det = detected_by_id.constFind(nid);
                const auto game = std::ranges::find_if(games, [&](const Game& g) {
                    return NormalizeTitleId(g.title_id) == nid;
                });
                const QString game_name = det != detected_by_id.cend() && !det->name.isEmpty()
                                              ? det->name
                                          : game != games.cend() ? game->name
                                                                 : nid;
                auto* item = new QTreeWidgetItem(results, {installed.name, game_name});
                item->setSizeHint(0, QSize(0, row_height));
                item->setData(0, IdRole, installed.id);
                item->setData(0, TitleRole, nid);
                item->setToolTip(0, installed.version);
            }
        }
        results->headerItem()->setText(1, tr("Game"));
        Status(results->topLevelItemCount() == 0 ? tr("No installed store mods match this filter.")
                                                 : tr("Installed mods are available offline."));
    }

    void ClearDetail() {
        ++mod_epoch;
        ++preview_epoch;
        if (mod_request) {
            mod_request->Cancel();
        }
        if (preview_request) {
            preview_request->Cancel();
        }
        selected.reset();
        offer_list.clear();
        local.reset();
        local_id.clear();
        local_title.clear();
        name->setText(tr("Select a mod to see its details"));
        byline->clear();
        description->clear();
        description_toggle->hide();
        inspector->verticalScrollBar()->setValue(0);
        preview->clear();
        preview->hide();
        preview_select->hide();
        compatibility_card->hide();
        target_picker->hide();
        release->hide();
        hint->clear();
        hint->hide();
        source->hide();
        install->hide();
        enable->hide();
        uninstall->hide();
        results->headerItem()->setText(1, installed_mode->isChecked() ? tr("Game") : tr("Type"));
    }

    void SelectMod(QTreeWidgetItem* item) {
        ClearDetail();
        if (!item) {
            return;
        }
        const auto id = item->data(0, IdRole).toString();
        local_title = NormalizeTitleId(item->data(0, TitleRole).toString());
        local_id = id;
        if (installed_mode->isChecked()) {
            ReadLocal();
            if (local) {
                name->setText(local->name);
                byline->setText(tr("Installed release: %1").arg(local->version));
                UpdateButtons();
            }
        } else {
            const auto& entry = catalog[id];
            name->setText(entry.name);
            byline->setText(entry.author);
            description->setText(entry.summary);
        }
        FetchDetail(id);
    }

    void FetchDetail(const QString& mod_id) {
        const auto epoch = mod_epoch;
        mod_request = client.GetMod(mod_id, [this, epoch](auto response) {
            if (epoch != mod_epoch) {
                return;
            }
            if (const auto* error = std::get_if<Error>(&response)) {
                hint->setText(tr("Could not load details: %1").arg(error->message));
                hint->show();
                return;
            }
            selected = std::get<Mod>(std::move(response));
            if (local_title.isEmpty()) {
                local_title = title;
                if (local_title.isEmpty()) {
                    const auto found =
                        std::ranges::find_if(selected->title_ids, [this](const QString& id) {
                            const QString nid = NormalizeTitleId(id);
                            return detected_by_id.contains(nid) || Identity(nid).installed;
                        });
                    local_title = NormalizeTitleId(
                        found == selected->title_ids.cend() ? selected->title_ids.value(0) : *found);
                }
            }
            {
                const QSignalBlocker block(target_picker);
                target_picker->clear();
                for (const auto& raw_id : selected->title_ids) {
                    const QString nid = NormalizeTitleId(raw_id);
                    const auto det = detected_by_id.constFind(nid);
                    const auto game = std::ranges::find_if(games, [&](const Game& g) {
                        return NormalizeTitleId(g.title_id) == nid;
                    });
                    const QString label = det != detected_by_id.cend() && !det->name.isEmpty()
                                              ? tr("%1 (Detected)").arg(det->name)
                                          : game != games.cend() ? game->name
                                                                 : nid;
                    target_picker->addItem(label, nid);
                }
                target_picker->setCurrentIndex(target_picker->findData(local_title));
                target_picker->setVisible(title.isEmpty() && !installed_mode->isChecked() &&
                                          target_picker->count() > 1);
            }
            ReadLocal();
            name->setText(selected->name);
            byline->setText(QStringLiteral("%1 · %2 · %3")
                                .arg(selected->author, TypeName(selected->type),
                                     selected->source == Source::GameBanana ? tr("GameBanana")
                                     : selected->demo                       ? tr("Sample")
                                                      : tr("Lichii catalog")));
            const auto full = selected->description.trimmed();
            description_excerpt = full.section(QLatin1Char('\n'), 0, 1).simplified();
            if (description_excerpt.size() > 200) {
                description_excerpt.truncate(200);
                if (description_excerpt.back().isHighSurrogate()) {
                    description_excerpt.chop(1);
                }
            }
            const bool truncated = description_excerpt != full.simplified();
            if (truncated) {
                description_excerpt = description_excerpt.trimmed() + QStringLiteral("…");
            }
            const QSignalBlocker description_signals(description_toggle);
            description_toggle->setChecked(false);
            description_toggle->setText(tr("Show full description"));
            description_toggle->setVisible(truncated);
            description->setText(truncated ? description_excerpt : full);
            source->setVisible(selected->source == Source::GameBanana);
            {
                const QSignalBlocker block(preview_select);
                preview_select->clear();
                if (selected->external) {
                    for (qsizetype i = 0; i < selected->external->preview_urls.size(); ++i) {
                        preview_select->addItem(tr("Preview %1").arg(i + 1));
                    }
                }
                preview_select->setVisible(preview_select->count() > 1);
            }
            LoadPreview();
            BuildOffers();
        });
    }

    void LoadPreview() {
        const auto epoch = ++preview_epoch;
        if (preview_request) {
            preview_request->Cancel();
        }
        if (!selected) {
            return;
        }
        auto mod = *selected;
        if (mod.external && preview_select->currentIndex() >= 0) {
            mod.preview_url = mod.external->preview_urls.value(preview_select->currentIndex());
        }
        preview->clear();
        preview->setVisible(mod.preview_url.isValid() && !mod.preview_url.isEmpty());
        if (preview->isHidden()) {
            return;
        }
        preview_request = client.GetPreview(mod, [this, epoch](auto response) {
            if (epoch != preview_epoch) {
                return;
            }
            if (const auto* image = std::get_if<QImage>(&response)) {
                preview->setPixmap(QPixmap::fromImage(
                    image->scaled(qMax(1, preview->width()), preview->height(), Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation)));
            } else {
                preview->setText(tr("Preview unavailable"));
            }
        });
    }

    void BuildOffers() {
        const auto identity = Identity(local_title);
        const QSignalBlocker block(release);
        offer_list.clear();
        release->clear();
        int first_match = -1;
        if (selected->external) {
            bool first_match_has_checksum = false;
            for (const auto& file : selected->external->files) {
                offer_list.append(
                    {file, {}, Compatibility::Unknown,
                     file.name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) &&
                         file.size_bytes <= MaxArchiveBytes});
                const auto entry =
                    tr("%1 · %2 · %3")
                        .arg(file.name, owner->locale().formattedDataSize(file.size_bytes),
                             owner->locale().toString(file.uploaded_at.date(),
                                                      QLocale::ShortFormat));
                release->addItem(entry);
                release->setItemData(release->count() - 1, entry, Qt::ToolTipRole);
                if ((first_match < 0 || (!first_match_has_checksum && !file.sha256.isEmpty())) &&
                    CanInstall(offer_list.back())) {
                    first_match = release->count() - 1;
                    first_match_has_checksum = !file.sha256.isEmpty();
                }
            }
        } else {
            for (const auto& version : selected->versions) {
                const auto match = MatchVersion(local_title, identity, version);
                offer_list.append({version, version.id, match, true});
                release->addItem(
                    tr("%1 · %2 · %3")
                        .arg(version.id, version.game_version, CompatibilityName(match)));
                if (first_match < 0 && match == Compatibility::Match) {
                    first_match = release->count() - 1;
                }
            }
        }
        release->setCurrentIndex(first_match < 0 ? 0 : first_match);
        release->setVisible(!offer_list.isEmpty());
        UpdateOffer();
    }

    const Offer* CurrentOffer() const {
        const auto index = release->currentIndex();
        return index >= 0 && index < offer_list.size() ? &offer_list[index] : nullptr;
    }

    bool TargetMatchesSelected() const {
        if (!selected) {
            return false;
        }
        return std::ranges::any_of(selected->title_ids, [this](const QString& id) {
            return id.compare(local_title, Qt::CaseInsensitive) == 0;
        });
    }

    bool CanInstall(const Offer& offer) {
        if (!selected || selected->demo || !TargetMatchesSelected() || !offer.supported ||
            !Identity(local_title).installed || offer.compatibility == Compatibility::Mismatch) {
            return false;
        }
        if (const auto* file = std::get_if<ExternalFile>(&offer.file)) {
            return !file->ambiguous_layout && (file->sha256.isEmpty() || file->sha256.size() == 32);
        }
        return offer.compatibility == Compatibility::Match &&
               std::get<Version>(offer.file).sha256.size() == 32;
    }

    void UpdateOffer() {
        if (!selected) {
            return;
        }
        const auto identity = Identity(local_title);
        const auto* offer = CurrentOffer();
        const auto unknown = tr("Unknown");
        const auto short_build = [](const QString& build) {
            return build.size() > 16 ? QStringLiteral("%1…%2").arg(build.left(12), build.right(8))
                                     : build;
        };
        const auto set_row = [&](size_t row, const QString& key, const QString& value,
                                 const QString& tooltip = {}) {
            const auto [left, right] = compatibility_rows[row];
            left->setText(key);
            right->setText(value);
            right->setToolTip(tooltip);
            left->setVisible(!key.isEmpty());
            right->setVisible(!key.isEmpty());
        };
        const auto det = detected_by_id.constFind(local_title);
        const auto game = std::ranges::find_if(games, [&](const Game& g) {
            return NormalizeTitleId(g.title_id) == local_title;
        });
        const QString game_display = det != detected_by_id.cend() && !det->name.isEmpty()
                                         ? det->name
                                     : game != games.cend() ? game->name
                                                            : unknown;
        set_row(0, tr("Catalog game"), game_display);
        set_row(1, tr("Title ID"), local_title);
        set_row(2, tr("Target game version"), unknown);
        set_row(3, tr("Target build ID"), unknown);
        set_row(4, tr("Minimum Lichii"), unknown);
        set_row(5, tr("Installed version"),
                identity.display_version.isEmpty()
                    ? (identity.installed ? unknown : tr("Not in your library"))
                    : identity.display_version);
        set_row(6, tr("Installed build ID"),
                KnownBuild(identity.build_id) ? short_build(identity.build_id) : unknown,
                KnownBuild(identity.build_id) ? identity.build_id : QString{});
        set_row(7, tr("Running Lichii"),
                Common::g_build_release_tag[0] ? QString::fromLatin1(Common::g_build_release_tag)
                                               : tr("Release number unavailable"));
        const auto* external = offer ? std::get_if<ExternalFile>(&offer->file) : nullptr;
        catalog_note->setVisible(external && external->sha256.isEmpty());
        if (offer) {
            const QString color =
                offer->compatibility == Compatibility::Match
                    ? tokens.live.name()
                : offer->compatibility == Compatibility::Mismatch
                    ? tokens.danger.name()
                    : tokens.warn.name();
            compatibility_state->setStyleSheet(
                QStringLiteral("color: %1; font-weight: 600;").arg(color));
            compatibility_state->setText(
                QStringLiteral("●  %1").arg(CompatibilityName(offer->compatibility)));
            if (const auto* version = std::get_if<Version>(&offer->file)) {
                set_row(1, tr("Title ID"), version->title_id);
                set_row(2, tr("Target game version"), version->game_version);
                set_row(3, tr("Target build ID"), short_build(version->build_id),
                        version->build_id);
                set_row(4, tr("Minimum Lichii"), version->min_lichii_build);
            } else {
                const auto& file = std::get<ExternalFile>(offer->file);
                QString visible_builds = unknown;
                if (!file.build_ids.isEmpty()) {
                    visible_builds = short_build(file.build_ids.front());
                    if (file.build_ids.size() > 1) {
                        visible_builds = tr("%1 and %n more", nullptr,
                                            static_cast<int>(file.build_ids.size() - 1))
                                             .arg(visible_builds);
                    }
                }
                set_row(3, tr("Patch build IDs"), visible_builds,
                        file.build_ids.join(QLatin1Char('\n')));
            }
        }
        compatibility_state->setVisible(offer != nullptr);
        compatibility_card->show();
        UpdateButtons();
    }

    void ReadLocal() {
        local.reset();
        if (local_title.isEmpty() || local_id.isEmpty()) {
            return;
        }
        const auto installed = ReadInstalledMods(context.mods_root, local_title);
        const auto it = std::ranges::find(installed, local_id, &InstalledMod::id);
        if (it != installed.cend()) {
            local = *it;
        }
    }

    void UpdateButtons() {
        const auto* offer = CurrentOffer();
        const auto* external = offer ? std::get_if<ExternalFile>(&offer->file) : nullptr;
        const bool installed = local.has_value();
        const bool same_file =
            external && installed && local->version.startsWith(ExternalVersionPrefix(external->id));
        const bool reinstall = same_file && external->sha256.isEmpty();
        const bool current =
            offer && installed &&
            (external ? same_file && !reinstall &&
                            (local->sha256.isEmpty()
                                 ? local->version == ExternalVersion(external->id, external->sha256)
                                 : local->sha256 == external->sha256)
                      : local->version == offer->version);
        install->setVisible(offer != nullptr);
        install->setText(reinstall ? tr("Reinstall") : installed ? tr("Update") : tr("Install"));
        install->setEnabled(!busy && offer && !current && CanInstall(*offer));
        source->setEnabled(!busy);
        const QString primary = BuildPrimaryButtonStyle(tokens, use_glass);
        const bool install_primary = install->isEnabled();
        const bool source_primary = !install_primary && !source->isHidden() && !busy;
        install->setStyleSheet(install_primary ? primary : QString{});
        source->setStyleSheet(source_primary ? primary : QString{});
        if (mod_actions->indexOf(source_primary ? source : install) != 0) {
            auto* first = source_primary ? source : install;
            mod_actions->removeWidget(first);
            mod_actions->insertWidget(0, first);
        }
        enable->setVisible(installed);
        uninstall->setVisible(installed);
        enable->setEnabled(!busy && static_cast<bool>(context.set_enabled));
        uninstall->setEnabled(!busy);
        const bool enabled =
            installed && (!context.is_enabled || context.is_enabled(local_title, local_id));
        enable->setText(enabled ? tr("Disable") : tr("Enable"));
        hint->clear();
        if (offer && selected) {
            if (selected->demo) {
                hint->setText(tr("This sample is not available for installation."));
            } else if (external && external->ambiguous_layout) {
                hint->setText(tr("This ZIP requires choosing components. Open GameBanana to "
                                 "install it manually."));
            } else if (!TargetMatchesSelected()) {
                hint->setText(tr("This listing does not target the selected game."));
            } else if (offer->compatibility == Compatibility::Mismatch) {
                hint->setText(
                    tr("This release does not match the installed game or Lichii build."));
            } else if (!offer->supported) {
                hint->setText(tr("Only ZIP files up to 64 MiB can be installed. Open GameBanana "
                                 "for this file."));
            } else if (!Identity(local_title).installed) {
                hint->setText(tr("Add this game to your library to install its mods."));
            } else if (!CanInstall(*offer)) {
                hint->setText(tr("The installed game or Lichii build could not be verified."));
            } else if (current || reinstall) {
                hint->setText(enabled ? tr("Installed and enabled") : tr("Installed and disabled"));
            } else {
                hint->setText(installed ? tr("A different release is available.") : QString{});
            }
        } else if (installed) {
            hint->setText(enabled ? tr("Installed and enabled") : tr("Installed and disabled"));
        }
        hint->setVisible(!hint->text().isEmpty());
    }

    void OpenSource() {
        if (selected && selected->source == Source::GameBanana && !selected->source_url.isEmpty()) {
            QDesktopServices::openUrl(selected->source_url);
        }
    }

    void InstallFailed(const QString& error, bool external) {
        Status(error);
        if (!external) {
            return;
        }
        const QUrl source_url = selected ? selected->source_url : QUrl{};
        QMessageBox prompt(QMessageBox::Warning, tr("Could not install the mod"), error,
                           QMessageBox::Close, owner);
        auto* open = prompt.addButton(tr("View on GameBanana"), QMessageBox::ActionRole);
        prompt.exec();
        if (prompt.clickedButton() == open && !source_url.isEmpty()) {
            QDesktopServices::openUrl(source_url);
        }
    }

    void SetProgress(const QString& text, int percent = -1, bool can_cancel = false) {
        if (text.isEmpty()) {
            progress->hide();
            return;
        }
        progress_label->setText(text);
        cancel_button->setVisible(can_cancel);
        progress_bar->setRange(0, percent < 0 ? 0 : 100);
        if (percent >= 0) {
            progress_bar->setValue(percent);
        }
        progress->show();
    }

    void SetBusy(bool value) {
        busy = value;
        for (QWidget* widget :
             {static_cast<QWidget*>(game_picker), static_cast<QWidget*>(refresh),
              static_cast<QWidget*>(all_games), static_cast<QWidget*>(my_games_btn),
              static_cast<QWidget*>(all_catalog_btn), static_cast<QWidget*>(preview_select),
              static_cast<QWidget*>(installed_mode), static_cast<QWidget*>(search),
              static_cast<QWidget*>(game_tree), static_cast<QWidget*>(results),
              static_cast<QWidget*>(more), static_cast<QWidget*>(release),
              static_cast<QWidget*>(target_picker), static_cast<QWidget*>(close_button)}) {
            widget->setEnabled(!value);
        }
        my_games_btn->setEnabled(!value && !detected_games.isEmpty());
        type->setEnabled(!value && !installed_mode->isChecked());
        sort->setEnabled(!value && !installed_mode->isChecked());
        UpdateButtons();
    }

    void StartInstall() {
        const auto* offer = CurrentOffer();
        if (!offer || !CanInstall(*offer) || busy) {
            return;
        }
        const auto chosen = *offer;
        const auto* external_file = std::get_if<ExternalFile>(&chosen.file);
        const bool external = external_file != nullptr;
        const bool unverified = external && external_file->sha256.isEmpty();
        const auto label = selected->name;
        const auto epoch = mod_epoch;
        if (external && !unverified && chosen.compatibility == Compatibility::Unknown &&
            QMessageBox::question(
                owner, tr("Compatibility unverified"),
                tr("Lichii could not confirm that this file works with your game version. "
                   "It will check the download's SHA-256 before installing. Continue?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        if (epoch != mod_epoch) {
            return;
        }
        reload_after_install = debounce.isActive();
        debounce.stop();
        SetBusy(true);
        progress_catalog_note->setVisible(unverified);
        SetProgress(tr("Downloading %1…").arg(label), 0, true);
        const auto generation = ++install_epoch;
        const auto version = chosen.version;
        const auto file_id = external ? external_file->id : 0;
        auto receive = [this, generation, version, file_id, label, external](auto response) {
            if (generation != install_epoch) {
                return;
            }
            if (const auto* error = std::get_if<Error>(&response)) {
                FinishOperation();
                InstallFailed(tr("Could not download the mod: %1").arg(error->message), external);
                return;
            }
            auto received = std::get<DownloadedFile>(std::move(response));
            auto owned = std::shared_ptr<QTemporaryFile>(std::move(received.file));
            ApplyDownloaded(owned, external ? ExternalVersion(file_id, received.sha256) : version,
                            received.sha256, label, external);
        };
        auto report = [this, generation, unverified](qint64 received, qint64 total) {
            if (generation != install_epoch || total <= 0) {
                return;
            }
            if (received >= total) {
                SetProgress(unverified ? tr("Checking the download…") : tr("Verifying SHA-256…"));
            } else {
                SetProgress(tr("Downloading %1 of %2 KiB…").arg(received / 1024).arg(total / 1024),
                            static_cast<int>(100 * received / total), true);
            }
        };
        download = std::visit(
            [&](const auto& file) {
                return client.Download(file, std::move(receive), std::move(report));
            },
            chosen.file);
    }

    void ApplyDownloaded(const std::shared_ptr<QTemporaryFile>& file, const QString& version,
                         const QByteArray& digest, const QString& label, bool external) {
        SetProgress(tr("Checking the ZIP and installing the mod…"));
        applying = true;
        const auto root = context.mods_root;
        const auto target = local_title;
        const auto id = local_id;
        auto* install_task = new QFutureWatcher<QString>(owner);
        connect(install_task, &QFutureWatcher<QString>::finished, owner,
                [this, install_task, external] {
                    const auto error = install_task->result();
                    install_task->deleteLater();
                    if (error.isEmpty()) {
                        changed = true;
                        ReadLocal();
                    }
                    FinishOperation();
                    if (error.isEmpty()) {
                        Status(tr("Installation complete. The mod is available in Installed."));
                        emit owner->ModsChanged();
                    } else {
                        InstallFailed(error, external);
                    }
                });
        install_task->setFuture(QtConcurrent::run([root, target, id, label, version, file, digest,
                                                   external] {
            return InstallMod(root, target, id, label, version, file->fileName(), digest, external);
        }));
    }

    void FinishOperation() {
        applying = false;
        SetProgress({});
        SetBusy(false);
        if (reload_after_install) {
            reload_after_install = false;
            debounce.start();
        }
    }

    void CancelDownload() {
        if (applying || !busy) {
            return;
        }
        ++install_epoch;
        if (download) {
            download->Cancel();
        }
        FinishOperation();
        Status(tr("Download canceled. No add-ons were changed."));
    }

    void ToggleEnabled() {
        if (!local || !context.set_enabled || busy) {
            return;
        }
        const bool before = context.is_enabled && context.is_enabled(local_title, local_id);
        if (!context.set_enabled(local_title, local_id, !before)) {
            Status(tr("Could not save the change to this mod."));
            return;
        }
        changed = true;
        UpdateButtons();
        Status(before ? tr("Mod disabled.") : tr("Mod enabled."));
        emit owner->ModsChanged();
    }

    void RemoveInstalled() {
        if (!local || busy) {
            return;
        }
        const auto epoch = mod_epoch;
        if (QMessageBox::question(owner, tr("Uninstall mod"), tr("Uninstall %1?").arg(local->name),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes ||
            epoch != mod_epoch) {
            return;
        }
        const auto error = UninstallMod(context.mods_root, local_title, local_id);
        if (!error.isEmpty()) {
            Status(error);
            return;
        }
        const bool preference_reset =
            !context.set_enabled || context.set_enabled(local_title, local_id, true);
        changed = true;
        local.reset();
        if (installed_mode->isChecked()) {
            ReloadMods();
        } else {
            UpdateButtons();
        }
        Status(preference_reset
                   ? tr("Mod uninstalled.")
                   : tr("Mod uninstalled, but Lichii could not clear its saved disabled state."));
        emit owner->ModsChanged();
    }

    Widget* owner{};
    DialogContext context;
    Client client;
    bool embedded{true};
    GlassUi::PaletteTokens tokens;
    bool use_glass{false};
    bool dark{true};
    int row_height{30};
    bool my_games_only{false};
    bool user_selected_game{false};

    QTimer debounce;
    QList<DetectedGame> detected_games;
    QHash<QString, DetectedGame> detected_by_id;
    QList<Game> games;
    QList<Game> incoming_games;
    QHash<QString, Mod> catalog;
    QHash<QString, GameIdentity> identities;
    QList<Offer> offer_list;
    ModFilter list_filter{};
    std::optional<int> next;
    std::optional<Mod> selected;
    std::optional<InstalledMod> local;
    QString description_excerpt;
    QString title;
    QString local_title;
    QString local_id;
    QPointer<Request> games_request;
    QPointer<Request> list_request;
    QPointer<Request> mod_request;
    QPointer<Request> preview_request;
    QPointer<Request> download;
    int game_epoch{};
    int list_epoch{};
    int mod_epoch{};
    int preview_epoch{};
    int install_epoch{};
    bool changed{};
    bool busy{};
    bool applying{};
    bool reload_after_install{};

    QLabel* detect_banner{};
    QPushButton* my_games_btn{};
    QPushButton* all_catalog_btn{};
    QComboBox* game_picker{};
    QComboBox* type{};
    QComboBox* sort{};
    QComboBox* release{};
    QComboBox* target_picker{};
    QComboBox* preview_select{};
    QLineEdit* search{};
    QTreeWidget* game_tree{};
    QTreeWidget* results{};
    QFrame* inspector_frame{};
    QScrollArea* inspector{};
    QLabel* name{};
    QLabel* byline{};
    QLabel* description{};
    QToolButton* description_toggle{};
    QLabel* preview{};
    QLabel* compatibility_state{};
    QLabel* catalog_note{};
    QLabel* hint{};
    QLabel* status{};
    QFrame* compatibility_card{};
    QList<QFrame*> compatibility_dividers;
    std::array<std::pair<QLabel*, QLabel*>, 8> compatibility_rows{};
    QHBoxLayout* mod_actions{};
    QDialog* progress{};
    QLabel* progress_label{};
    QLabel* progress_catalog_note{};
    QProgressBar* progress_bar{};
    QPushButton* cancel_button{};
    QPushButton* all_games{};
    QPushButton* refresh{};
    QPushButton* installed_mode{};
    QPushButton* more{};
    QPushButton* install{};
    QPushButton* source{};
    QPushButton* enable{};
    QPushButton* uninstall{};
    QPushButton* close_button{};
};

Widget::Widget(DialogContext context, QWidget* parent, bool embedded)
    : QWidget(parent), impl(std::make_unique<Impl>(this, std::move(context), embedded)) {}

Widget::~Widget() = default;

bool Widget::Changed() const {
    return impl->changed;
}

bool Widget::CanClose() const {
    return !impl->applying && !impl->busy;
}

void Widget::CancelIfBusy() {
    impl->CancelDownload();
}

void Widget::SetDetectedGames(QList<DetectedGame> games) {
    impl->SetDetectedGames(std::move(games));
}

void Widget::SetSelectedTitleId(const QString& title_id) {
    impl->user_selected_game = true;
    impl->SelectGame(title_id, false);
}

void Widget::SetMyGamesOnly(bool my_games_only) {
    impl->SetMyGamesOnly(my_games_only);
}

void Widget::SetCatalogGames(QList<Game> catalog_games) {
    impl->SetCatalogGames(std::move(catalog_games));
}

void Widget::RefreshTheme() {
    impl->RefreshTheme();
}

void Widget::RefreshAll() {
    impl->RefreshAll();
}

QString Widget::SelectedTitleId() const {
    return impl->title;
}

bool Widget::IsMyGamesOnly() const {
    return impl->my_games_only;
}

int Widget::DetectedGameCount() const {
    return static_cast<int>(impl->detected_games.size());
}

int Widget::MatchedCatalogGameCount() const {
    return impl->MatchedCatalogGameCount();
}

QTreeWidget* Widget::GameTree() const {
    return impl->game_tree;
}

namespace {

class ModalDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(ModStore::Dialog)

public:
    ModalDialog(QWidget* parent, DialogContext ctx) : QDialog(parent) {
        setWindowTitle(tr("Mod Store"));
        resize(1080, 760);
        setMinimumSize(760, 520);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        widget = new Widget(std::move(ctx), this, false);
        layout->addWidget(widget);
        connect(widget, &Widget::CloseRequested, this, &ModalDialog::reject);
    }

    bool Changed() const {
        return widget->Changed();
    }

    void reject() override {
        if (!widget->CanClose()) {
            widget->CancelIfBusy();
            return;
        }
        QDialog::reject();
    }

private:
    Widget* widget{};
};

} // namespace

bool ShowDialog(QWidget* parent, DialogContext context) {
    ModalDialog dialog(parent, std::move(context));
    dialog.exec();
    return dialog.Changed();
}

} // namespace ModStore
