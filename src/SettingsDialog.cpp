#include "SettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QProxyStyle>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <set>

#include "AppInfo.h"
#include "AppUpdater.h"
#include "FileAssociation.h"

namespace {

constexpr int kPreviewPointSize = 18;
constexpr const char* kPreviewText = "가나다라 ABC 123";
constexpr const char* kDefaultFontLabel = "기본 글꼴";
constexpr const char* kShortcutHint = "항목을 더블클릭하거나 변경을 누른 뒤 새 키를 누르세요.";

// Preserves the native OS button look while adding comfortable padding around text.
class PaddedButtonStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size,
                           const QWidget* widget) const override {
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_PushButton) {
            result.setWidth(std::max(result.width() + 24, 80));
            result.setHeight(std::max(result.height() + 8, 30));
        }
        return result;
    }
};

class SettingsDialog : public QDialog {
public:
    SettingsDialog(QWidget* owner, const Preferences& current, const std::function<void(const Preferences&)>& apply)
        : QDialog(owner), owner_(owner), working_(current), applied_(current), apply_(apply) {
        setWindowTitle(QStringLiteral("설정"));
        setModal(true);
        setMinimumSize(540, 480);

        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(12, 12, 12, 12);
        mainLayout->setSpacing(10);
        tabs_ = new QTabWidget(this);
        tabs_->addTab(BuildGeneralPage(), QStringLiteral("일반"));
        tabs_->addTab(BuildSubtitlePage(), QStringLiteral("자막"));
        tabs_->addTab(BuildShortcutPage(), QStringLiteral("단축키"));
        tabs_->addTab(BuildAboutPage(), QStringLiteral("정보"));
        mainLayout->addWidget(tabs_, 1);

        auto* buttonRow = new QHBoxLayout();
        buttonRow->setSpacing(8);
        buttonRow->addStretch(1);
        okButton_ = new QPushButton(QStringLiteral("확인"), this);
        cancelButton_ = new QPushButton(QStringLiteral("취소"), this);
        applyButton_ = new QPushButton(QStringLiteral("적용"), this);
        okButton_->setDefault(true);
        buttonRow->addWidget(okButton_);
        buttonRow->addWidget(cancelButton_);
        buttonRow->addWidget(applyButton_);
        mainLayout->addLayout(buttonRow);

        buttonStyle_ = new PaddedButtonStyle();
        buttonStyle_->setParent(this);
        for (QPushButton* btn : findChildren<QPushButton*>()) {
            btn->setStyle(buttonStyle_);
        }

        connect(okButton_, &QPushButton::clicked, this, [this] {
            Apply();
            accept();
        });
        connect(cancelButton_, &QPushButton::clicked, this, [this] {
            StopCapture(false);
            reject();
        });
        connect(applyButton_, &QPushButton::clicked, this, [this] {
            Apply();
        });

        UpdatePreview();
        UpdateAssociationState();
        UpdateShortcutButtons();
        UpdateApplyButton();
        loading_ = false;
    }

    ~SettingsDialog() override {
        StopCapture(false);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (capturing_) {
            if (event->type() == QEvent::ShortcutOverride) {
                event->accept();
                return true;
            }
            if (event->type() == QEvent::KeyPress) {
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                if (!IsModifierKey(keyEvent->key())) {
                    const KeyChord chord = ChordFromKeyEvent(keyEvent);
                    if (CanBeShortcut(chord)) {
                        AssignCapturedChord(chord);
                    } else {
                        shortcutStatus_->setText(KeyChordName(chord) + QStringLiteral(" 키는 단축키로 쓸 수 없습니다. 다른 키를 누르세요."));
                    }
                }
                return true;
            }
            if (event->type() == QEvent::KeyRelease) {
                return true;
            }
            if (event->type() == QEvent::MouseButtonPress ||
                event->type() == QEvent::MouseButtonDblClick ||
                event->type() == QEvent::WindowDeactivate) {
                StopCapture(true);
                return false;
            }
        }
        return QDialog::eventFilter(watched, event);
    }

private:
    QWidget* BuildGeneralPage() {
        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);

        auto* playGroup = new QGroupBox(QStringLiteral("재생"), page);
        auto* playLayout = new QVBoxLayout(playGroup);

        auto* seekRow = new QHBoxLayout();
        seekRow->addWidget(new QLabel(QStringLiteral("앞으로/뒤로 이동 간격:"), playGroup));
        seekSpin_ = new QSpinBox(playGroup);
        seekSpin_->setRange(kMinSeekSeconds, kMaxSeekSeconds);
        seekSpin_->setValue(working_.seekSeconds);
        seekRow->addWidget(seekSpin_);
        seekRow->addWidget(new QLabel(QStringLiteral("초 (1~600)"), playGroup));
        seekRow->addStretch(1);
        playLayout->addLayout(seekRow);

        playlistFitCheck_ = new QCheckBox(QStringLiteral("재생 목록에서 다른 파일로 넘어갈 때 창을 영상 크기에 맞추기"), playGroup);
        playlistFitCheck_->setChecked(working_.fitWindowOnPlaylistChange);
        playLayout->addWidget(playlistFitCheck_);
        layout->addWidget(playGroup);

        auto* assocGroup = new QGroupBox(QStringLiteral("탐색기 연결"), page);
        auto* assocLayout = new QVBoxLayout(assocGroup);
        assocStatus_ = new QLabel(assocGroup);
        assocLayout->addWidget(assocStatus_);

        auto* assocHint = new QLabel(
            QStringLiteral("등록하면 영상과 자막 파일의 \"연결 프로그램\" 목록에 표시됩니다. 현재 사용자에게만 적용됩니다. "
                           "기본 앱을 직접 바꾸지는 않지만, 형식에 따라 다음에 열 때 어떤 앱으로 열지 물어볼 수 있습니다. "
                           "더블클릭으로 바로 열려면 그때 선택하거나 기본 앱 설정에서 지정하세요.\n"
                           "실행 파일을 옮겼다면 다시 등록하세요. 이 항목은 누르는 즉시 적용됩니다."),
            assocGroup);
        assocHint->setWordWrap(true);
        assocLayout->addWidget(assocHint);

        auto* assocButtons = new QHBoxLayout();
        assocButtons->setSpacing(8);
        assocRegisterBtn_ = new QPushButton(QStringLiteral("등록"), assocGroup);
        assocUnregisterBtn_ = new QPushButton(QStringLiteral("등록 해제"), assocGroup);
        assocDefaultsBtn_ = new QPushButton(QStringLiteral("기본 앱 설정 열기"), assocGroup);
        assocButtons->addWidget(assocRegisterBtn_);
        assocButtons->addWidget(assocUnregisterBtn_);
        assocButtons->addWidget(assocDefaultsBtn_);
        assocButtons->addStretch(1);
        assocLayout->addLayout(assocButtons);
        layout->addWidget(assocGroup);
        layout->addStretch(1);

        connect(seekSpin_, &QSpinBox::valueChanged, this, [this](int val) {
            if (!loading_) {
                working_.seekSeconds = std::clamp(val, kMinSeekSeconds, kMaxSeekSeconds);
                UpdateApplyButton();
            }
        });
        connect(playlistFitCheck_, &QCheckBox::toggled, this, [this](bool checked) {
            if (!loading_) {
                working_.fitWindowOnPlaylistChange = checked;
                UpdateApplyButton();
            }
        });
        connect(assocRegisterBtn_, &QPushButton::clicked, this, [this] {
            if (!RegisterFileAssociation()) {
                assocStatus_->setText(QStringLiteral("상태: 등록하지 못했습니다"));
                return;
            }
            UpdateAssociationState();
        });
        connect(assocUnregisterBtn_, &QPushButton::clicked, this, [this] {
            UnregisterFileAssociation();
            UpdateAssociationState();
        });
        connect(assocDefaultsBtn_, &QPushButton::clicked, this, [] {
            OpenDefaultAppsSettings();
        });

        return page;
    }

    QWidget* BuildSubtitlePage() {
        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);
        auto* grid = new QGridLayout();

        grid->addWidget(new QLabel(QStringLiteral("글꼴:"), page), 0, 0);
        fontCombo_ = new QComboBox(page);
        FillFontList();
        grid->addWidget(fontCombo_, 0, 1, 1, 2);

        grid->addWidget(new QLabel(QStringLiteral("크기:"), page), 1, 0);
        sizeSpin_ = new QSpinBox(page);
        sizeSpin_->setRange(kMinSubtitleFontSize, kMaxSubtitleFontSize);
        sizeSpin_->setValue(working_.subtitleStyle.fontSize);
        grid->addWidget(sizeSpin_, 1, 1);
        grid->addWidget(new QLabel(QStringLiteral("(10~150, 기본 38)"), page), 1, 2);

        grid->addWidget(new QLabel(QStringLiteral("외곽선:"), page), 2, 0);
        outlineCombo_ = new QComboBox(page);
        FillOutlineList();
        grid->addWidget(outlineCombo_, 2, 1);

        grid->addWidget(new QLabel(QStringLiteral("높이:"), page), 3, 0);
        heightSpin_ = new QSpinBox(page);
        heightSpin_->setRange(0, kMaxSubtitleHeightPercent);
        heightSpin_->setValue(working_.subtitleStyle.heightPercent);
        grid->addWidget(heightSpin_, 3, 1);
        grid->addWidget(new QLabel(QStringLiteral("% (0은 맨 아래, 100은 맨 위)"), page), 3, 2);
        grid->setColumnStretch(2, 1);
        layout->addLayout(grid);

        layout->addWidget(new QLabel(QStringLiteral("글꼴 미리 보기"), page));
        previewLabel_ = new QLabel(QString::fromUtf8(kPreviewText), page);
        previewLabel_->setAlignment(Qt::AlignCenter);
        previewLabel_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        previewLabel_->setMinimumHeight(48);
        layout->addWidget(previewLabel_);

        auto* subHint = new QLabel(
            QStringLiteral("적용을 누르면 재생 중인 영상의 자막에 바로 반영됩니다.\n"
                           "크기는 창 높이 720을 기준으로 한 값이며, 창 크기에 따라 함께 변합니다.\n"
                           "ASS 자막처럼 자체 스타일이나 위치가 있는 자막은 그 설정을 우선합니다."),
            page);
        subHint->setWordWrap(true);
        layout->addWidget(subHint);
        layout->addStretch(1);

        connect(fontCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (!loading_) {
                working_.subtitleStyle.font = index <= 0 ? QString() : fontCombo_->itemText(index);
                UpdatePreview();
                UpdateApplyButton();
            }
        });
        connect(sizeSpin_, &QSpinBox::valueChanged, this, [this](int val) {
            if (!loading_) {
                working_.subtitleStyle.fontSize = std::clamp(val, kMinSubtitleFontSize, kMaxSubtitleFontSize);
                UpdateApplyButton();
            }
        });
        connect(outlineCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (!loading_ && index >= 0 && static_cast<size_t>(index) < kSubtitleOutlineSizes.size()) {
                working_.subtitleStyle.outlineSize = kSubtitleOutlineSizes[static_cast<size_t>(index)];
                UpdateApplyButton();
            }
        });
        connect(heightSpin_, &QSpinBox::valueChanged, this, [this](int val) {
            if (!loading_) {
                working_.subtitleStyle.heightPercent = std::clamp(val, 0, kMaxSubtitleHeightPercent);
                UpdateApplyButton();
            }
        });

        return page;
    }

    QWidget* BuildShortcutPage() {
        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);

        shortcutList_ = new QTreeWidget(page);
        shortcutList_->setColumnCount(2);
        shortcutList_->setHeaderLabels({QStringLiteral("동작"), QStringLiteral("단축키")});
        shortcutList_->setRootIsDecorated(false);
        shortcutList_->setSelectionMode(QAbstractItemView::SingleSelection);
        shortcutList_->setUniformRowHeights(true);
        shortcutList_->setAttribute(Qt::WA_InputMethodEnabled, false);
        shortcutList_->header()->setStretchLastSection(true);
        shortcutList_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        shortcutList_->header()->setSectionResizeMode(1, QHeaderView::Interactive);
        FillShortcutList();
        if (shortcutList_->topLevelItemCount() > 0) {
            shortcutList_->setCurrentItem(shortcutList_->topLevelItem(0));
        }
        layout->addWidget(shortcutList_, 1);

        shortcutStatus_ = new QLabel(QString::fromUtf8(kShortcutHint), page);
        shortcutStatus_->setWordWrap(true);
        layout->addWidget(shortcutStatus_);

        auto* btnRow = new QHBoxLayout();
        btnRow->setSpacing(8);
        shortcutChangeBtn_ = new QPushButton(QStringLiteral("변경"), page);
        shortcutClearBtn_ = new QPushButton(QStringLiteral("지우기"), page);
        shortcutDefaultsBtn_ = new QPushButton(QStringLiteral("모두 기본값으로"), page);
        btnRow->addWidget(shortcutChangeBtn_);
        btnRow->addWidget(shortcutClearBtn_);
        btnRow->addStretch(1);
        btnRow->addWidget(shortcutDefaultsBtn_);
        layout->addLayout(btnRow);

        connect(shortcutList_, &QTreeWidget::currentItemChanged, this, [this] {
            UpdateShortcutButtons();
        });
        connect(shortcutList_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
            const int row = shortcutList_->indexOfTopLevelItem(item);
            if (row >= 0) {
                StartCapture(row);
            }
        });
        connect(shortcutChangeBtn_, &QPushButton::clicked, this, [this] {
            const int row = SelectedShortcutRow();
            if (row >= 0) {
                StartCapture(row);
            }
        });
        connect(shortcutClearBtn_, &QPushButton::clicked, this, [this] {
            const int row = SelectedShortcutRow();
            if (row >= 0) {
                working_.shortcuts[static_cast<size_t>(row)] = kNoKey;
                ShowShortcutRow(row);
                shortcutStatus_->setText(QString::fromUtf8(kShortcutHint));
                UpdateShortcutButtons();
                UpdateApplyButton();
            }
        });
        connect(shortcutDefaultsBtn_, &QPushButton::clicked, this, [this] {
            working_.shortcuts = DefaultShortcuts();
            FillShortcutList();
            shortcutStatus_->setText(QStringLiteral("모든 단축키를 기본값으로 되돌렸습니다. 적용하거나 확인을 눌러야 반영됩니다."));
            UpdateShortcutButtons();
            UpdateApplyButton();
        });

        return page;
    }

    QWidget* BuildAboutPage() {
        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(12);

        // 1) Program info header (icon, name, version, install type, GitHub buttons)
        auto* headerRow = new QHBoxLayout();
        headerRow->setSpacing(14);

        auto* iconLabel = new QLabel(page);
        iconLabel->setPixmap(QIcon(QStringLiteral(":/res/app.ico")).pixmap(48, 48));
        iconLabel->setFixedSize(52, 52);
        iconLabel->setAlignment(Qt::AlignCenter);
        headerRow->addWidget(iconLabel, 0, Qt::AlignTop);

        auto* titleCol = new QVBoxLayout();
        titleCol->setSpacing(4);
        auto* nameLabel = new QLabel(QString::fromUtf8(AppInfo::kAppName), page);
        QFont nameFont = nameLabel->font();
        nameFont.setPointSize(nameFont.pointSize() + 3);
        nameFont.setBold(true);
        nameLabel->setFont(nameFont);
        titleCol->addWidget(nameLabel);

        const InstallKind installKind = AppUpdater::DetectInstallKind();
        auto* versionLabel = new QLabel(
            QStringLiteral("버전 v%1  (%2)")
                .arg(QCoreApplication::applicationVersion(), AppUpdater::InstallKindLabel(installKind)),
            page);
        titleCol->addWidget(versionLabel);

        auto* linkRow = new QHBoxLayout();
        linkRow->setContentsMargins(0, 6, 0, 0);
        linkRow->setSpacing(8);
        auto* githubBtn = new QPushButton(QStringLiteral("GitHub 열기"), page);
        auto* releasesBtn = new QPushButton(QStringLiteral("릴리스 페이지"), page);
        linkRow->addWidget(githubBtn);
        linkRow->addWidget(releasesBtn);
        linkRow->addStretch(1);
        titleCol->addLayout(linkRow);
        headerRow->addLayout(titleCol, 1);
        layout->addLayout(headerRow);

        connect(githubBtn, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl(QString::fromLatin1(AppInfo::kGithubUrl)));
        });
        connect(releasesBtn, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl(QString::fromLatin1(AppInfo::kGithubReleasesUrl)));
        });

        // 2) GitHub Releases auto-update section
        auto* updateGroup = new QGroupBox(QStringLiteral("업데이트"), page);
        auto* updateLayout = new QVBoxLayout(updateGroup);
        updateLayout->setContentsMargins(12, 10, 12, 12);
        updateLayout->setSpacing(8);

        updateStatus_ = new QLabel(QStringLiteral("GitHub 릴리스에서 최신 버전을 확인할 수 있습니다."), updateGroup);
        updateStatus_->setWordWrap(true);
        updateLayout->addWidget(updateStatus_);

        updateProgress_ = new QProgressBar(updateGroup);
        updateProgress_->setRange(0, 100);
        updateProgress_->setValue(0);
        updateProgress_->hide();
        updateLayout->addWidget(updateProgress_);

        auto* updateBtnRow = new QHBoxLayout();
        updateBtnRow->setSpacing(8);
        checkUpdateBtn_ = new QPushButton(QStringLiteral("업데이트 확인"), updateGroup);
        applyUpdateBtn_ = new QPushButton(QStringLiteral("지금 업데이트"), updateGroup);
        applyUpdateBtn_->setEnabled(false);
        updateBtnRow->addWidget(checkUpdateBtn_);
        updateBtnRow->addWidget(applyUpdateBtn_);
        updateBtnRow->addStretch(1);
        updateLayout->addLayout(updateBtnRow);
        layout->addWidget(updateGroup);

        updater_ = new AppUpdater(this);
        connect(checkUpdateBtn_, &QPushButton::clicked, this, [this] {
            updater_->CheckForUpdates();
        });
        connect(applyUpdateBtn_, &QPushButton::clicked, this, [this] {
            if (latestRelease_.updateAvailable) {
                updater_->DownloadAndApply(latestRelease_);
            }
        });
        connect(updater_, &AppUpdater::checkStarted, this, [this] {
            checkUpdateBtn_->setEnabled(false);
            applyUpdateBtn_->setEnabled(false);
            updateProgress_->hide();
            updateStatus_->setText(QStringLiteral("업데이트 확인 중..."));
        });
        connect(updater_, &AppUpdater::checkFinished, this, [this](const ReleaseInfo& info) {
            latestRelease_ = info;
            checkUpdateBtn_->setEnabled(true);
            if (!info.updateAvailable) {
                applyUpdateBtn_->setEnabled(false);
                applyUpdateBtn_->setText(QStringLiteral("지금 업데이트"));
                updateStatus_->setText(
                    QStringLiteral("최신 버전입니다 (현재 v%1 / 최신 v%2).")
                        .arg(QCoreApplication::applicationVersion(), info.version));
                return;
            }
            applyUpdateBtn_->setEnabled(true);
            if (info.canAutoUpdate) {
                applyUpdateBtn_->setText(QStringLiteral("지금 업데이트"));
                updateStatus_->setText(
                    QStringLiteral("새 버전 v%1을(를) 사용할 수 있습니다. (%2)")
                        .arg(info.version, info.assetName));
            } else if (!info.assetDownloadUrl.isEmpty()) {
                applyUpdateBtn_->setText(QStringLiteral("패키지 다운로드"));
                updateStatus_->setText(
                    QStringLiteral("새 버전 v%1을(를) 사용할 수 있습니다. (%2)")
                        .arg(info.version, info.assetName));
            } else {
                applyUpdateBtn_->setText(QStringLiteral("릴리스 페이지 열기"));
                updateStatus_->setText(
                    QStringLiteral("새 버전 v%1을(를) 사용할 수 있습니다. GitHub 릴리스에서 다운로드하세요.")
                        .arg(info.version));
            }
        });
        connect(updater_, &AppUpdater::checkFailed, this, [this](const QString& error) {
            checkUpdateBtn_->setEnabled(true);
            applyUpdateBtn_->setEnabled(false);
            updateStatus_->setText(error);
        });
        connect(updater_, &AppUpdater::downloadStarted, this, [this](const QString& assetName) {
            checkUpdateBtn_->setEnabled(false);
            applyUpdateBtn_->setEnabled(false);
            updateProgress_->setRange(0, 100);
            updateProgress_->setValue(0);
            updateProgress_->show();
            updateStatus_->setText(QStringLiteral("다운로드 중: %1").arg(assetName));
        });
        connect(updater_, &AppUpdater::downloadProgress, this, [this](qint64 received, qint64 total) {
            if (total > 0) {
                updateProgress_->setRange(0, 100);
                updateProgress_->setValue(static_cast<int>((received * 100) / total));
                const double recvMb = static_cast<double>(received) / (1024.0 * 1024.0);
                const double totalMb = static_cast<double>(total) / (1024.0 * 1024.0);
                updateStatus_->setText(QStringLiteral("다운로드 중... %1 / %2 MB")
                                           .arg(recvMb, 0, 'f', 1)
                                           .arg(totalMb, 0, 'f', 1));
            } else {
                updateProgress_->setRange(0, 0);
            }
        });
        connect(updater_, &AppUpdater::downloadFailed, this, [this](const QString& error) {
            updateProgress_->hide();
            checkUpdateBtn_->setEnabled(true);
            applyUpdateBtn_->setEnabled(latestRelease_.updateAvailable);
            updateStatus_->setText(error);
        });
        connect(updater_, &AppUpdater::updateReadyToApply, this, [this](bool restartRequired, const QString& message) {
            updateProgress_->hide();
            updateStatus_->setText(message);
            if (restartRequired) {
                accept();
                if (owner_) {
                    QMetaObject::invokeMethod(owner_, &QWidget::close, Qt::QueuedConnection);
                } else {
                    QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
                }
            } else {
                checkUpdateBtn_->setEnabled(true);
                applyUpdateBtn_->setEnabled(true);
                QMessageBox::information(this, QStringLiteral("업데이트"), message);
            }
        });

        // 3) Used libraries list (managed via AppInfo::kUsedLibraries)
        auto* libGroup = new QGroupBox(QStringLiteral("사용 라이브러리"), page);
        auto* libLayout = new QVBoxLayout(libGroup);

        libraryList_ = new QTreeWidget(libGroup);
        libraryList_->setColumnCount(3);
        libraryList_->setHeaderLabels({QStringLiteral("라이브러리"), QStringLiteral("라이선스"), QStringLiteral("설명")});
        libraryList_->setRootIsDecorated(false);
        libraryList_->setSelectionMode(QAbstractItemView::SingleSelection);
        libraryList_->setUniformRowHeights(true);
        libraryList_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        libraryList_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        libraryList_->header()->setSectionResizeMode(2, QHeaderView::Stretch);

        for (const AppInfo::LibraryInfo& lib : AppInfo::kUsedLibraries) {
            auto* item = new QTreeWidgetItem(libraryList_);
            QString displayName = QString::fromUtf8(lib.name);
            if (lib.version && lib.version[0] != '\0') {
                displayName += QStringLiteral(" (%1)").arg(QString::fromUtf8(lib.version));
            }
            item->setText(0, displayName);
            item->setText(1, QString::fromUtf8(lib.license));
            item->setText(2, QString::fromUtf8(lib.description));
            if (lib.url && lib.url[0] != '\0') {
                const QString url = QString::fromUtf8(lib.url);
                item->setData(0, Qt::UserRole, url);
                item->setToolTip(0, url);
                item->setToolTip(2, url);
            }
        }
        libLayout->addWidget(libraryList_, 1);

        auto* libHint = new QLabel(QStringLiteral("항목을 더블클릭하면 프로젝트 웹 페이지를 엽니다."), libGroup);
        libLayout->addWidget(libHint);
        layout->addWidget(libGroup, 1);

        connect(libraryList_, &QTreeWidget::itemDoubleClicked, this, [](QTreeWidgetItem* item, int) {
            if (!item) {
                return;
            }
            const QString url = item->data(0, Qt::UserRole).toString();
            if (!url.isEmpty()) {
                QDesktopServices::openUrl(QUrl(url));
            }
        });

        return page;
    }

    void FillFontList() {
        std::set<QString> names;
        for (const QString& family : QFontDatabase::families()) {
            if (!family.startsWith(QLatin1Char('@')) && QFontDatabase::isSmoothlyScalable(family)) {
                names.insert(family);
            }
        }
        if (!working_.subtitleStyle.font.isEmpty()) {
            names.insert(working_.subtitleStyle.font);
        }

        fontCombo_->addItem(QString::fromUtf8(kDefaultFontLabel));
        int selected = 0;
        for (const QString& name : names) {
            fontCombo_->addItem(name);
            if (name == working_.subtitleStyle.font) {
                selected = fontCombo_->count() - 1;
            }
        }
        fontCombo_->setCurrentIndex(selected);
    }

    void FillOutlineList() {
        for (size_t i = 0; i < kSubtitleOutlineSizes.size(); ++i) {
            const double size = kSubtitleOutlineSizes[i];
            QString label;
            if (size == 0.0) {
                label = QStringLiteral("없음");
            } else if (size == kDefaultSubtitleOutlineSize) {
                label = QString::asprintf("%g (기본)", size);
            } else {
                label = QString::asprintf("%g", size);
            }
            outlineCombo_->addItem(label);
            if (size == working_.subtitleStyle.outlineSize) {
                outlineCombo_->setCurrentIndex(static_cast<int>(i));
            }
        }
    }

    void UpdatePreview() {
        QFont font = previewLabel_->font();
        if (!working_.subtitleStyle.font.isEmpty()) {
            font.setFamily(working_.subtitleStyle.font);
        } else {
            font.setFamily(QApplication::font().family());
        }
        font.setPointSize(kPreviewPointSize);
        previewLabel_->setFont(font);
    }

    void UpdateAssociationState() {
        const bool registered = IsFileAssociationRegistered();
        assocStatus_->setText(registered ? QStringLiteral("상태: 등록됨") : QStringLiteral("상태: 등록 안 됨"));
#if defined(Q_OS_MACOS)
        assocRegisterBtn_->setEnabled(false);
        assocUnregisterBtn_->setEnabled(false);
        assocDefaultsBtn_->setEnabled(true);
#else
        assocRegisterBtn_->setEnabled(!registered);
        assocUnregisterBtn_->setEnabled(registered);
        assocDefaultsBtn_->setEnabled(registered);
#endif
    }

    void FillShortcutList() {
        if (shortcutList_->topLevelItemCount() == 0) {
            for (size_t i = 0; i < kShortcutCount; ++i) {
                auto* item = new QTreeWidgetItem(shortcutList_);
                item->setText(0, QString::fromUtf8(AllShortcuts()[i].label));
            }
        }
        for (int row = 0; row < static_cast<int>(kShortcutCount); ++row) {
            ShowShortcutRow(row);
        }
    }

    void ShowShortcutRow(int row) {
        QTreeWidgetItem* item = shortcutList_->topLevelItem(row);
        if (!item) {
            return;
        }
        const KeyChord chord = working_.shortcuts[static_cast<size_t>(row)];
        item->setText(1, chord == kNoKey ? QStringLiteral("없음") : KeyChordName(chord));
    }

    int SelectedShortcutRow() const {
        QTreeWidgetItem* item = shortcutList_->currentItem();
        return item ? shortcutList_->indexOfTopLevelItem(item) : -1;
    }

    void UpdateShortcutButtons() {
        const int row = SelectedShortcutRow();
        shortcutChangeBtn_->setEnabled(row >= 0);
        shortcutClearBtn_->setEnabled(row >= 0 && working_.shortcuts[static_cast<size_t>(row)] != kNoKey);
    }

    void StartCapture(int row) {
        if (capturing_) {
            return;
        }
        capturing_ = true;
        capturingRow_ = row;
        if (QTreeWidgetItem* item = shortcutList_->topLevelItem(row)) {
            item->setText(1, QStringLiteral("키를 누르세요..."));
        }
        shortcutStatus_->setText(QStringLiteral("새 키를 누르세요. 다른 곳을 클릭하면 취소됩니다."));
        shortcutList_->setFocus();
        qApp->installEventFilter(this);
    }

    void StopCapture(bool restoreRowText) {
        if (!capturing_) {
            return;
        }
        qApp->removeEventFilter(this);
        const int row = capturingRow_;
        capturing_ = false;
        capturingRow_ = -1;
        if (restoreRowText && row >= 0) {
            ShowShortcutRow(row);
            shortcutStatus_->setText(QString::fromUtf8(kShortcutHint));
        }
    }

    void AssignCapturedChord(KeyChord chord) {
        const int row = capturingRow_;
        StopCapture(false);
        if (row < 0) {
            return;
        }

        QString status = QString::fromUtf8(kShortcutHint);
        const size_t index = static_cast<size_t>(row);
        const std::optional<ShortcutAction> taken = FindShortcut(working_.shortcuts, chord);
        if (taken && static_cast<size_t>(*taken) != index) {
            working_.shortcuts[static_cast<size_t>(*taken)] = kNoKey;
            ShowShortcutRow(static_cast<int>(*taken));
            status = QStringLiteral("%1 키를 \"%2\"에서 옮겼습니다.")
                         .arg(KeyChordName(chord), QString::fromUtf8(GetShortcutInfo(*taken).label));
        }
        working_.shortcuts[index] = chord;
        ShowShortcutRow(row);
        shortcutStatus_->setText(status);
        UpdateShortcutButtons();
        UpdateApplyButton();
    }

    void UpdateApplyButton() {
        if (applyButton_) {
            applyButton_->setEnabled(!(working_ == applied_));
        }
    }

    void Apply() {
        if (!(working_ == applied_)) {
            apply_(working_);
            applied_ = working_;
        }
        UpdateApplyButton();
    }

    QWidget* owner_ = nullptr;
    Preferences working_;
    Preferences applied_;
    const std::function<void(const Preferences&)>& apply_;

    QTabWidget* tabs_ = nullptr;
    QSpinBox* seekSpin_ = nullptr;
    QCheckBox* playlistFitCheck_ = nullptr;
    QLabel* assocStatus_ = nullptr;
    QPushButton* assocRegisterBtn_ = nullptr;
    QPushButton* assocUnregisterBtn_ = nullptr;
    QPushButton* assocDefaultsBtn_ = nullptr;

    QComboBox* fontCombo_ = nullptr;
    QSpinBox* sizeSpin_ = nullptr;
    QComboBox* outlineCombo_ = nullptr;
    QSpinBox* heightSpin_ = nullptr;
    QLabel* previewLabel_ = nullptr;

    QTreeWidget* shortcutList_ = nullptr;
    QLabel* shortcutStatus_ = nullptr;
    QPushButton* shortcutChangeBtn_ = nullptr;
    QPushButton* shortcutClearBtn_ = nullptr;
    QPushButton* shortcutDefaultsBtn_ = nullptr;

    AppUpdater* updater_ = nullptr;
    ReleaseInfo latestRelease_;
    QLabel* updateStatus_ = nullptr;
    QProgressBar* updateProgress_ = nullptr;
    QPushButton* checkUpdateBtn_ = nullptr;
    QPushButton* applyUpdateBtn_ = nullptr;
    QTreeWidget* libraryList_ = nullptr;

    QPushButton* okButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QProxyStyle* buttonStyle_ = nullptr;

    bool loading_ = true;
    bool capturing_ = false;
    int capturingRow_ = -1;
};

}  // namespace

void ShowSettingsDialog(QWidget* owner, const Preferences& current, const std::function<void(const Preferences&)>& apply) {
    SettingsDialog dialog(owner, current, apply);
    dialog.exec();
}
