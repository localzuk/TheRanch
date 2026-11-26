// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See aboutlibraries.h.
//
// Keeping this list accurate: every library in target_link_libraries() and every
// ecm_find_qmlmodule() in CMakeLists.txt belongs here. Licence terms are taken from
// each project's SPDX headers / LICENSES folder; the buttons offer the full text of
// each licence a library's own code is under (not the CC0/BSD build-system files).

#include "aboutlibraries.h"

#include <KAboutData>
#include <KLocalizedString>

#include <kconfig_version.h>
#include <kcoreaddons_version.h>
#include <kdbusaddons_version.h>
#include <kglobalaccel_version.h>
#include <ki18n_version.h>
#include <kio_version.h>
#include <kstatusnotifieritem_version.h>
#include <kxmlgui_version.h>

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

using namespace Qt::StringLiterals;

namespace
{

struct Library {
    QString name;
    QString version; // empty for QML modules loaded at run time
    QString description; // what it is and what The Ranch uses it for
    QString copyright;
    QString terms; // the licence terms, in words
    QList<KAboutLicense::LicenseKey> licences; // one "show text" button each
    QString website;
};

QList<Library> libraries()
{
    // KDE Frameworks share copyright attribution and (mostly) licence terms. Files are
    // individually licensed; most are LGPL-2.0-or-later, newer ones LGPL-2.1-only OR
    // LGPL-3.0-only (or a later version approved by KDE e.V.).
    const QString kdeCopyright = i18n("Copyright © The KDE community and contributors");
    const QString kfTerms = i18n(
        "GNU LGPL. Most files are LGPL-2.0-or-later; some are LGPL-2.0-only, or LGPL-2.1-only OR LGPL-3.0-only.");
    const QList<KAboutLicense::LicenseKey> kfLicences{KAboutLicense::LGPL_V2, KAboutLicense::LGPL_V2_1, KAboutLicense::LGPL_V3};
    const auto kf = [&](const QString &name, const char *version, const QString &description, const QString &repo) {
        return Library{name, QString::fromLatin1(version), description, kdeCopyright, kfTerms, kfLicences,
                       u"https://invent.kde.org/frameworks/"_s + repo};
    };

    return {
        Library{u"Qt"_s,
                QString::fromLatin1(qVersion()),
                i18n("Cross-platform application framework. The Ranch uses Qt Core, GUI, Widgets, QML, Quick, "
                     "Quick Controls, Quick Dialogs, Quick Effects and Wayland."),
                i18n("Copyright © The Qt Company Ltd. and other contributors"),
                i18n("Used under the GNU LGPL version 3. Qt is available under LGPL-3.0-only OR GPL-2.0-only OR "
                     "GPL-3.0-only, or a commercial licence."),
                {KAboutLicense::LGPL_V3, KAboutLicense::GPL_V2, KAboutLicense::GPL_V3},
                u"https://www.qt.io/"_s},
        kf(u"KCoreAddons"_s, KCOREADDONS_VERSION_STRING, i18n("Core utilities from KDE Frameworks; holds the application's About information."), u"kcoreaddons"_s),
        kf(u"KI18n"_s, KI18N_VERSION_STRING, i18n("Translation and localisation."), u"ki18n"_s),
        kf(u"KIO"_s,
           KIO_VERSION_STRING,
           i18n("Opening, moving, copying, renaming and deleting files, the file context menu and the Properties dialog."),
           u"kio"_s),
        kf(u"KDBusAddons"_s, KDBUSADDONS_VERSION_STRING, i18n("Keeps a single instance of The Ranch running."), u"kdbusaddons"_s),
        Library{u"KGlobalAccel"_s,
                QString::fromLatin1(KGLOBALACCEL_VERSION_STRING),
                i18n("System-wide keyboard shortcuts."),
                kdeCopyright,
                i18n("GNU LGPL, version 2 or (at your option) any later version (LGPL-2.0-or-later)."),
                {KAboutLicense::LGPL_V2},
                u"https://invent.kde.org/frameworks/kglobalaccel"_s},
        Library{u"KStatusNotifierItem"_s,
                QString::fromLatin1(KSTATUSNOTIFIERITEM_VERSION_STRING),
                i18n("The system tray icon and its menu."),
                kdeCopyright,
                i18n("GNU LGPL, version 2 or (at your option) any later version (LGPL-2.0-or-later)."),
                {KAboutLicense::LGPL_V2},
                u"https://invent.kde.org/frameworks/kstatusnotifieritem"_s},
        kf(u"KConfig"_s, KCONFIG_VERSION_STRING, i18n("Settings storage, used by the KDE libraries listed here."), u"kconfig"_s),
        kf(u"KXmlGui"_s, KXMLGUI_VERSION_STRING, i18n("This About dialog and the Configure Shortcuts dialog."), u"kxmlgui"_s),
        kf(u"Kirigami"_s, "", i18n("QML components and Plasma theme colours, sizes and icons for the corrals and dialogs."), u"kirigami"_s),
        Library{u"QQC2 Desktop Style"_s,
                QString(),
                i18n("Makes The Ranch's buttons, menus and fields look like the rest of Plasma (optional)."),
                kdeCopyright,
                i18n("GNU LGPL. Most files are LGPL-2.0-or-later, LGPL-2.1-or-later or LGPL-3.0-only; some are "
                     "GPL-2.0-or-later or GPL-3.0-only."),
                {KAboutLicense::LGPL_V2, KAboutLicense::LGPL_V2_1, KAboutLicense::LGPL_V3, KAboutLicense::GPL_V2, KAboutLicense::GPL_V3},
                u"https://invent.kde.org/frameworks/qqc2-desktop-style"_s},
        Library{u"LayerShellQt"_s,
                QStringLiteral(THERANCH_LAYERSHELLQT_VERSION), // from CMakeLists.txt
                i18n("Places corrals on the desktop and drawers above windows, using the wlr-layer-shell Wayland protocol."),
                i18n("Copyright © Drew DeVault, Aleix Pol Gonzalez and the KDE community"),
                i18n("GNU LGPL. Files are LGPL-2.1-only OR LGPL-3.0-only, or LGPL-3.0-or-later."),
                {KAboutLicense::LGPL_V2_1, KAboutLicense::LGPL_V3},
                u"https://invent.kde.org/plasma/layer-shell-qt"_s},
    };
}

QString licenceName(KAboutLicense::LicenseKey key, KAboutLicense::NameFormat format)
{
    const KAboutData data(u"theranch-licence"_s, QString(), QString(), QString(), key);
    return data.licenses().constFirst().name(format);
}

// The licence text KCoreAddons ships, minus the "This program is distributed under
// the terms of…" line it starts with: in this dialog, "this program" would read as
// The Ranch rather than the library.
QString licenceText(KAboutLicense::LicenseKey key)
{
    const KAboutData data(u"theranch-licence"_s, QString(), QString(), QString(), key);
    QString text = data.licenses().constFirst().text();
    static const QRegularExpression title(uR"(^[ \t]*GNU (LIBRARY|LESSER|GENERAL) )"_s, QRegularExpression::MultilineOption);
    if (const auto match = title.match(text); match.hasMatch()) {
        text = text.mid(match.capturedStart());
    }
    return text;
}

// A plain, resizable window with one licence's full text.
QDialog *createLicenceWindow(KAboutLicense::LicenseKey key, QWidget *parent)
{
    auto *dialog = new QDialog(parent, Qt::Window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const QString name = licenceName(key, KAboutLicense::FullName);
    dialog->setWindowTitle(i18nc("@title:window", "%1 — The Ranch", name));

    QString text = licenceText(key);
    // The LGPL v3 is a set of extra permissions on top of the GPL v3 and isn't
    // complete without it, so show both.
    if (key == KAboutLicense::LGPL_V3) {
        text += u"\n\n"_s + QString(72, u'=') + u"\n\n"_s
            + i18n("The GNU LGPL version 3 incorporates the terms of the GNU GPL version 3, which follows.") + u"\n\n"_s
            + licenceText(KAboutLicense::GPL_V3);
    }

    auto *view = new QPlainTextEdit(text, dialog);
    view->setReadOnly(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    // Wide enough for the licences' 80-column lines.
    const QFontMetrics metrics(view->font());
    const int width = metrics.horizontalAdvance(QString(82, u'M')) + view->verticalScrollBar()->sizeHint().width();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);

    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(view);
    layout->addWidget(buttons);
    dialog->resize(width + 2 * layout->contentsMargins().left(), metrics.lineSpacing() * 40);
    return dialog;
}

QLabel *wrappingLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

QWidget *AboutLibraries::createPage(QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    layout->addWidget(wrappingLabel(i18n("The Ranch is built on the following free software. Each library is the work of "
                                         "its authors and is used under the licence shown. Its source code is available "
                                         "from its website."),
                                    page));

    // One window per licence: clicking the same licence again (from any library)
    // brings its open window forward instead of opening another.
    auto openWindows = std::make_shared<QHash<int, QPointer<QDialog>>>();
    const auto showLicence = [page, openWindows](KAboutLicense::LicenseKey key) {
        QPointer<QDialog> &window = (*openWindows)[key];
        if (!window) {
            window = createLicenceWindow(key, page->window());
        }
        window->show();
        window->raise();
        window->activateWindow();
    };

    for (const Library &library : libraries()) {
        auto *separator = new QFrame(page);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        layout->addWidget(separator);

        auto *name = new QLabel(page);
        name->setTextFormat(Qt::RichText);
        name->setText(u"<b>%1</b>"_s.arg(library.name.toHtmlEscaped())
                      + (library.version.isEmpty() ? QString() : u" &nbsp;"_s + i18nc("library version", "version %1", library.version).toHtmlEscaped()));
        layout->addWidget(name);

        layout->addWidget(wrappingLabel(library.description, page));
        layout->addWidget(wrappingLabel(library.copyright, page));
        layout->addWidget(wrappingLabel(i18nc("@label", "Licence: %1", library.terms), page));

        auto *buttons = new QHBoxLayout;
        for (const KAboutLicense::LicenseKey key : library.licences) {
            auto *button = new QPushButton(QIcon::fromTheme(u"license"_s, QIcon::fromTheme(u"text-x-generic"_s)),
                                           licenceName(key, KAboutLicense::ShortName),
                                           page);
            button->setToolTip(i18nc("@info:tooltip", "Show the full text of the %1", licenceName(key, KAboutLicense::FullName)));
            QObject::connect(button, &QPushButton::clicked, page, [showLicence, key] {
                showLicence(key);
            });
            buttons->addWidget(button);
        }
        buttons->addStretch();
        auto *web = new QPushButton(QIcon::fromTheme(u"internet-services-symbolic"_s), i18nc("@action:button", "Website"), page);
        web->setToolTip(i18nc("@info:tooltip", "Visit %1's website\n%2", library.name, library.website));
        QObject::connect(web, &QPushButton::clicked, page, [url = QUrl(library.website)] {
            QDesktopServices::openUrl(url);
        });
        buttons->addWidget(web);
        layout->addLayout(buttons);
    }

    layout->addStretch();
    scroll->setWidget(page);
    // With horizontal scrolling off, a page wider than the viewport is cut off
    // behind the vertical scroll bar. Ask for room for the widest row (a library's
    // licence buttons plus Website) and the scroll bar, so the dialog grows to fit.
    scroll->setMinimumWidth(page->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width()
                            + 2 * scroll->frameWidth());
    return scroll;
}
