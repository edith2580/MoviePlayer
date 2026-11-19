#include "ContextMenu.h"

#include <QAction>
#include <QFileInfo>
#include <QMenu>

#include <array>
#include <functional>

#include "TimeFormat.h"

namespace {

constexpr std::array<double, 8> kSpeeds{0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0};
constexpr std::array<AspectMode, 5> kAspects{
    AspectMode::Original, AspectMode::Ratio16x9, AspectMode::Ratio4x3, AspectMode::Ratio235x100, AspectMode::Stretch,
};
constexpr std::array<int, 4> kRotations{0, 90, 180, 270};
constexpr std::array<VideoAdjustment, kVideoAdjustmentCount> kAdjustments{
    VideoAdjustment::Brightness, VideoAdjustment::Contrast, VideoAdjustment::Saturation, VideoAdjustment::Hue, VideoAdjustment::Gamma,
};
constexpr std::array<ShortcutAction, kVideoAdjustmentCount> kLowerActions{
    ShortcutAction::BrightnessDown, ShortcutAction::ContrastDown, ShortcutAction::SaturationDown, ShortcutAction::HueDown, ShortcutAction::GammaDown,
};
constexpr std::array<ShortcutAction, kVideoAdjustmentCount> kRaiseActions{
    ShortcutAction::BrightnessUp, ShortcutAction::ContrastUp, ShortcutAction::SaturationUp, ShortcutAction::HueUp, ShortcutAction::GammaUp,
};

// '&' marks a shortcut letter in menu text, so a literal one has to be doubled.
QString EscapeForMenu(const QString& text) {
    QString escaped = text;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&&"));
    return escaped;
}

// Shows the action's current key at the right of the menu item, if it has one.
QString WithKey(const QString& label, const ShortcutTable& shortcuts, ShortcutAction action) {
    const KeyChord chord = shortcuts[static_cast<size_t>(action)];
    return chord == kNoKey ? label : label + QLatin1Char('\t') + KeyChordName(chord);
}

QAction* AddItem(QMenu* menu, const QString& text, bool checked = false, bool enabled = true) {
    QAction* action = menu->addAction(text);
    action->setCheckable(checked);
    action->setChecked(checked);
    action->setEnabled(enabled);
    return action;
}

QAction* ExecAt(QMenu& menu, PopupPlace place) {
    QPoint pos = place.point;
    if (place.centered) {
        const QSize hint = menu.sizeHint();
        pos -= QPoint(hint.width() / 2, hint.height() / 2);
    }
    return menu.exec(pos);
}

}  // namespace

std::optional<size_t> ShowChapterMenu(QWidget* owner, PopupPlace place, const PlaybackState& state) {
    QMenu menu(owner);
    std::optional<size_t> picked;
    for (size_t i = 0; i < state.chapters.size(); ++i) {
        const Chapter& chapter = state.chapters[i];
        const bool current = state.chapter && *state.chapter == static_cast<int64_t>(i);
        QAction* act = AddItem(&menu, EscapeForMenu(chapter.title) + QLatin1Char('\t') + FormatTime(chapter.time), current);
        QObject::connect(act, &QAction::triggered, [&picked, i] { picked = i; });
    }
    ExecAt(menu, place);
    return picked;
}

PlaylistChoice ShowPlaylistMenu(QWidget* owner, PopupPlace place, const QStringList& files, size_t current,
                                bool autoAdvance, RepeatMode repeat) {
    QMenu menu(owner);
    PlaylistChoice choice;

    QObject::connect(AddItem(&menu, QStringLiteral("다음 파일 자동 재생"), autoAdvance), &QAction::triggered, [&] {
        choice.action = PlaylistChoice::Action::ToggleAutoAdvance;
    });
    QObject::connect(AddItem(&menu, QStringLiteral("한 개 반복"), repeat == RepeatMode::One), &QAction::triggered, [&] {
        choice.action = PlaylistChoice::Action::ToggleRepeatOne;
    });
    QObject::connect(AddItem(&menu, QStringLiteral("전체 목록 반복"), repeat == RepeatMode::All), &QAction::triggered, [&] {
        choice.action = PlaylistChoice::Action::ToggleRepeatAll;
    });
    menu.addSeparator();

    if (files.isEmpty()) {
        AddItem(&menu, QStringLiteral("재생 목록이 비어 있습니다"), false, false);
    }
    for (qsizetype i = 0; i < files.size(); ++i) {
        const QString name = QFileInfo(files[i]).fileName();
        const size_t idx = static_cast<size_t>(i);
        QAction* act = AddItem(&menu, QString::number(i + 1) + QStringLiteral(". ") + EscapeForMenu(name), idx == current);
        QObject::connect(act, &QAction::triggered, [&choice, idx] {
            choice.action = PlaylistChoice::Action::Play;
            choice.index = idx;
        });
    }
    ExecAt(menu, place);
    return choice;
}

QString AspectLabel(AspectMode aspect) {
    switch (aspect) {
    case AspectMode::Ratio16x9:
        return QStringLiteral("16:9");
    case AspectMode::Ratio4x3:
        return QStringLiteral("4:3");
    case AspectMode::Ratio235x100:
        return QStringLiteral("2.35:1");
    case AspectMode::Stretch:
        return QStringLiteral("창에 맞춰 늘리기");
    case AspectMode::Original:
        break;
    }
    return QStringLiteral("원본 비율");
}

QString VideoAdjustmentLabel(VideoAdjustment adjustment) {
    switch (adjustment) {
    case VideoAdjustment::Contrast:
        return QStringLiteral("대비");
    case VideoAdjustment::Saturation:
        return QStringLiteral("채도");
    case VideoAdjustment::Hue:
        return QStringLiteral("색조");
    case VideoAdjustment::Gamma:
        return QStringLiteral("감마");
    case VideoAdjustment::Brightness:
        break;
    }
    return QStringLiteral("밝기");
}

QString TrackLabel(const MediaTrack& track) {
    QString label;
    if (!track.externalFileName.isEmpty()) {
        label = track.externalFileName + QStringLiteral(" (외부)");
    } else {
        label = !track.title.isEmpty()      ? track.title
              : !track.language.isEmpty()   ? track.language
                                            : QStringLiteral("트랙 ") + QString::number(track.id);
        if (!track.title.isEmpty() && !track.language.isEmpty()) {
            label += QStringLiteral(" [") + track.language + QStringLiteral("]");
        }
        if (!track.codec.isEmpty()) {
            label += QStringLiteral(" (") + track.codec + QStringLiteral(")");
        }
    }
    if (track.channels > 0) {
        label += QLatin1Char(' ') + QString::number(track.channels) + QStringLiteral("ch");
    }
    return label;
}

MenuChoice ShowContextMenu(QWidget* owner, QPoint screenPoint, const PlaybackState& state, const ShortcutTable& shortcuts) {
    const bool fileOpen = !state.fileName.isEmpty();
    QMenu menu(owner);
    MenuChoice choice;

    QObject::connect(AddItem(&menu, QStringLiteral("파일 열기...")), &QAction::triggered, [&] {
        choice.action = MenuChoice::Action::OpenVideo;
    });
    menu.addSeparator();

    // Subtitle menu
    QMenu* subMenu = menu.addMenu(QStringLiteral("자막"));
    bool anySubSelected = false;
    for (const MediaTrack& track : state.tracks) {
        anySubSelected = anySubSelected || (track.type == TrackType::Subtitle && track.selected);
    }
    QObject::connect(AddItem(subMenu, QStringLiteral("자막 끄기"), !anySubSelected, fileOpen), &QAction::triggered, [&] {
        choice.action = MenuChoice::Action::SelectSubtitle;
        choice.trackId = std::nullopt;
    });
    for (const MediaTrack& track : state.tracks) {
        if (track.type == TrackType::Subtitle) {
            const int64_t id = track.id;
            QObject::connect(AddItem(subMenu, EscapeForMenu(TrackLabel(track)), track.selected), &QAction::triggered, [&, id] {
                choice.action = MenuChoice::Action::SelectSubtitle;
                choice.trackId = id;
            });
        }
    }
    subMenu->addSeparator();
    QObject::connect(AddItem(subMenu, QStringLiteral("자막 파일 열기..."), false, fileOpen), &QAction::triggered, [&] {
        choice.action = MenuChoice::Action::OpenSubtitle;
    });
    const QString subDelayText = QString::asprintf("싱크 초기화 (현재 %+.1f초)", state.subtitleDelay);
    QObject::connect(AddItem(subMenu, subDelayText, false, state.subtitleDelay != 0.0), &QAction::triggered, [&] {
        choice.action = MenuChoice::Action::ResetSubtitleDelay;
    });
    QMenu* encodingMenu = subMenu->addMenu(QStringLiteral("자막 인코딩"));
    QObject::connect(AddItem(encodingMenu, QStringLiteral("한국어 (CP949)"), state.subtitleEncoding == SubtitleEncoding::Korean),
                     &QAction::triggered, [&] {
                         choice.action = MenuChoice::Action::SetSubtitleEncoding;
                         choice.encoding = SubtitleEncoding::Korean;
                     });
    QObject::connect(AddItem(encodingMenu, QStringLiteral("자동 감지"), state.subtitleEncoding == SubtitleEncoding::Auto),
                     &QAction::triggered, [&] {
                         choice.action = MenuChoice::Action::SetSubtitleEncoding;
                         choice.encoding = SubtitleEncoding::Auto;
                     });

    // Audio menu
    bool hasAudio = false;
    for (const MediaTrack& track : state.tracks) {
        if (track.type == TrackType::Audio) {
            hasAudio = true;
            break;
        }
    }
    QMenu* audioMenu = menu.addMenu(QStringLiteral("오디오"));
    audioMenu->setEnabled(fileOpen && hasAudio);
    for (const MediaTrack& track : state.tracks) {
        if (track.type == TrackType::Audio) {
            const int64_t id = track.id;
            QObject::connect(AddItem(audioMenu, EscapeForMenu(TrackLabel(track)), track.selected), &QAction::triggered, [&, id] {
                choice.action = MenuChoice::Action::SelectAudio;
                choice.trackId = id;
            });
        }
    }
    audioMenu->addSeparator();
    const QString audioDelayText = QString::asprintf("음성 싱크 초기화 (현재 %+.1f초)", state.audioDelay);
    QObject::connect(AddItem(audioMenu, audioDelayText, false, state.audioDelay != 0.0), &QAction::triggered, [&] {
        choice.action = MenuChoice::Action::ResetAudioDelay;
    });

    // Playback menu
    QMenu* playbackMenu = menu.addMenu(QStringLiteral("재생"));
    QMenu* speedMenu = playbackMenu->addMenu(QStringLiteral("배속"));
    for (double spd : kSpeeds) {
        const QString label = QString::asprintf("%g배", spd);
        QObject::connect(AddItem(speedMenu, label, state.speed == spd), &QAction::triggered, [&, spd] {
            choice.action = MenuChoice::Action::SetSpeed;
            choice.speed = spd;
        });
    }
    playbackMenu->addSeparator();
    QObject::connect(AddItem(playbackMenu, WithKey(QStringLiteral("구간 반복 시작 지정"), shortcuts, ShortcutAction::LoopStart), false, fileOpen),
                     &QAction::triggered, [&] { choice.action = MenuChoice::Action::LoopStart; });
    QObject::connect(AddItem(playbackMenu, WithKey(QStringLiteral("구간 반복 끝 지정"), shortcuts, ShortcutAction::LoopEnd), false, fileOpen),
                     &QAction::triggered, [&] { choice.action = MenuChoice::Action::LoopEnd; });
    QObject::connect(AddItem(playbackMenu, WithKey(QStringLiteral("구간 반복 해제"), shortcuts, ShortcutAction::LoopClear), false,
                             state.loopStart.has_value() || state.loopEnd.has_value()),
                     &QAction::triggered, [&] { choice.action = MenuChoice::Action::ClearLoop; });
    playbackMenu->addSeparator();
    QObject::connect(AddItem(playbackMenu, WithKey(QStringLiteral("다음 프레임"), shortcuts, ShortcutAction::FrameForward), false, fileOpen),
                     &QAction::triggered, [&] { choice.action = MenuChoice::Action::StepForward; });
    QObject::connect(AddItem(playbackMenu, WithKey(QStringLiteral("이전 프레임"), shortcuts, ShortcutAction::FrameBackward), false, fileOpen),
                     &QAction::triggered, [&] { choice.action = MenuChoice::Action::StepBackward; });

    // Video menu
    QMenu* videoMenu = menu.addMenu(QStringLiteral("영상"));
    QMenu* aspectMenu = videoMenu->addMenu(QStringLiteral("화면 비율"));
    for (AspectMode asp : kAspects) {
        QObject::connect(AddItem(aspectMenu, AspectLabel(asp), state.aspect == asp), &QAction::triggered, [&, asp] {
            choice.action = MenuChoice::Action::SetAspect;
            choice.aspect = asp;
        });
    }
    QMenu* rotationMenu = videoMenu->addMenu(QStringLiteral("회전"));
    for (int rot : kRotations) {
        const QString label = rot == 0 ? QStringLiteral("회전 안 함") : QString::number(rot) + QStringLiteral("도");
        QObject::connect(AddItem(rotationMenu, label, state.rotation == rot), &QAction::triggered, [&, rot] {
            choice.action = MenuChoice::Action::SetRotation;
            choice.rotation = rot;
        });
    }
    QMenu* adjustMenu = videoMenu->addMenu(QStringLiteral("영상 조정"));
    bool anyAdjusted = false;
    for (size_t i = 0; i < kAdjustments.size(); ++i) {
        const int value = state.adjustments[i];
        anyAdjusted = anyAdjusted || value != 0;
        const VideoAdjustment adj = kAdjustments[i];
        const QString name = VideoAdjustmentLabel(adj);
        const QString lower = QStringLiteral("%1 낮추기 (현재 %2)").arg(name, QString::asprintf("%+d", value));
        const QString raise = QStringLiteral("%1 높이기 (현재 %2)").arg(name, QString::asprintf("%+d", value));
        QObject::connect(AddItem(adjustMenu, WithKey(lower, shortcuts, kLowerActions[i])), &QAction::triggered, [&, adj] {
            choice.action = MenuChoice::Action::AdjustVideo;
            choice.adjustment = adj;
            choice.adjustmentDirection = -1;
        });
        QObject::connect(AddItem(adjustMenu, WithKey(raise, shortcuts, kRaiseActions[i])), &QAction::triggered, [&, adj] {
            choice.action = MenuChoice::Action::AdjustVideo;
            choice.adjustment = adj;
            choice.adjustmentDirection = 1;
        });
    }
    adjustMenu->addSeparator();
    QObject::connect(AddItem(adjustMenu, WithKey(QStringLiteral("모두 초기화"), shortcuts, ShortcutAction::VideoReset), false, anyAdjusted),
                     &QAction::triggered, [&] { choice.action = MenuChoice::Action::ResetVideo; });

    menu.addSeparator();
    QObject::connect(AddItem(&menu, WithKey(QStringLiteral("설정..."), shortcuts, ShortcutAction::OpenSettings)),
                     &QAction::triggered, [&] { choice.action = MenuChoice::Action::OpenSettings; });

    menu.exec(screenPoint);
    return choice;
}
