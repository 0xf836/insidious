#include "ui/collection_tree_model.h"

#include <QSignalSpy>
#include <QtTest>

using namespace insidious;
using namespace insidious::ui;

class CollectionTreeModelTests : public QObject {
    Q_OBJECT

private slots:
    void pathLookupFindsNestedFolders();
    void lazyLoadUpdateInsertsRowsWithoutReset();
};

namespace {

std::shared_ptr<core::CollectionFolderNode> folder(QString relativePath, QString displayName)
{
    auto node = std::make_shared<core::CollectionFolderNode>();
    node->relativePath = std::move(relativePath);
    node->displayName = std::move(displayName);
    return node;
}

} // namespace

void CollectionTreeModelTests::pathLookupFindsNestedFolders()
{
    auto root = folder(QString(), QStringLiteral("root"));
    auto child = folder(QStringLiteral("a"), QStringLiteral("a"));
    auto nested = folder(QStringLiteral("a/b"), QStringLiteral("b"));
    child->children.append(nested);
    root->children.append(child);

    CollectionTreeModel model;
    model.setRoot(root);

    const QModelIndex childIndex = model.indexForFolderPath(QStringLiteral("a"));
    QVERIFY(childIndex.isValid());
    QCOMPARE(model.nodeForFolderPath(QStringLiteral("a")), child.get());
    QCOMPARE(model.nodeForFolderPath(QStringLiteral("a/b")), nested.get());

    const QModelIndex nestedIndex = model.indexForFolderPath(QStringLiteral("a/b"));
    QVERIFY(nestedIndex.isValid());
    QCOMPARE(model.parent(nestedIndex), childIndex);
    QCOMPARE(model.data(nestedIndex, Qt::DisplayRole).toString(), QStringLiteral("b"));
}

void CollectionTreeModelTests::lazyLoadUpdateInsertsRowsWithoutReset()
{
    auto root = folder(QString(), QStringLiteral("root"));
    auto unloaded = folder(QStringLiteral("a"), QStringLiteral("a"));
    unloaded->canLoadChildren = true;
    root->children.append(unloaded);

    CollectionTreeModel model;
    model.setRoot(root);
    const QModelIndex parentIndex = model.indexForFolderPath(QStringLiteral("a"));
    QVERIFY(parentIndex.isValid());
    QCOMPARE(model.rowCount(parentIndex), 0);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy rowsInsertedSpy(&model, &QAbstractItemModel::rowsInserted);

    auto child = folder(QStringLiteral("a/child"), QStringLiteral("child"));
    QVector<std::shared_ptr<core::CollectionFolderNode>> children { child };
    QVector<core::ImageEntry> images;
    core::ImageEntry image;
    image.displayName = QStringLiteral("image.png");
    images.append(image);

    QVERIFY(model.replaceLoadedFolderContents(unloaded.get(), std::move(images), std::move(children)));

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(rowsInsertedSpy.count(), 1);
    QCOMPARE(model.rowCount(parentIndex), 1);
    QVERIFY(unloaded->loaded);
    QCOMPARE(unloaded->images.size(), 1);
    QCOMPARE(model.nodeForFolderPath(QStringLiteral("a/child")), child.get());

    const QModelIndex childIndex = model.indexForFolderPath(QStringLiteral("a/child"));
    QVERIFY(childIndex.isValid());
    QCOMPARE(model.parent(childIndex), parentIndex);
}

QTEST_MAIN(CollectionTreeModelTests)

#include "collection_tree_model_tests.moc"
