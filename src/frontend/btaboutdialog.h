/*********
*
* In the name of the Father, and of the Son, and of the Holy Spirit.
*
* This file is part of BibleTime's source code, http://www.bibletime.info/.
*
* Copyright 1999-2014 by the BibleTime developers.
* The BibleTime source code is licensed under the GNU General Public License version 2.0.
*
**********/

#ifndef BTABOUTDIALOG_H
#define BTABOUTDIALOG_H

#include <QDialog>

class QDialogButtonBox;
class QTabWidget;
class QUrl;
class QWebView;
class QLabel;

class BtAboutDialog: public QDialog {
        Q_OBJECT
    public:
        BtAboutDialog(QWidget *parent = 0, Qt::WindowFlags wflags = Qt::Dialog);

    protected:
        virtual void resizeEvent(QResizeEvent* event);

    private:
        void initTab(QWebView *&tab);

        void retranslateUi();
        void retranslateBtTab();
        void retranslateContributorsTab();
        void retranslateSwordTab();
        void retranslateQtTab();
        void retranslateLicenceTab();

    private slots:
        void linkClicked(const QUrl &url);

    private:
        QTabWidget *m_tabWidget;
        QWebView *m_bibletimeTab;
        QWebView *m_contributorsTab;
        QWebView *m_swordTab;
        QWebView *m_qtTab;
        QWebView *m_licenceTab;
        QDialogButtonBox *m_buttonBox;
        QLabel *m_iconLabel;
        QLabel *m_versionLabel;
};

#endif
