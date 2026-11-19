#include "InfoDialog.h"

#include <QDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPushButton>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>
#include <vector>

#include "PlaybackInfo.h"

namespace {

constexpr int kRefreshMs = 1000;

class InfoDialog : public QDialog {
public:
    InfoDialog(QWidget* owner, const MpvPlayer& player, const PlaybackState& state, KeyChord closeKey)
        : QDialog(owner), owner_(owner), player_(player), state_(state), closeKey_(closeKey) {
        setWindowTitle(QStringLiteral("재생 정보"));
        setModal(true);
        setMinimumSize(540, 420);

        auto* mainLayout = new QVBoxLayout(this);

        tabBar_ = new QTabBar(this);
        tabBar_->addTab(QStringLiteral("재생 정보"));
        tabBar_->addTab(QStringLiteral("파일 정보"));
        tabBar_->addTab(QStringLiteral("시스템 정보"));
        tabBar_->setExpanding(false);
        mainLayout->addWidget(tabBar_);

        list_ = new QTreeWidget(this);
        list_->setColumnCount(2);
        list_->setHeaderLabels({QStringLiteral("항목"), QStringLiteral("값")});
        list_->setRootIsDecorated(false);
        list_->setSelectionMode(QAbstractItemView::SingleSelection);
        list_->setUniformRowHeights(true);
        list_->header()->setStretchLastSection(true);
        list_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        list_->header()->resizeSection(0, 170);
        mainLayout->addWidget(list_, 1);

        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch(1);
        auto* closeBtn = new QPushButton(QStringLiteral("닫기"), this);
        closeBtn->setDefault(true);
        btnRow->addWidget(closeBtn);
        mainLayout->addLayout(btnRow);

        connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(tabBar_, &QTabBar::currentChanged, this, [this] {
            Refresh();
            if (list_->topLevelItemCount() > 0) {
                list_->scrollToItem(list_->topLevelItem(0));
            }
        });

        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &InfoDialog::Refresh);
        timer_->start(kRefreshMs);

        Refresh();
        tabBar_->setFocus();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (closeKey_ != kNoKey && ChordFromKeyEvent(event) == closeKey_) {
            reject();
            return;
        }
        QDialog::keyPressEvent(event);
    }

private:
    void Refresh() {
        switch (tabBar_->currentIndex()) {
        case 1:
            ShowRows(BuildFileRows(player_, state_));
            break;
        case 2:
            ShowRows(BuildSystemRows(player_, owner_));
            break;
        default:
            ShowRows(BuildPlaybackRows(player_, state_));
            break;
        }
    }

    void ShowRows(std::vector<InfoRow> rows) {
        // Usually only values change. Updating just those keeps the scroll position and selection.
        bool sameLayout = rows.size() == shown_.size() && rows.size() == valueItems_.size();
        for (size_t i = 0; sameLayout && i < rows.size(); ++i) {
            sameLayout = rows[i].group == shown_[i].group && rows[i].label == shown_[i].label;
        }
        if (sameLayout) {
            for (size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].value != shown_[i].value && valueItems_[i]) {
                    valueItems_[i]->setText(1, rows[i].value);
                    valueItems_[i]->setToolTip(1, rows[i].value);
                }
            }
        } else {
            list_->setUpdatesEnabled(false);
            list_->clear();
            valueItems_.clear();
            valueItems_.reserve(rows.size());

            QString currentGroup;
            for (const InfoRow& row : rows) {
                if (row.group != currentGroup) {
                    currentGroup = row.group;
                    auto* groupHeader = new QTreeWidgetItem(list_);
                    groupHeader->setText(0, currentGroup);
                    groupHeader->setFirstColumnSpanned(true);
                    groupHeader->setFlags(Qt::ItemIsEnabled);
                    QFont bold = groupHeader->font(0);
                    bold.setBold(true);
                    groupHeader->setFont(0, bold);
                }
                auto* item = new QTreeWidgetItem(list_);
                item->setText(0, QStringLiteral("  ") + row.label);
                item->setText(1, row.value);
                item->setToolTip(0, row.label);
                item->setToolTip(1, row.value);
                valueItems_.push_back(item);
            }
            list_->setUpdatesEnabled(true);
        }
        shown_ = std::move(rows);
    }

    QWidget* owner_ = nullptr;
    const MpvPlayer& player_;
    const PlaybackState& state_;
    KeyChord closeKey_;
    QTabBar* tabBar_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QTimer* timer_ = nullptr;
    std::vector<InfoRow> shown_;
    std::vector<QTreeWidgetItem*> valueItems_;
};

}  // namespace

void ShowInfoDialog(QWidget* owner, const MpvPlayer& player, const PlaybackState& state, KeyChord closeKey) {
    InfoDialog dialog(owner, player, state, closeKey);
    dialog.exec();
}
