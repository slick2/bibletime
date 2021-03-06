/*********
*
* This file is part of BibleTime's source code, http://www.bibletime.info/.
*
* Copyright 1999-2014 by the BibleTime developers.
* The BibleTime source code is licensed under the GNU General Public License version 2.0.
*
**********/

#include "frontend/displaywindow/btmodulechooserbar.h"
#include "frontend/displaywindow/btmodulechooserbutton.h"
#include "creadwindow.h"
#include "util/btmodules.h"

#include <QAction>
#include <QDebug>
#include <QList>
#include <QToolBar>


BtModuleChooserBar::BtModuleChooserBar(QWidget *parent)
        : QToolBar(parent),
        BtWindowModuleChooser(CSwordModuleInfo::Unknown, 0),
        m_idCounter(0),
        m_window(0) {
    setAllowedAreas(Qt::TopToolBarArea);
    setFloatable(false);
}

void BtModuleChooserBar::slotBackendModulesChanged() {
    m_modules = m_window->getModuleList();

    adjustButtonCount();

    //recreate all menus from scratch
    for (int i = 0; i < m_buttonList.count(); i++) {
        BtModuleChooserButton* button = m_buttonList.at(i);
        QString moduleName = (i >= m_modules.count()) ? QString::null : m_modules.at(i);
        qDebug() << "refresh button's menu:" << moduleName << i;
        int leftLikeModules = leftLikeParallelModules(m_modules);
        button->recreateMenu(m_modules, moduleName, i, leftLikeModules);
    }
}

void BtModuleChooserBar::adjustButtonCount(bool adjustToZero) {
    int buttonCountDifference = 0;
    if (adjustToZero) {
        buttonCountDifference = m_buttonList.count();
    }
    else {
        buttonCountDifference = m_buttonList.count() - (m_modules.count() + 1);
    }
    if (m_moduleType == CSwordModuleInfo::GenericBook && !adjustToZero) {
        buttonCountDifference = (1 - m_buttonList.count()) * -1;
    }
    //if there are more buttons than modules, delete buttons
    if (buttonCountDifference > 0) {
        for (int j = 0; j < buttonCountDifference; j++) {
            delete m_buttonList.takeFirst();
        }
    }
    // if there are more modules than buttons, add buttons
    if (buttonCountDifference < 0) {
        for (int i = (buttonCountDifference * (-1)); i > 0; i--) {
            addButton();
        }
    }
}

void BtModuleChooserBar::slotWindowModulesChanged() {
    m_modules = m_window->getModuleList();
    adjustButtonCount();
    updateButtonMenus();
}

BtModuleChooserButton* BtModuleChooserBar::addButton() {
    BtModuleChooserButton* b = new BtModuleChooserButton(this, m_moduleType);
    QAction* a = addWidget(b);
    m_buttonList.append(b);

    // the button sends signals directly to the window which then signals back when the module
    // list has changed
    connect(b, SIGNAL(sigModuleAdd(int, QString)), m_window, SLOT(slotAddModule(int, QString)));
    connect(b, SIGNAL(sigModuleReplace(int, QString)), m_window, SLOT(slotReplaceModule(int, QString)));
    connect(b, SIGNAL(sigModuleRemove(int)), m_window, SLOT(slotRemoveModule(int)));

    a->setVisible(true);
    return b;
}


/** Sets the modules which are chosen in this module chooser bar. */
void BtModuleChooserBar::setModules( QStringList useModules,CSwordModuleInfo::ModuleType type, CReadWindow* window) {
    m_modules = useModules;
    m_window = window;
    m_moduleType = type;

    clear();

    adjustButtonCount(true);

    //if (!useModules.count()) return;
    for (int i = 0; i < useModules.count(); i++) {
        addButton();
    }
    if (!(m_moduleType == CSwordModuleInfo::GenericBook)) {
        addButton(); // for ADD button
    }
    updateButtonMenus();

    connect(m_window, SIGNAL(sigModuleListSet(QStringList)), SLOT(slotBackendModulesChanged()));
    connect(m_window, SIGNAL(sigModuleListChanged()), SLOT(slotWindowModulesChanged()));
}

void BtModuleChooserBar::updateButtonMenus() {
    int leftLikeModules = leftLikeParallelModules(m_modules);
    for (int i = 0; i < m_buttonList.count(); i++) {
        BtModuleChooserButton* button = m_buttonList.at(i);
        QString moduleName = (i >= m_modules.count()) ? QString::null : m_modules.at(i);
        button->updateMenu(m_modules, moduleName, i, leftLikeModules);
    }
}
