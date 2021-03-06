/*********
*
* In the name of the Father, and of the Son, and of the Holy Spirit.
*
* This file is part of BibleTime's source code, http://www.bibletime.info/.
*
* Copyright 1999-2014 by the BibleTime developers.
* The BibleTime source code is licensed under the GNU General Public License
* version 2.0.
*
**********/


/**
  Total change list that should be applied after refactoring complete:
    non latin bookmark titles shown with unrecognized symbols
    feature request: hold Shift and Ctrl upon dragging item
    move loader to private class
    add ability to create bookmarks data with setData/insertRows
    unrecognized characters increaases in size file each save/load
    root folder for bookmarks
*/

#include "btbookmarksmodel.h"

#include <QtDebug>

#include <QDomElement>
#include <QDomNode>
#include <QFile>
#include <QIODevice>
#include <QList>
#include <QString>
#include <QTextCodec>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QSharedPointer>

#include "bibletimeapp.h"
#include "backend/drivers/cswordmoduleinfo.h"
#include "backend/config/btconfig.h"
#include "backend/managers/cswordbackend.h"
#include "backend/drivers/cswordmoduleinfo.h"
#include "backend/keys/cswordversekey.h"
#include "bibletimeapp.h"
#include "btglobal.h"
#include "util/cresmgr.h"
#include "util/geticon.h"
#include "util/tool.h"
#include "util/directory.h"


#define CURRENT_SYNTAX_VERSION 1


class BtBookmarksModelPrivate {

public: /* Tyepes */

    class BookmarkItemBase {

    public: /* Methods: */

        inline BookmarkItemBase(BookmarkItemBase * parent = 0)
            : m_parent(parent) {
            if(m_parent) {
                Q_ASSERT(!m_parent->m_children.contains(this));
                m_parent->m_children.append(this);
            }
        }
        BookmarkItemBase(const BookmarkItemBase & other)
            : m_flags(other.m_flags)
            , m_icon(other.m_icon)
            , m_parent(other.m_parent)
            , m_text(other.m_text)
            , m_tooltip(other.m_tooltip) {;}
        virtual ~BookmarkItemBase() {
            qDeleteAll(m_children);
        }

        /** Children routines. */
        inline void addChild(BookmarkItemBase * child) {
            child->setParent(this);
            Q_ASSERT(!m_children.contains(child));
            m_children.append(child);
        }

        inline int childCount() const { return m_children.size(); }

        inline BookmarkItemBase * child(int index) const {
            return m_children[index];
        }

        inline QList<BookmarkItemBase *> & children() {
            return m_children;
        }

        inline void insertChild(int index, BookmarkItemBase * child) {
            child->setParent(this);
            Q_ASSERT(!m_children.contains(child));
            m_children.insert(index, child);
        }

        inline void insertChildren(int index, QList<BookmarkItemBase *> children) {
            foreach(BookmarkItemBase *c, children)
                insertChild(index++, c);
        }

        inline void removeChild(int index) {
            delete m_children[index];
            m_children.removeAt(index);
        }


        inline void setText(const QString & text) { m_text = text; }

        inline const QString & text() const { return m_text; }

        inline void setToolTip(const QString & tooltip) { m_tooltip = tooltip; }

        virtual QString toolTip() const { return m_tooltip; }

        inline void setFlags(Qt::ItemFlags flags) { m_flags = flags; }

        inline Qt::ItemFlags flags() const { return m_flags; }

        inline void setIcon(const QIcon & icon) { m_icon = icon; }

        inline QIcon icon() const { return m_icon; }

        inline void setParent(BookmarkItemBase * parent) {
            m_parent = parent;
        }

        inline BookmarkItemBase * parent() const { return m_parent; }

        /**
          \returns index of this item in parent's child array.
         */
        inline int index() const {
            Q_CHECK_PTR(parent());
            for(int i = 0; i < parent()->childCount(); ++i)
                if(parent()->child(i) == this)
                    return i;
            return -1;
        }

    private:

        QList<BookmarkItemBase *> m_children;
        Qt::ItemFlags m_flags;
        QIcon m_icon;
        BookmarkItemBase * m_parent;
        QString m_text;
        QString m_tooltip;

    };

    class BookmarkItem : public BookmarkItemBase {
    public:
        friend class BookmarkLoader;

        BookmarkItem(BookmarkItemBase * parent);

        /** Creates a bookmark with module, key and description. */
        BookmarkItem(const CSwordModuleInfo & module, const QString & key,
                       const QString & description, const QString & title);

        /** Creates a copy. */
        BookmarkItem(const BookmarkItem & other);

        /** Returns the used module, 0 if there is no such module. */
        CSwordModuleInfo * module() const;

        /** Returns the used key. */
        QString key() const;

        inline void setKey(const QString & key) { m_key = key; }

        /** Returns the used description. */
        inline const QString &description() const { return m_description; }

        inline void setDescription(const QString & description) { m_description = description; }

        /** Returns a tooltip for this bookmark. */
        QString toolTip() const;

        /** Returns the english key.*/
        inline const QString & englishKey() const { return m_key; }

        inline void setModule(const QString & moduleName) { m_moduleName = moduleName; }

        inline const QString & moduleName() const { return m_moduleName; }

    private:
        QString m_key;
        QString m_description;
        QString m_moduleName;

    };

    class BookmarkFolder : public BookmarkItemBase {
    public:

        BookmarkFolder(const QString & name, BookmarkItemBase * parent = 0);

        /** Returns a list of direct childs of this item. */
        QList<BookmarkItemBase *> getChildList() const;

        /** Returns true if the given item is this or a direct or indirect subitem of this. */
        bool hasDescendant(BookmarkItemBase * item) const;

        /** Creates a deep copy of this item. */
        BookmarkFolder * deepCopy();

    };


public: /* Methods */

    BtBookmarksModelPrivate(BtBookmarksModel * parent)
        : m_rootItem(new BookmarkFolder("Root"))
        , q_ptr(parent)
    {
        m_saveTimer.setInterval(0.5 * 60 * 1000);
        m_saveTimer.setSingleShot(true);
    }
    ~BtBookmarksModelPrivate() { delete m_rootItem; }

    QString defaultBookmarksFile() {
        return QString();
    }

    BookmarkItemBase * item(const QModelIndex & index) const {
        if(index.isValid()) {
#ifdef QT_DEBUG
            {
                // check for item in tree
                QList<BookmarkItemBase *> items;
                items << m_rootItem;
                for(int c = 0; ; ++c) {
                    if(items[c] == index.internalPointer())
                        break;
                    if(items[c]->childCount())
                        items.append(items[c]->children());
                    Q_ASSERT(c < items.size());
                }
            }
#endif
            return reinterpret_cast<BookmarkItemBase *>(index.internalPointer());
        }
        else
            return m_rootItem;
    }

    /// \test
    void printItems() const {
        QList<BookmarkItemBase *> items;
        QList<int> spaces;
        items << m_rootItem;
        spaces << 0;
        for(int c = 0; c < items.size(); ++c) {
            qDebug() << QString().fill('\t', spaces[c]) << items[c]->text().left(24) << items[c]
                     << items[c]->parent() << items[c]->childCount();
            if(items[c]->childCount())
                for(int i = 0; i < items[c]->childCount(); ++i) {
                    items.insert(c + i + 1, items[c]->children()[i]);
                    spaces.insert(c + i + 1, spaces[c] + 1);
                }
        }
    }

    void needSave(){
        if(m_defaultModel == q_ptr){
            if(!m_saveTimer.isActive())
                m_saveTimer.start();
        }
    }
public: /* Loader */

    /** Loads a list of items (with subitem trees) from a named file
    * or from the default bookmarks file. */
    QList<BookmarkItemBase *> loadTree(QString fileName = QString::null) {
        QList<BookmarkItemBase*> itemList;

        QDomDocument doc;
        doc.setContent(loadXmlFromFile(fileName));

        //bookmarkfolder::loadBookmarksFromXML()

        QDomElement document = doc.documentElement();
        if ( document.tagName() != "SwordBookmarks" ) {
            qWarning("Not a BibleTime Bookmark XML file");
            return QList<BookmarkItemBase*>();
        }

        QDomElement child = document.firstChild().toElement();

        while ( !child.isNull() && child.parentNode() == document) {
            BookmarkItemBase* i = handleXmlElement(child, 0);
            itemList.append(i);
            if (!child.nextSibling().isNull()) {
                child = child.nextSibling().toElement();
            }
            else {
                child = QDomElement(); //null
            }

        }

        return itemList;
    }

    /** Create a new item from a document element. */
    BookmarkItemBase * handleXmlElement(QDomElement & element, BookmarkItemBase * parent) {
        BookmarkItemBase* newItem = 0;
        if (element.tagName() == "Folder") {
            BookmarkFolder* newFolder = new BookmarkFolder(QString::null, parent);
            if (element.hasAttribute("caption")) {
                newFolder->setText(element.attribute("caption"));
            }
            QDomNodeList childList = element.childNodes();
    #if QT_VERSION < 0x050000
            for (unsigned int i = 0; i < childList.length(); i++) {
    #else
            for (int i = 0; i < childList.length(); i++) {
    #endif
                QDomElement newElement = childList.at(i).toElement();
                handleXmlElement(newElement, newFolder); // passing parent in constructor will add items to tree
            }
            newItem = newFolder;
        }
        else if (element.tagName() == "Bookmark") {
            BookmarkItem* newBookmarkItem = new BookmarkItem(parent);
            if (element.hasAttribute("modulename")) {
                //we use the name in all cases, even if the module isn't installed anymore
                newBookmarkItem->setModule(element.attribute("modulename"));
            }
            if (element.hasAttribute("key")) {
                newBookmarkItem->setKey(element.attribute("key"));
            }
            if (element.hasAttribute("description")) {
                newBookmarkItem->setDescription(element.attribute("description"));
            }
            if (element.hasAttribute("title")) {
                newBookmarkItem->setText(element.attribute("title"));
            }
            newItem = newBookmarkItem;
        }
        return newItem;
    }

    /** Loads a bookmark XML document from a named file or from the default bookmarks file. */
    QString loadXmlFromFile(QString fileName = QString::null) {
        namespace DU = util::directory;

        if (fileName.isNull()) {
            fileName = DU::getUserBaseDir().absolutePath() + "/bookmarks.xml";
        }
        QFile file(fileName);
        if (!file.exists())
            return QString::null;

        QString xml;
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream t;
            t.setAutoDetectUnicode(false);
            t.setCodec(QTextCodec::codecForName("UTF-8"));
            t.setDevice(&file);
            xml = t.readAll();
            file.close();
        }
        return xml;
    }

    /** Takes one item and saves the tree which is under it to a named file
    * or to the default bookmarks file, asking the user about overwriting if necessary. */
    void saveTreeFromRootItem(BookmarkItemBase* rootItem, QString fileName = QString::null, bool forceOverwrite = true) {
        namespace DU = util::directory;

        Q_ASSERT(rootItem);
        if (fileName.isNull()) {
            fileName = DU::getUserBaseDir().absolutePath() + "/bookmarks.xml";
        }

        QDomDocument doc("DOC");
        doc.appendChild( doc.createProcessingInstruction( "xml", "version=\"1.0\" encoding=\"UTF-8\"" ) );

        QDomElement content = doc.createElement("SwordBookmarks");
        content.setAttribute("syntaxVersion", CURRENT_SYNTAX_VERSION);
        doc.appendChild(content);

        //append the XML nodes of all child items

        for (int i = 0; i < rootItem->childCount(); i++) {
            saveItem(rootItem->child(i), content);
        }
        util::tool::savePlainFile(fileName, doc.toString(), forceOverwrite, QTextCodec::codecForName("UTF-8"));

    }

    /** Writes one item to a document element. */
    void saveItem(BookmarkItemBase * item, QDomElement & parentElement) {
        BookmarkFolder* folderItem = 0;
        BookmarkItem* bookmarkItem = 0;

        if ((folderItem = dynamic_cast<BookmarkFolder*>(item))) {
            QDomElement elem = parentElement.ownerDocument().createElement("Folder");
            elem.setAttribute("caption", folderItem->text());

            parentElement.appendChild(elem);

            for (int i = 0; i < folderItem->childCount(); i++) {
                saveItem(folderItem->child(i), elem);
            }
        }
        else if ((bookmarkItem = dynamic_cast<BookmarkItem*>(item))) {
            QDomElement elem = parentElement.ownerDocument().createElement("Bookmark");

            elem.setAttribute("key", bookmarkItem->englishKey());
            elem.setAttribute("description", bookmarkItem->description());
            elem.setAttribute("modulename", bookmarkItem->moduleName());
            elem.setAttribute("moduledescription", bookmarkItem->module() ? bookmarkItem->module()->config(CSwordModuleInfo::Description) : QString::null);
        if (!bookmarkItem->text().isEmpty()) {
            elem.setAttribute("title", bookmarkItem->text());
        }
            parentElement.appendChild(elem);
        }
    }


public: /* Fields */

    BookmarkFolder * m_rootItem;
    QTimer m_saveTimer;
    static BtBookmarksModel * m_defaultModel;

    Q_DECLARE_PUBLIC(BtBookmarksModel);
    BtBookmarksModel * const q_ptr;

};

BtBookmarksModel * BtBookmarksModelPrivate::m_defaultModel = 0;

typedef BtBookmarksModelPrivate::BookmarkItemBase BookmarkItemBase;
typedef BtBookmarksModelPrivate::BookmarkItem BookmarkItem;
typedef BtBookmarksModelPrivate::BookmarkFolder BookmarkFolder;


BookmarkFolder::BookmarkFolder(const QString & name, BookmarkItemBase * parent)
        : BookmarkItemBase(parent) {
    setText(name);
    setFlags(Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEnabled);
    setIcon(util::getIcon(CResMgr::mainIndex::closedFolder::icon));
}

QList<BookmarkItemBase*> BookmarkFolder::getChildList() const {
    QList<BookmarkItemBase*> list;
    for (int i = 0; i < childCount(); i++) {
        list.append(child(i));
    }
    return list;
}

bool BookmarkFolder::hasDescendant(BookmarkItemBase* item) const {
    if (this == item) {
        return true;
    }
    if (getChildList().indexOf(item) > -1) {
        return true;
    }
    foreach(BookmarkItemBase* childItem, getChildList()) {
        bool subresult = false;
        BookmarkFolder* folder = 0;
        if ( (folder = dynamic_cast<BookmarkFolder*>(childItem)) ) {
            subresult = folder->hasDescendant(childItem);
        }

        if (subresult == true) {
            return true;
        }
    }
    return false;
}

BookmarkFolder* BookmarkFolder::deepCopy() {
    BookmarkFolder* newFolder = new BookmarkFolder(this->text());
    foreach(BookmarkItemBase* subitem, getChildList()) {
        if (BookmarkItem* bmItem = dynamic_cast<BookmarkItem*>(subitem)) {
            newFolder->addChild(new BookmarkItem(*bmItem));
        }
        else {
            if (BookmarkFolder* bmFolder = dynamic_cast<BookmarkFolder*>(subitem)) {
                newFolder->addChild(bmFolder->deepCopy());
            }
        }
    }
    return newFolder;
}

BookmarkItem::BookmarkItem(CSwordModuleInfo const & module,
                               const QString & key,
                               const QString & description,
                               const QString & title)
        : m_description(description)
        , m_moduleName(module.name())
{
    Q_UNUSED(title);

    if (((module.type() == CSwordModuleInfo::Bible) || (module.type() == CSwordModuleInfo::Commentary))) {
        CSwordVerseKey vk(0);
        vk.setKey(key);
        vk.setLocale("en");
        m_key = vk.key(); //the m_key member is always the english key!
    }
    else {
        m_key = key;
    };

    setIcon(util::getIcon(CResMgr::mainIndex::bookmark::icon));
    setText(QString::fromLatin1("%1 (%2)").arg(key).arg(module.name()));
    setFlags(Qt::ItemIsSelectable /*| Qt::ItemIsEditable*/ | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEnabled);
}

BookmarkItem::BookmarkItem(BookmarkItemBase * parent)
        : BookmarkItemBase(parent) {
    setFlags(Qt::ItemIsSelectable /*| Qt::ItemIsEditable*/ | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEnabled);
    setIcon(util::getIcon(CResMgr::mainIndex::bookmark::icon));
    setText(QString::fromLatin1("%1 (%2)").arg(key()).arg(module() ? module()->name() : QObject::tr("unknown")));
}

BookmarkItem::BookmarkItem(const BookmarkItem & other)
        : BookmarkItemBase(other)
        , m_key(other.m_key)
        , m_description(other.m_description)
        , m_moduleName(other.m_moduleName)
{
    setIcon(util::getIcon(CResMgr::mainIndex::bookmark::icon));
    setText(QString::fromLatin1("%1 (%2)").arg(key()).arg(module() ? module()->name() : QObject::tr("unknown")));
}

CSwordModuleInfo *BookmarkItem::module() const {
    return CSwordBackend::instance()->findModuleByName(m_moduleName);
}

QString BookmarkItem::key() const {
    const QString englishKeyName = englishKey();
    if (!module()) {
        return englishKeyName;
    }

    QString returnKeyName = englishKeyName;
    if ((module()->type() == CSwordModuleInfo::Bible) || (module()->type() == CSwordModuleInfo::Commentary)) {
        CSwordVerseKey vk(0);
        vk.setKey(englishKeyName);
        vk.setLocale(CSwordBackend::instance()->booknameLanguage().toLatin1() );

        returnKeyName = vk.key(); //the returned key is always in the currently set bookname language
    }

    return returnKeyName;
}

QString BookmarkItem::toolTip() const {
    if (!module()) {
        return QString::null;
    }

    FilterOptions filterOptions = btConfig().getFilterOptions();
    filterOptions.footnotes = false;
    filterOptions.scriptureReferences = false;
    CSwordBackend::instance()->setFilterOptions(filterOptions);

    QString ret;
    QSharedPointer<CSwordKey> k( CSwordKey::createInstance(module()) );
    k->setKey(key());

    // const CLanguageMgr::Language* lang = module()->language();
    // BtConfig::FontSettingsPair fontPair = getBtConfig().getFontForLanguage(lang);

    Q_ASSERT(k.data());
    QString header = QString::fromLatin1("%1 (%2)")
              .arg(key())
              .arg(module()->name());
    if (text() != header) {
        ret = QString::fromLatin1("<b>%1</b><br>%2<hr>%3")
              .arg(header)
              .arg(text())
              .arg(description())
              ;
    }
    else {
        ret = QString::fromLatin1("<b>%1</b><hr>%2")
              .arg(header)
              .arg(description())
              ;
    }

    return ret;
}


BtBookmarksModel::BtBookmarksModel(QObject * parent)
    : QAbstractItemModel(parent)
    , d_ptr(new BtBookmarksModelPrivate(this))
{
    load();
}

BtBookmarksModel::BtBookmarksModel(const QString & fileName, const QString & rootFolder, QObject * parent)
    : QAbstractItemModel(parent)
    , d_ptr(new BtBookmarksModelPrivate(this))
{
    /// \todo take into account rootFolder
    Q_ASSERT(rootFolder.isEmpty() && "specifying root folder for bookmarks is not supported at moment");

    load(fileName);
}

BtBookmarksModel::~BtBookmarksModel() {
    Q_D(BtBookmarksModel);

    if(d->m_saveTimer.isActive())
        save();

    delete d_ptr;
}

int BtBookmarksModel::rowCount(const QModelIndex & parent) const {
    Q_D(const BtBookmarksModel);

    return d->item(parent)->childCount();
}

int BtBookmarksModel::columnCount(const QModelIndex & parent) const {
    Q_UNUSED(parent);
    return 1;
}

bool BtBookmarksModel::hasChildren(const QModelIndex & parent) const {
    return rowCount(parent) > 0;
}
QModelIndex BtBookmarksModel::index(int row, int column, const QModelIndex & parent) const {
    Q_D(const BtBookmarksModel);

    const BookmarkItemBase * i = d->item(parent);
    if(i->childCount() > row)
        return createIndex(row, column, i->child(row));
    return QModelIndex();
}

QModelIndex BtBookmarksModel::parent(const QModelIndex & index) const {
    Q_D(const BtBookmarksModel);

    const BookmarkItemBase * i = d->item(index);
    return (i->parent() == 0 || i->parent()->parent() == 0) ?
                QModelIndex() : createIndex(i->parent()->index(), 0, i->parent());
}

QVariant BtBookmarksModel::data(const QModelIndex & index, int role) const {
    Q_D(const BtBookmarksModel);

    const BookmarkItemBase * i = d->item(index);

    switch(role)
    {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return i->text();
        case Qt::ToolTipRole:
            return i->toolTip();
        case Qt::DecorationRole:
            return i->icon();
    }
    return QVariant();
}

Qt::ItemFlags BtBookmarksModel::flags(const QModelIndex & index) const {
    Q_D(const BtBookmarksModel);

    return d->item(index)->flags();
}

QVariant BtBookmarksModel::headerData(int section, Qt::Orientation orientation, int role) const {
    Q_UNUSED(section);
    Q_UNUSED(orientation);
    Q_UNUSED(role);

    return QVariant();
}

bool BtBookmarksModel::setData(const QModelIndex & index, const QVariant & val, int role) {
    Q_D(BtBookmarksModel);

    BookmarkItemBase * i = d->item(index);
    switch(role)
    {
        case Qt::DisplayRole:
        case Qt::EditRole:
        {
            i->setText(val.toString());
            if(dynamic_cast<BookmarkFolder *>(i) || dynamic_cast<BookmarkItem *>(i))
                d->needSave();
            return true;
        }
        case Qt::ToolTipRole:
        {
            i->setToolTip(val.toString());
            if(dynamic_cast<BookmarkFolder *>(i) || dynamic_cast<BookmarkItem *>(i))
                d->needSave();
            return true;
        }
    }
    return false;
}

bool BtBookmarksModel::removeRows(int row, int count, const QModelIndex & parent)
{
    Q_D(BtBookmarksModel);

    Q_ASSERT(rowCount(parent) >= row + count);

    beginRemoveRows(parent, row, row + count - 1);

    for(int i = 0; i < count; ++i) {
        d->item(parent)->removeChild(row);
    }
    endRemoveRows();

    d->needSave();

    return true;
}

bool BtBookmarksModel::insertRows(int row, int count, const QModelIndex &parent)
{
    Q_D(BtBookmarksModel);

    Q_ASSERT(rowCount(parent) >= row + count - 1);

    beginInsertRows(parent, row, row + count - 1);

    for(int i = 0; i < count; ++i) {
        d->item(parent)->insertChild(row, new BookmarkItemBase);
    }
    endInsertRows();

    return true;

}

bool BtBookmarksModel::save(QString fileName, const QModelIndex & rootItem) {
    Q_D(BtBookmarksModel);

    BookmarkItemBase * i = d->item(rootItem);
    d->saveTreeFromRootItem(i, fileName, fileName.isEmpty());

    if(d->m_saveTimer.isActive())
        d->m_saveTimer.stop();

    return true;
}

bool BtBookmarksModel::load(QString fileName, const QModelIndex & rootItem) {
    Q_D(BtBookmarksModel);

    BookmarkItemBase * i = d->item(rootItem);
    QList<BookmarkItemBase *> items = d->loadTree(fileName);

    if(items.size() == 0)
        return false;

    beginInsertRows(rootItem, i->childCount(), i->childCount() + items.size() - 1);

    i->insertChildren(i->childCount(), items);

    endInsertRows();

    if(!rootItem.isValid() && fileName.isEmpty()) {
        if(!d->m_defaultModel) {
            connect(&d->m_saveTimer, SIGNAL(timeout()), this, SLOT(save()));
            d->m_defaultModel = this;
        }
        else
            Q_ASSERT_X(false, "BtBookmarksModel::load" ,
                       "no more than one default bookmarks model is allowed");
    }
    else
        d->needSave();

    return true;
}

bool BtBookmarksModel::isFolder(const QModelIndex &index) const
{
    Q_D(const BtBookmarksModel);
    return dynamic_cast<const BookmarkFolder*>(d->item(index));
}

bool BtBookmarksModel::isBookmark(const QModelIndex &index) const
{
    Q_D(const BtBookmarksModel);
    return dynamic_cast<const BookmarkItem*>(d->item(index));
}

QModelIndexList BtBookmarksModel::copyItems(int row, const QModelIndex & parent, const QModelIndexList & toCopy)
{
    Q_D(BtBookmarksModel);

    bool bookmarksOnly = true;
    bool targetIncluded = false;
    bool moreThanOneFolder = false;

    QList<BookmarkItemBase *> newList;

    foreach(QModelIndex index, toCopy) {
        if(BookmarkFolder * folder = dynamic_cast<BookmarkFolder*>(d->item(index))) {
            bookmarksOnly = false;
            if (toCopy.count() > 1) { // only one item allowed if a folder is selected
                moreThanOneFolder = true;
                break;
            }
            if (folder->hasDescendant(d->item(parent))) { // dropping to self or descendand not allowed
                targetIncluded = true;
                break;
            }
        }
        else {
            newList.append( new BookmarkItem(*(dynamic_cast<BookmarkItem *>(d->item(index))) ));
        }
    }

    if (!bookmarksOnly && toCopy.count() == 1) {
        BookmarkFolder* folder = dynamic_cast<BookmarkFolder*>(d->item(toCopy[0]));
        BookmarkFolder* copy = folder->deepCopy();
        newList.append(copy);
    }
    if (!bookmarksOnly && toCopy.count() > 1) {
        // wrong amount of items
        moreThanOneFolder = true;
    }

    if (moreThanOneFolder || targetIncluded) {
        return QModelIndexList();
    }


    beginInsertRows(parent, row, row + newList.size() - 1);

    d->item(parent)->insertChildren(row, newList);

    endInsertRows();

    d->needSave();

    QModelIndexList result;
    for(int i = 0; i < newList.size(); ++i) {
        result.append(index(row + i, 0, parent));
    }
    return result;
}

CSwordModuleInfo * BtBookmarksModel::module(const QModelIndex & index) const
{
    Q_D(const BtBookmarksModel);

    const BookmarkItem * i = dynamic_cast<const BookmarkItem *>(d->item(index));
    if(i) return i->module();
    return 0;
}

QString BtBookmarksModel::key(const QModelIndex & index) const
{
    Q_D(const BtBookmarksModel);

    const BookmarkItem * i = dynamic_cast<const BookmarkItem *>(d->item(index));
    if(i) return i->key();
    return QString();
}

QString BtBookmarksModel::description(const QModelIndex &index) const
{
    Q_D(const BtBookmarksModel);

    const BookmarkItem * i = dynamic_cast<const BookmarkItem *>(d->item(index));
    if(i) return i->description();
    return QString();
}

void BtBookmarksModel::setDescription(const QModelIndex &index, const QString &description)
{
    Q_D(BtBookmarksModel);

    BookmarkItem * i = dynamic_cast<BookmarkItem *>(d->item(index));
    if(i) return i->setDescription(description);

    d->needSave();
}

QModelIndex BtBookmarksModel::addBookmark(int const row,
                                          QModelIndex const & parent,
                                          CSwordModuleInfo const & module,
                                          QString const & key,
                                          QString const & description,
                                          QString const & title)
{
    Q_D(BtBookmarksModel);

    BookmarkFolder * i = dynamic_cast<BookmarkFolder *>(d->item(parent));
    if(i) {
        beginInsertRows(parent, row, row);

        BookmarkItem * c = new BookmarkItem(module, key, description, title);
        i->insertChild(row, c);

        endInsertRows();

        d->needSave();

        return createIndex(c->index(), 0, c);
    }
    return QModelIndex();
}

QModelIndex BtBookmarksModel::addFolder(int row, const QModelIndex &parent, const QString &name)
{
    Q_D(BtBookmarksModel);

    BookmarkFolder * i = dynamic_cast<BookmarkFolder *>(d->item(parent));
    if(i) {
        beginInsertRows(parent, row, row);

        BookmarkFolder * c = new BookmarkFolder(name.isEmpty() ? QObject::tr("New folder") : name);
        i->insertChild(row, c);

        endInsertRows();

        d->needSave();

        return createIndex(c->index(), 0, c);
    }
    return QModelIndex();
}

bool BtBookmarksModel::hasDescendant(const QModelIndex &baseIndex, const QModelIndex &testIndex) const
{
    Q_D(const BtBookmarksModel);

    if(const BookmarkFolder * f = dynamic_cast<const BookmarkFolder *>(d->item(baseIndex)))
        return f->hasDescendant(d->item(testIndex));
    return false;
}

bool BtBookmarksModelSortAscending(BookmarkItemBase * i1, BookmarkItemBase * i2)
{
    return i1->text().localeAwareCompare(i2->text()) < 0;
}

bool BtBookmarksModelSortDescending(BookmarkItemBase * i1, BookmarkItemBase * i2)
{
    return i1->text().localeAwareCompare(i2->text()) > 0;
}

void BtBookmarksModel::sort(const QModelIndex & parent, Qt::SortOrder order)
{
    Q_D(BtBookmarksModel);

    if(BookmarkFolder * f = dynamic_cast<BookmarkFolder *>(d->item(parent))) {
        QList<BookmarkFolder *> parents;
        if(f == d->m_rootItem) {
            QList<BookmarkItemBase *> items;
            items.append(f);
            for(int i = 0; i < items.size(); ++i) {
                items.append(items[i]->children());
                if(BookmarkFolder * ff = dynamic_cast<BookmarkFolder *>(items[i]))
                    parents.append(ff);
            }
        }
        else
            parents.append(f);

        foreach(BookmarkFolder * f, parents) {
            emit layoutAboutToBeChanged();

            QModelIndexList indexes;
            for(int i = 0; i < f->children().size(); ++i)
                indexes.append(createIndex(i, 0, f->children()[i]));

            qSort(f->children().begin(), f->children().end(), order == Qt::AscendingOrder ?
                      BtBookmarksModelSortAscending : BtBookmarksModelSortDescending);

            for(int i = 0; i < f->children().size(); ++i) {
                BookmarkItemBase * iii = f->children()[i];
                for(int ii = 0; ii < indexes.size(); ++ii)
                    if(iii == indexes[ii].internalPointer())
                        changePersistentIndex(createIndex(ii, 0, iii), createIndex(i, 0, iii));
            }
            emit layoutChanged();

            d->needSave();
        }
    }
}
